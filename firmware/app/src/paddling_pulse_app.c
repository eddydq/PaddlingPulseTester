/**
 ****************************************************************************************
 *
 * @file paddling_pulse_app.c
 *
 * @brief PaddlingPulse application source code.
 *
 * Copyright (C) 2015-2023 Renesas Electronics Corporation and/or its affiliates.
 * All rights reserved. Confidential Information.
 *
 * This software ("Software") is supplied by Renesas Electronics Corporation and/or its
 * affiliates ("Renesas"). Renesas grants you a personal, non-exclusive, non-transferable,
 * revocable, non-sub-licensable right and license to use the Software, solely if used in
 * or together with Renesas products. You may make copies of this Software, provided this
 * copyright notice and disclaimer ("Notice") is included in all such copies. Renesas
 * reserves the right to change or discontinue the Software at any time without notice.
 *
 * THE SOFTWARE IS PROVIDED "AS IS". RENESAS DISCLAIMS ALL WARRANTIES OF ANY KIND,
 * WHETHER EXPRESS, IMPLIED, OR STATUTORY, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. TO THE
 * MAXIMUM EXTENT PERMITTED UNDER LAW, IN NO EVENT SHALL RENESAS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE, EVEN IF RENESAS HAS BEEN ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES. USE OF THIS SOFTWARE MAY BE SUBJECT TO TERMS AND CONDITIONS CONTAINED IN
 * AN ADDITIONAL AGREEMENT BETWEEN YOU AND RENESAS. IN CASE OF CONFLICT BETWEEN THE TERMS
 * OF THIS NOTICE AND ANY SUCH ADDITIONAL LICENSE AGREEMENT, THE TERMS OF THE AGREEMENT
 * SHALL TAKE PRECEDENCE. BY CONTINUING TO USE THIS SOFTWARE, YOU AGREE TO THE TERMS OF
 * THIS NOTICE.IF YOU DO NOT AGREE TO THESE TERMS, YOU ARE NOT PERMITTED TO USE THIS
 * SOFTWARE.
 *
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @addtogroup APP
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "rwip_config.h"             // SW configuration
#include "gattc_task.h"
#include "gap.h"
#include "app_easy_timer.h"
#include "paddling_pulse_app.h"
#include "paddling_pulse_console.h"
#include "paddling_pulse_console_io.h"
#include "paddling_pulse_imu.h"
#include "paddling_pulse_sample_store.h"
#include "paddling_pulse_stroke_rate.h"
#include "pp_graph.h"
#include "pp_pipeline_service.h"
#include "pp_storage.h"
#include "co_bt.h"
#include "app_cscps.h"


/*
 * TYPE DEFINITIONS
 ****************************************************************************************
 */

// Manufacturer Specific Data ADV structure type
struct mnf_specific_data_ad_structure
{
    uint8_t ad_structure_size;
    uint8_t ad_structure_type;
    uint8_t company_id[APP_AD_MSD_COMPANY_ID_LEN];
    uint8_t proprietary_data[APP_AD_MSD_DATA_LEN];
};

/*
 * GLOBAL VARIABLE DEFINITIONS
 ****************************************************************************************
 */

uint8_t app_connection_idx                      __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
timer_hnd app_adv_data_update_timer_used        __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
timer_hnd app_param_update_request_timer_used   __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
timer_hnd app_csc_meas_timer_used               __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
timer_hnd app_imu_process_timer_used            __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
timer_hnd app_stroke_rate_timer_used            __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY

// Retained variables
struct mnf_specific_data_ad_structure mnf_data  __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
// Index of manufacturer data in advertising data or scan response data (when MSB is 1)
uint8_t mnf_data_index                          __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
uint8_t stored_adv_data_len                     __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
uint8_t stored_scan_rsp_data_len                __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
uint8_t stored_adv_data[ADV_DATA_LEN]           __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
uint8_t stored_scan_rsp_data[SCAN_RSP_DATA_LEN] __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
bool csc_meas_ntf_enabled                       __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
bool imu_active                                 __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
uint8_t current_cadence_rpm                     __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
struct cscp_csc_meas csc_meas_state             __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY

static pp_graph_t s_pipeline_graph;
static uint8_t s_pipeline_binary[PP_PIPELINE_MAX_BYTES];
static uint8_t s_pipeline_ready;
static uint8_t s_pipeline_rpm;
static uint8_t s_node_state[PP_MAX_NODES][32];

static uint8_t s_source_params[3];
static uint8_t s_select_axis_params[1];
static uint8_t s_hpf_params[2];
static uint8_t s_autocorr_params[6];
static uint8_t s_kalman_params[7];

/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
*/

#ifdef CFG_PADDLING_PULSE_CONSOLE_MODE
static void ble_log_peer(const char *message, uint8_t conidx, uint16_t conhdl,
                         uint8_t addr_type, const struct bd_addr *addr)
{
    paddling_pulse_console_printf("BLE: %s idx=%u h=%u type=%u addr=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                                  message,
                                  conidx,
                                  conhdl,
                                  addr_type,
                                  addr->addr[5], addr->addr[4], addr->addr[3],
                                  addr->addr[2], addr->addr[1], addr->addr[0]);
}

static void ble_log_cfg(const char *message, uint8_t conidx)
{
    paddling_pulse_console_printf("BLE: %s idx=%u\r\n", message, conidx);
}
#endif



/**
 ****************************************************************************************
 * @brief Initialize Manufacturer Specific Data
 ****************************************************************************************
 */
static void mnf_data_init()
{
    mnf_data.ad_structure_size = sizeof(struct mnf_specific_data_ad_structure ) - sizeof(uint8_t); // minus the size of the ad_structure_size field
    mnf_data.ad_structure_type = GAP_AD_TYPE_MANU_SPECIFIC_DATA;
    mnf_data.company_id[0] = APP_AD_MSD_COMPANY_ID & 0xFF; // LSB
    mnf_data.company_id[1] = (APP_AD_MSD_COMPANY_ID >> 8 )& 0xFF; // MSB
    mnf_data.proprietary_data[0] = 0;
    mnf_data.proprietary_data[1] = 0;
}

/**
 ****************************************************************************************
 * @brief Update Manufacturer Specific Data
 ****************************************************************************************
 */
static void mnf_data_update()
{
    uint16_t data;

    data = mnf_data.proprietary_data[0] | (mnf_data.proprietary_data[1] << 8);
    data += 1;
    mnf_data.proprietary_data[0] = data & 0xFF;
    mnf_data.proprietary_data[1] = (data >> 8) & 0xFF;

    if (data == 0xFFFF) {
         mnf_data.proprietary_data[0] = 0;
         mnf_data.proprietary_data[1] = 0;
    }
}

/**
 ****************************************************************************************
 * @brief Add an AD structure in the Advertising or Scan Response Data of the
 *        GAPM_START_ADVERTISE_CMD parameter struct.
 * @param[in] cmd               GAPM_START_ADVERTISE_CMD parameter struct
 * @param[in] ad_struct_data    AD structure buffer
 * @param[in] ad_struct_len     AD structure length
 * @param[in] adv_connectable   Connectable advertising event or not. It controls whether
 *                              the advertising data use the full 31 bytes length or only
 *                              28 bytes (Document CCSv6 - Part 1.3 Flags).
 ****************************************************************************************
 */
static void app_add_ad_struct(struct gapm_start_advertise_cmd *cmd, void *ad_struct_data, uint8_t ad_struct_len, uint8_t adv_connectable)
{
    uint8_t adv_data_max_size = (adv_connectable) ? (ADV_DATA_LEN - 3) : (ADV_DATA_LEN);
    
    if ((adv_data_max_size - cmd->info.host.adv_data_len) >= ad_struct_len)
    {
        // Append manufacturer data to advertising data
        memcpy(&cmd->info.host.adv_data[cmd->info.host.adv_data_len], ad_struct_data, ad_struct_len);

        // Update Advertising Data Length
        cmd->info.host.adv_data_len += ad_struct_len;
        
        // Store index of manufacturer data which are included in the advertising data
        mnf_data_index = cmd->info.host.adv_data_len - sizeof(struct mnf_specific_data_ad_structure);
    }
    else if ((SCAN_RSP_DATA_LEN - cmd->info.host.scan_rsp_data_len) >= ad_struct_len)
    {
        // Append manufacturer data to scan response data
        memcpy(&cmd->info.host.scan_rsp_data[cmd->info.host.scan_rsp_data_len], ad_struct_data, ad_struct_len);

        // Update Scan Response Data Length
        cmd->info.host.scan_rsp_data_len += ad_struct_len;
        
        // Store index of manufacturer data which are included in the scan response data
        mnf_data_index = cmd->info.host.scan_rsp_data_len - sizeof(struct mnf_specific_data_ad_structure);
        // Mark that manufacturer data is in scan response and not advertising data
        mnf_data_index |= 0x80;
    }
    else
    {
        // Manufacturer Specific Data do not fit in either Advertising Data or Scan Response Data
        ASSERT_WARNING(0);
    }
    // Store advertising data length
    stored_adv_data_len = cmd->info.host.adv_data_len;
    // Store advertising data
    memcpy(stored_adv_data, cmd->info.host.adv_data, stored_adv_data_len);
    // Store scan response data length
    stored_scan_rsp_data_len = cmd->info.host.scan_rsp_data_len;
    // Store scan_response data
    memcpy(stored_scan_rsp_data, cmd->info.host.scan_rsp_data, stored_scan_rsp_data_len);
}

/**
 ****************************************************************************************
 * @brief Advertisement data update timer callback function.
 ****************************************************************************************
*/
static void adv_data_update_timer_cb()
{
    // If mnd_data_index has MSB set, manufacturer data is stored in scan response
    uint8_t *mnf_data_storage = (mnf_data_index & 0x80) ? stored_scan_rsp_data : stored_adv_data;

    // Update manufacturer data
    mnf_data_update();

    // Update the selected fields of the advertising data (manufacturer data)
    memcpy(mnf_data_storage + (mnf_data_index & 0x7F), &mnf_data, sizeof(struct mnf_specific_data_ad_structure));

    // Update advertising data on the fly
    app_easy_gap_update_adv_data(stored_adv_data, stored_adv_data_len, stored_scan_rsp_data, stored_scan_rsp_data_len);
    
    // Restart timer for the next advertising update
    app_adv_data_update_timer_used = app_easy_timer(APP_ADV_DATA_UPDATE_TO, adv_data_update_timer_cb);
}

/**
 ****************************************************************************************
 * @brief Parameter update request timer callback function.
 ****************************************************************************************
*/
static void param_update_request_timer_cb()
{
    app_easy_gap_param_update_start(app_connection_idx);
    app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;
}

static void write_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)(value >> 8);
}

static uint16_t pipeline_sample_rate_hz(void)
{
#ifdef CFG_IMU_POLAR
    return 52;
#else
    return 100;
#endif
}

static uint8_t pipeline_axis_param(void)
{
#ifdef CFG_IMU_AXIS_X
    return PP_AXIS_X;
#elif defined(CFG_IMU_AXIS_Y)
    return PP_AXIS_Y;
#else
    return PP_AXIS_Z;
#endif
}

static uint8_t pipeline_source_block_id(void)
{
#ifdef CFG_IMU_LIS3DH
    return PP_BLOCK_LIS3DH_SOURCE;
#elif defined(CFG_IMU_MPU6050)
    return PP_BLOCK_MPU6050_SOURCE;
#else
    return PP_BLOCK_POLAR_SOURCE;
#endif
}

static void pipeline_assign_node_states(pp_graph_t *graph)
{
    uint8_t i;
    memset(s_node_state, 0, sizeof(s_node_state));
    for (i = 0; i < graph->node_count; i++)
    {
        const pp_block_manifest_t *manifest = pp_block_get_manifest(graph->nodes[i].block_id);
        if (manifest && manifest->state_size > 0 && manifest->state_size <= sizeof(s_node_state[i]))
        {
            graph->nodes[i].state = s_node_state[i];
        }
    }
}

static uint8_t pipeline_build_default_graph(void)
{
    uint16_t rate_hz = pipeline_sample_rate_hz();
    uint16_t min_lag = (uint16_t)((60UL * rate_hz) / PP_STROKE_RATE_MAX_RPM);
    uint16_t max_lag = (uint16_t)((60UL * rate_hz) / PP_STROKE_RATE_MIN_RPM);

    memset(&s_pipeline_graph, 0, sizeof(s_pipeline_graph));
    memset(s_source_params, 0, sizeof(s_source_params));

    write_u16_le(s_source_params, rate_hz);
    s_source_params[2] = 16;
    s_select_axis_params[0] = pipeline_axis_param();
    s_hpf_params[0] = 1;
    s_hpf_params[1] = 2;
    write_u16_le(&s_autocorr_params[0], min_lag);
    write_u16_le(&s_autocorr_params[2], max_lag);
    s_autocorr_params[4] = 0;
    s_autocorr_params[5] = PP_AUTOCORR_HARMONIC_PCT;
    write_u16_le(&s_kalman_params[0], 256);
    write_u16_le(&s_kalman_params[2], 256);
    write_u16_le(&s_kalman_params[4], 10000);
    s_kalman_params[6] = PP_KALMAN_MAX_JUMP_RPM;

    s_pipeline_graph.node_count = 5;
    s_pipeline_graph.edge_count = 4;

    s_pipeline_graph.nodes[0].block_id = pipeline_source_block_id();
    s_pipeline_graph.nodes[0].params = s_source_params;
    s_pipeline_graph.nodes[0].params_len = sizeof(s_source_params);
    s_pipeline_graph.nodes[1].block_id = PP_BLOCK_SELECT_AXIS;
    s_pipeline_graph.nodes[1].params = s_select_axis_params;
    s_pipeline_graph.nodes[1].params_len = sizeof(s_select_axis_params);
    s_pipeline_graph.nodes[2].block_id = PP_BLOCK_HPF_GRAVITY;
    s_pipeline_graph.nodes[2].params = s_hpf_params;
    s_pipeline_graph.nodes[2].params_len = sizeof(s_hpf_params);
    s_pipeline_graph.nodes[3].block_id = PP_BLOCK_AUTOCORRELATION;
    s_pipeline_graph.nodes[3].params = s_autocorr_params;
    s_pipeline_graph.nodes[3].params_len = sizeof(s_autocorr_params);
    s_pipeline_graph.nodes[4].block_id = PP_BLOCK_KALMAN_2D;
    s_pipeline_graph.nodes[4].params = s_kalman_params;
    s_pipeline_graph.nodes[4].params_len = sizeof(s_kalman_params);

    s_pipeline_graph.edges[0] = (pp_edge_t){0, 0, 1, 0};
    s_pipeline_graph.edges[1] = (pp_edge_t){1, 0, 2, 0};
    s_pipeline_graph.edges[2] = (pp_edge_t){2, 0, 3, 0};
    s_pipeline_graph.edges[3] = (pp_edge_t){3, 0, 4, 0};

    pipeline_assign_node_states(&s_pipeline_graph);
    if (pp_graph_validate_ports(&s_pipeline_graph) != PP_OK) return 0;
    if (pp_graph_topo_sort(&s_pipeline_graph) != PP_OK) return 0;
    return 1;
}

void paddling_pulse_pipeline_init(void)
{
    uint16_t binary_len = 0;

    s_pipeline_ready = 0;
    s_pipeline_rpm = 0;

    if (pp_storage_load_pipeline(s_pipeline_binary, sizeof(s_pipeline_binary), &binary_len))
    {
        if (pp_graph_build_from_binary(s_pipeline_binary, binary_len, &s_pipeline_graph) == PP_OK &&
            pp_graph_validate_ports(&s_pipeline_graph) == PP_OK &&
            pp_graph_topo_sort(&s_pipeline_graph) == PP_OK)
        {
            pipeline_assign_node_states(&s_pipeline_graph);
            s_pipeline_ready = 1;
            return;
        }
    }

    s_pipeline_ready = pipeline_build_default_graph();
}

static uint8_t pipeline_current_rpm(void)
{
    return s_pipeline_rpm;
}

static void imu_process_timer_cb(void)
{
    app_imu_process_timer_used = EASY_TIMER_INVALID_TIMER;
    if (!csc_meas_ntf_enabled) return;

    pp_imu_process();
    app_imu_process_timer_used = app_easy_timer(APP_IMU_PROCESS_TO, imu_process_timer_cb);
}

static void stroke_rate_timer_cb(void)
{
    app_stroke_rate_timer_used = EASY_TIMER_INVALID_TIMER;
    if (!csc_meas_ntf_enabled) return;

    if (s_pipeline_ready && pp_graph_execute(&s_pipeline_graph) == PP_OK)
    {
        uint8_t last_index = s_pipeline_graph.exec_order[s_pipeline_graph.node_count - 1];
        pp_packet_t *packet = &s_pipeline_graph.nodes[last_index].output;
        if (packet->length > 0 && packet->data && (packet->kind == PP_KIND_ESTIMATE || packet->kind == PP_KIND_CANDIDATE))
        {
            int16_t rpm = packet->data[0];
            if (rpm < 0) rpm = 0;
            if (rpm > 255) rpm = 255;
            s_pipeline_rpm = (uint8_t)rpm;
        }
    }

#ifdef CFG_PADDLING_PULSE_CONSOLE_MODE
    paddling_pulse_console_printf("SR: rpm=%u samples=%u\r\n",
                                  s_pipeline_rpm,
                                  pp_sample_store_get_count());
#endif

    app_stroke_rate_timer_used = app_easy_timer(APP_STROKE_RATE_TO, stroke_rate_timer_cb);
}

static void csc_meas_timer_cb(void)
{
    app_csc_meas_timer_used = EASY_TIMER_INVALID_TIMER;
    if (!csc_meas_ntf_enabled) return;

    if (app_connection_idx != GAP_INVALID_CONIDX)
    {
        uint8_t rpm = pipeline_current_rpm();
        uint8_t cadence = (rpm > 0) ? rpm : current_cadence_rpm;

        uint16_t drev, dticks;
        pp_cadence_to_crank(cadence, &drev, &dticks);

        csc_meas_state.flags              = CSCP_MEAS_CRANK_REV_DATA_PRESENT;
        csc_meas_state.cumul_crank_rev   += drev;
        csc_meas_state.last_crank_evt_time += dticks;
        csc_meas_state.cumul_wheel_rev    = 0;
        csc_meas_state.last_wheel_evt_time = 0;

        app_cscps_ntf_csc_meas_req(app_connection_idx, &csc_meas_state);

#ifdef CFG_PADDLING_PULSE_CONSOLE_MODE
        paddling_pulse_console_printf("CSC: rpm=%u rev=%u time=%u\r\n",
                                      cadence, drev, dticks);
#endif
    }

    app_csc_meas_timer_used = app_easy_timer(APP_CSC_MEAS_NTF_TO, csc_meas_timer_cb);
}

static void pipeline_start(void)
{
#ifdef CFG_IMU_POLAR
    pp_sample_store_init(52);
#else
    pp_sample_store_init(100);
#endif
    pp_stroke_rate_init();
    paddling_pulse_pipeline_init();

    imu_active = pp_imu_init();
    if (imu_active)
    {
        pp_imu_start();
    }
#ifdef CFG_PADDLING_PULSE_CONSOLE_MODE
    else
    {
        paddling_pulse_console_printf("IMU: init failed, using manual cadence\r\n");
    }
#endif

    app_imu_process_timer_used   = app_easy_timer(APP_IMU_PROCESS_TO, imu_process_timer_cb);
    app_stroke_rate_timer_used   = app_easy_timer(APP_STROKE_RATE_TO, stroke_rate_timer_cb);
    app_csc_meas_timer_used      = app_easy_timer(APP_CSC_MEAS_NTF_TO, csc_meas_timer_cb);
}

static void pipeline_stop(void)
{
    if (app_imu_process_timer_used != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(app_imu_process_timer_used);
        app_imu_process_timer_used = EASY_TIMER_INVALID_TIMER;
    }
    if (app_stroke_rate_timer_used != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(app_stroke_rate_timer_used);
        app_stroke_rate_timer_used = EASY_TIMER_INVALID_TIMER;
    }
    if (app_csc_meas_timer_used != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(app_csc_meas_timer_used);
        app_csc_meas_timer_used = EASY_TIMER_INVALID_TIMER;
    }

    if (imu_active)
    {
        pp_imu_stop();
        imu_active = false;
    }
    pp_sample_store_reset();
}

void user_app_init(void)
{
    app_connection_idx = GAP_INVALID_CONIDX;
    app_csc_meas_timer_used = EASY_TIMER_INVALID_TIMER;
    app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;
    app_imu_process_timer_used = EASY_TIMER_INVALID_TIMER;
    app_stroke_rate_timer_used = EASY_TIMER_INVALID_TIMER;
    csc_meas_ntf_enabled = false;
    imu_active = false;
    current_cadence_rpm = APP_CSCP_DEFAULT_CADENCE_RPM;
    memset(&csc_meas_state, 0, sizeof(csc_meas_state));
    
    // Initialize Manufacturer Specific Data
    mnf_data_init();
    
    // Initialize Advertising and Scan Response Data
    memcpy(stored_adv_data, USER_ADVERTISE_DATA, USER_ADVERTISE_DATA_LEN);
    stored_adv_data_len = USER_ADVERTISE_DATA_LEN;
    memcpy(stored_scan_rsp_data, USER_ADVERTISE_SCAN_RESPONSE_DATA, USER_ADVERTISE_SCAN_RESPONSE_DATA_LEN);
    stored_scan_rsp_data_len = USER_ADVERTISE_SCAN_RESPONSE_DATA_LEN;
    
    default_app_on_init();
    paddling_pulse_console_init();
    pp_pipeline_service_init();
    paddling_pulse_pipeline_init();
}

void user_app_on_get_dev_appearance(uint16_t *appearance)
{
    *appearance = APP_CSCP_DEVICE_APPEARANCE;
}

void user_app_adv_start(void)
{
    // Schedule the next advertising data update
    app_adv_data_update_timer_used = app_easy_timer(APP_ADV_DATA_UPDATE_TO, adv_data_update_timer_cb);
    
    struct gapm_start_advertise_cmd* cmd;
    cmd = app_easy_gap_undirected_advertise_get_active();
    
    // Add manufacturer data to initial advertising or scan response data, if there is enough space
    app_add_ad_struct(cmd, &mnf_data, sizeof(struct mnf_specific_data_ad_structure), 1);

    app_easy_gap_undirected_advertise_start();
}

void user_app_connection(const uint8_t conidx, struct gapc_connection_req_ind const *param)
{
#ifdef CFG_IMU_POLAR
    /* Check if this is the Polar central-role connection */
    if (pp_imu_polar_on_connection(conidx, param))
    {
        return;
    }
#endif

    if (app_env[conidx].conidx != GAP_INVALID_CONIDX)
    {
        app_connection_idx = conidx;

#ifdef CFG_PADDLING_PULSE_CONSOLE_MODE
        ble_log_peer("connected", conidx, param->conhdl,
                     param->peer_addr_type, &param->peer_addr);
#endif

        // Stop the advertising data update timer
        app_easy_timer_cancel(app_adv_data_update_timer_used);

        // Check if the parameters of the established connection are the preferred ones.
        // If not then schedule a connection parameter update request.
        if ((param->con_interval < user_connection_param_conf.intv_min) ||
            (param->con_interval > user_connection_param_conf.intv_max) ||
            (param->con_latency != user_connection_param_conf.latency) ||
            (param->sup_to != user_connection_param_conf.time_out))
        {
            // Connection params are not these that we expect
            app_param_update_request_timer_used = app_easy_timer(APP_PARAM_UPDATE_REQUEST_TO, param_update_request_timer_cb);
        }
    }
    else
    {
        // No connection has been established, restart advertising
        user_app_adv_start();
    }

    default_app_on_connection(conidx, param);
}

void user_app_adv_undirect_complete(uint8_t status)
{
    // If advertising was canceled then update advertising data and start advertising again
    if (status == GAP_ERR_CANCELED)
    {
        user_app_adv_start();
    }
}

void user_app_disconnect(struct gapc_disconnect_ind const *param)
{
#ifdef CFG_IMU_POLAR
    if (pp_imu_polar_on_disconnect(param->conhdl))
    {
        /* Polar connection dropped — driver handles retry internally */
        return;
    }
#endif

    app_connection_idx = GAP_INVALID_CONIDX;

#ifdef CFG_PADDLING_PULSE_CONSOLE_MODE
    paddling_pulse_console_printf("BLE: disconnect h=%u\r\n", param->conhdl);
#endif

    csc_meas_ntf_enabled = false;

    pipeline_stop();

    if (app_param_update_request_timer_used != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(app_param_update_request_timer_used);
        app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;
    }

    mnf_data_update();
    user_app_adv_start();
}

void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id)
{
    if (paddling_pulse_console_handle_message(msgid, param))
    {
        return;
    }

#ifdef CFG_IMU_POLAR
    if (pp_imu_polar_handle_message(msgid, param, dest_id, src_id))
    {
        return;
    }
#endif

    switch(msgid)
    {
        case GAPC_PARAM_UPDATED_IND:
        {
            (void)param;
        } break;

        case GATTC_EVENT_REQ_IND:
        {
            // Confirm unhandled indication to avoid GATT timeout
            struct gattc_event_ind const *ind = (struct gattc_event_ind const *) param;
            struct gattc_event_cfm *cfm = KE_MSG_ALLOC(GATTC_EVENT_CFM, src_id, dest_id, gattc_event_cfm);
            cfm->handle = ind->handle;
            KE_MSG_SEND(cfm);
        } break;

        default:
            break;
    }
}
void user_on_cscps_cfg_ntfind_ind(uint8_t conidx, const struct cscps_cfg_ntfind_ind *param)
{
    if (param->char_code == CSCP_CSCS_CSC_MEAS_CHAR)
    {
        if (param->ntf_cfg == PRF_CLI_START_NTF)
        {
#ifdef CFG_PADDLING_PULSE_CONSOLE_MODE
            ble_log_cfg("csc ntf on", conidx);
#endif
            csc_meas_ntf_enabled = true;
            pipeline_start();
        }
        else
        {
#ifdef CFG_PADDLING_PULSE_CONSOLE_MODE
            ble_log_cfg("csc ntf off", conidx);
#endif
            csc_meas_ntf_enabled = false;
            pipeline_stop();
        }
    }

    app_cscps_cfg_ntfind_ind(conidx, param);
}

/// @} APP
