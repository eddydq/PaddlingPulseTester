/**
 ****************************************************************************************
 * @file paddling_pulse_imu_polar.c
 * @brief Polar Verity Sense BLE central client — PMD accelerometer at 52 Hz.
 ****************************************************************************************
 */

#include "da14531_config_basic.h"

#ifdef CFG_IMU_POLAR

#include "paddling_pulse_imu_polar.h"
#include "paddling_pulse_sample_store.h"
#include "rwip_config.h"
#include "gap.h"
#include "gapc_task.h"
#include "gapm_task.h"
#include "gattc_task.h"
#include "co_bt.h"
#include "ke_msg.h"
#include "ke_task.h"
#include "app_easy_timer.h"
#include "app.h"
#include "arch.h"
#include "paddling_pulse_console_io.h"
#include "user_config.h"
#include <string.h>

/*
 * CONSTANTS
 */

/* PMD service and characteristic UUIDs (128-bit, little-endian) */
static const uint8_t polar_pmd_svc_uuid[ATT_UUID_128_LEN] = {
    0xFB, 0x00, 0x5C, 0x80, 0x02, 0xE7, 0xF3, 0x87,
    0x1C, 0xAD, 0x8A, 0xCD, 0x2D, 0x8D, 0xF0, 0xC8
};
static const uint8_t polar_pmd_cp_uuid[ATT_UUID_128_LEN] = {
    0xFB, 0x00, 0x5C, 0x81, 0x02, 0xE7, 0xF3, 0x87,
    0x1C, 0xAD, 0x8A, 0xCD, 0x2D, 0x8D, 0xF0, 0xC8
};
static const uint8_t polar_pmd_data_uuid[ATT_UUID_128_LEN] = {
    0xFB, 0x00, 0x5C, 0x82, 0x02, 0xE7, 0xF3, 0x87,
    0x1C, 0xAD, 0x8A, 0xCD, 0x2D, 0x8D, 0xF0, 0xC8
};

#define POLAR_SCAN_INTV             10
#define POLAR_CONNECT_TIMEOUT       800     /* 8 s in 10ms ticks */
#define POLAR_PMD_DATA_HDR_LEN      10
#define POLAR_CP_RSP_MAX_LEN        96
#define POLAR_ACC_TLV_MAX_LEN       24

#define POLAR_PMD_OP_GET_SETTINGS   0x01
#define POLAR_PMD_OP_START_MEAS     0x02
#define POLAR_PMD_MEAS_ACC          0x02
#define POLAR_PMD_CP_RSP_CODE       0xF0
#define POLAR_PMD_STATUS_SUCCESS    0x00

enum polar_seq {
    POLAR_SEQ_CP_CCCD       = 0x5101,
    POLAR_SEQ_DATA_CCCD     = 0x5102,
    POLAR_SEQ_GET_ACC_SET   = 0x5105,
    POLAR_SEQ_START_ACC     = 0x5106,
};

enum polar_setting_type {
    POLAR_SET_SAMPLE_RATE   = 0x00,
    POLAR_SET_RESOLUTION    = 0x01,
    POLAR_SET_RANGE         = 0x02,
    POLAR_SET_CHANNELS      = 0x04,
};

/*
 * STATE MACHINE
 */
enum polar_state {
    POLAR_IDLE = 0,
    POLAR_SCANNING,
    POLAR_CONNECTING,
    POLAR_WAIT_MTU,
    POLAR_DISC_SVC,
    POLAR_DISC_CHARS,
    POLAR_DISC_CP_CCCD,
    POLAR_DISC_DATA_CCCD,
    POLAR_ENABLE_CP_NTF,
    POLAR_ENABLE_DATA_NTF,
    POLAR_GET_ACC_SETTINGS,
    POLAR_START_ACC,
    POLAR_STREAMING,
};

/*
 * ENVIRONMENT
 */
static struct {
    uint8_t  state;
    uint8_t  conidx;
    uint16_t conhdl;
    uint16_t mtu;
    uint8_t  target_addr_type;
    struct bd_addr target_addr;

    uint16_t svc_start, svc_end;
    uint16_t cp_decl_handle,   cp_handle,   cp_cccd_handle;
    uint16_t data_decl_handle, data_handle, data_cccd_handle;

    uint8_t  cp_rsp_params[POLAR_CP_RSP_MAX_LEN];
    uint16_t cp_rsp_len;

    uint16_t acc_sample_rate_hz;
    uint8_t  acc_selected_tlvs[POLAR_ACC_TLV_MAX_LEN];
    uint8_t  acc_selected_tlvs_len;
} s_polar __SECTION_ZERO("retention_mem_area0");

static timer_hnd s_connect_timer __SECTION_ZERO("retention_mem_area0");

static const char *polar_state_name(uint8_t state)
{
    switch (state)
    {
    case POLAR_IDLE:             return "IDLE";
    case POLAR_SCANNING:         return "SCANNING";
    case POLAR_CONNECTING:       return "CONNECTING";
    case POLAR_WAIT_MTU:         return "WAIT_MTU";
    case POLAR_DISC_SVC:         return "DISC_SVC";
    case POLAR_DISC_CHARS:       return "DISC_CHARS";
    case POLAR_DISC_CP_CCCD:     return "DISC_CP_CCCD";
    case POLAR_DISC_DATA_CCCD:   return "DISC_DATA_CCCD";
    case POLAR_ENABLE_CP_NTF:    return "ENABLE_CP_NTF";
    case POLAR_ENABLE_DATA_NTF:  return "ENABLE_DATA_NTF";
    case POLAR_GET_ACC_SETTINGS: return "GET_ACC_SETTINGS";
    case POLAR_START_ACC:        return "START_ACC";
    case POLAR_STREAMING:        return "STREAMING";
    default:                     return "UNKNOWN";
    }
}

#if defined(CFG_PADDLING_PULSE_CONSOLE_MODE)
static void polar_log(const char *message)
{
    paddling_pulse_console_printf("POLAR: %s\r\n", message);
}

static void polar_log_status(const char *message, uint16_t status)
{
    paddling_pulse_console_printf("POLAR: %s st=%u\r\n", message, status);
}

static void polar_log_conn(const char *message, uint8_t conidx, uint16_t conhdl)
{
    paddling_pulse_console_printf("POLAR: %s idx=%u h=%u\r\n", message, conidx, conhdl);
}

static void polar_log_handle(const char *name, uint16_t handle)
{
    paddling_pulse_console_printf("POLAR: %s=%04X\r\n", name, handle);
}

static void polar_log_range(const char *name, uint16_t start_hdl, uint16_t end_hdl)
{
    paddling_pulse_console_printf("POLAR: %s=%04X-%04X\r\n", name, start_hdl, end_hdl);
}

static void polar_log_addr(const char *message, uint8_t addr_type,
                           const struct bd_addr *addr)
{
    paddling_pulse_console_printf("POLAR: %s type=%u addr=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                                  message,
                                  addr_type,
                                  addr->addr[5], addr->addr[4], addr->addr[3],
                                  addr->addr[2], addr->addr[1], addr->addr[0]);
}

static void polar_log_state_change(uint8_t from_state, uint8_t to_state,
                                   const char *step)
{
    paddling_pulse_console_printf("POLAR: %s->%s %s\r\n",
                                  polar_state_name(from_state),
                                  polar_state_name(to_state),
                                  step);
}

static void polar_log_acc_cfg(uint16_t sample_rate_hz, uint8_t tlv_count)
{
    paddling_pulse_console_printf("POLAR: acc cfg rate=%uHz tlvs=%u\r\n", sample_rate_hz, tlv_count);
}
#else
static void polar_log(const char *message) { (void)message; }
static void polar_log_status(const char *message, uint16_t status)
{
    (void)message;
    (void)status;
}
static void polar_log_conn(const char *message, uint8_t conidx, uint16_t conhdl)
{
    (void)message;
    (void)conidx;
    (void)conhdl;
}
static void polar_log_handle(const char *name, uint16_t handle)
{
    (void)name;
    (void)handle;
}
static void polar_log_range(const char *name, uint16_t start_hdl, uint16_t end_hdl)
{
    (void)name;
    (void)start_hdl;
    (void)end_hdl;
}
static void polar_log_addr(const char *message, uint8_t addr_type,
                           const struct bd_addr *addr)
{
    (void)message;
    (void)addr_type;
    (void)addr;
}
static void polar_log_state_change(uint8_t from_state, uint8_t to_state,
                                   const char *step)
{
    (void)from_state;
    (void)to_state;
    (void)step;
}
static void polar_log_acc_cfg(uint16_t sample_rate_hz, uint8_t tlv_count)
{
    (void)sample_rate_hz;
    (void)tlv_count;
}
#endif

/*
 * FORWARD DECLARATIONS
 */
static void polar_clear_handles(void);
static void polar_reset(void);
static void polar_retry_if_needed(void);

/*
 * GATT HELPERS
 */
static void polar_gatt_discover_svc(uint8_t conidx)
{
    struct gattc_disc_cmd *cmd = KE_MSG_ALLOC_DYN(
        GATTC_DISC_CMD, KE_BUILD_ID(TASK_GATTC, conidx), TASK_APP,
        gattc_disc_cmd, ATT_UUID_128_LEN);
    cmd->operation  = GATTC_DISC_BY_UUID_SVC;
    cmd->start_hdl  = 0x0001;
    cmd->end_hdl    = 0xFFFF;
    cmd->uuid_len   = ATT_UUID_128_LEN;
    memcpy(cmd->uuid, polar_pmd_svc_uuid, ATT_UUID_128_LEN);
    KE_MSG_SEND(cmd);
}

static void polar_gatt_discover_chars(uint8_t conidx)
{
    struct gattc_disc_cmd *cmd = KE_MSG_ALLOC_DYN(
        GATTC_DISC_CMD, KE_BUILD_ID(TASK_GATTC, conidx), TASK_APP,
        gattc_disc_cmd, 0);
    cmd->operation  = GATTC_DISC_ALL_CHAR;
    cmd->start_hdl  = s_polar.svc_start;
    cmd->end_hdl    = s_polar.svc_end;
    cmd->uuid_len   = 0;
    KE_MSG_SEND(cmd);
}

static void polar_gatt_discover_desc(uint8_t conidx,
                                     uint16_t start_hdl, uint16_t end_hdl)
{
    if (start_hdl > end_hdl) return;
    struct gattc_disc_cmd *cmd = KE_MSG_ALLOC_DYN(
        GATTC_DISC_CMD, KE_BUILD_ID(TASK_GATTC, conidx), TASK_APP,
        gattc_disc_cmd, 0);
    cmd->operation  = GATTC_DISC_DESC_CHAR;
    cmd->start_hdl  = start_hdl;
    cmd->end_hdl    = end_hdl;
    cmd->uuid_len   = 0;
    KE_MSG_SEND(cmd);
}

static void polar_gatt_write(uint8_t conidx, uint16_t handle,
                             const uint8_t *value, uint16_t length,
                             uint8_t operation, uint16_t seq_num)
{
    struct gattc_write_cmd *cmd = KE_MSG_ALLOC_DYN(
        GATTC_WRITE_CMD, KE_BUILD_ID(TASK_GATTC, conidx), TASK_APP,
        gattc_write_cmd, length);
    cmd->operation    = operation;
    cmd->auto_execute = true;
    cmd->seq_num      = seq_num;
    cmd->handle       = handle;
    cmd->offset       = 0;
    cmd->length       = length;
    cmd->cursor        = 0;
    memcpy(cmd->value, value, length);
    KE_MSG_SEND(cmd);
}

static void polar_send_event_cfm(uint8_t conidx, uint16_t handle)
{
    struct gattc_event_cfm *cfm = KE_MSG_ALLOC(
        GATTC_EVENT_CFM,
        KE_BUILD_ID(TASK_GATTC, conidx), TASK_APP,
        gattc_event_cfm);
    cfm->handle = handle;
    KE_MSG_SEND(cfm);
}

static void polar_send_cp_cmd(uint8_t opcode, uint8_t meas_type,
                              const uint8_t *payload, uint8_t payload_len,
                              uint16_t seq_num)
{
    uint8_t buf[2 + POLAR_ACC_TLV_MAX_LEN];
    buf[0] = opcode;
    buf[1] = meas_type;
    if (payload_len > 0 && payload != NULL)
        memcpy(&buf[2], payload, payload_len);
    polar_gatt_write(s_polar.conidx, s_polar.cp_handle,
                     buf, 2 + payload_len, GATTC_WRITE, seq_num);
}

/*
 * NAME MATCHING
 */
static bool polar_is_verity_sense(const uint8_t *data, uint16_t length)
{
    /* Extract complete/shortened local name from AD data */
    uint16_t pos = 0;
    while (pos < length)
    {
        uint8_t ad_len = data[pos];
        if (ad_len == 0 || pos + ad_len >= length) break;

        uint8_t ad_type = data[pos + 1];
        if (ad_type == GAP_AD_TYPE_COMPLETE_NAME ||
            ad_type == GAP_AD_TYPE_SHORTENED_NAME)
        {
            uint8_t name_len = ad_len - 1;
            const char *name = (const char *)&data[pos + 2];
            /* Check for "Polar" prefix and "Sense" substring */
            if (name_len >= 6 && memcmp(name, "Polar ", 6) == 0)
            {
                uint8_t i;
                for (i = 6; i + 5 <= name_len; i++)
                {
                    if (memcmp(&name[i], "Sense", 5) == 0)
                        return true;
                }
            }
        }
        pos += ad_len + 1;
    }
    return false;
}

/*
 * SCAN AND CONNECT
 */
static void polar_start_scan(void)
{
    if (s_polar.state != POLAR_IDLE) return;

    polar_clear_handles();
    polar_log_state_change(s_polar.state, POLAR_SCANNING, "scan start");
    s_polar.state = POLAR_SCANNING;

    struct gapm_start_scan_cmd *cmd = KE_MSG_ALLOC(
        GAPM_START_SCAN_CMD, TASK_GAPM, TASK_APP, gapm_start_scan_cmd);
    cmd->op.code     = GAPM_SCAN_ACTIVE;
    cmd->op.addr_src = APP_CFG_ADDR_SRC(USER_CFG_ADDRESS_MODE);
    cmd->interval    = user_central_conf.scan_interval;
    cmd->window      = user_central_conf.scan_window;
    cmd->mode        = GAP_GEN_DISCOVERY;
    cmd->filt_policy = SCAN_ALLOW_ADV_ALL;
    cmd->filter_duplic = SCAN_FILT_DUPLIC_EN;
    KE_MSG_SEND(cmd);
}

static void polar_connect_timeout_cb(void)
{
    s_connect_timer = EASY_TIMER_INVALID_TIMER;
    if (s_polar.state == POLAR_CONNECTING)
    {
        polar_log("connect timeout");
        struct gapm_cancel_cmd *cmd = KE_MSG_ALLOC(
            GAPM_CANCEL_CMD, TASK_GAPM, TASK_APP, gapm_cancel_cmd);
        cmd->operation = GAPM_CANCEL;
        KE_MSG_SEND(cmd);
    }
}

static void polar_start_connection(void)
{
    if (s_polar.state != POLAR_CONNECTING) return;

    polar_log_addr("connect start", s_polar.target_addr_type, &s_polar.target_addr);

    struct gapm_start_connection_cmd *cmd = KE_MSG_ALLOC_DYN(
        GAPM_START_CONNECTION_CMD, TASK_GAPM, TASK_APP,
        gapm_start_connection_cmd, sizeof(struct gap_bdaddr));
    cmd->op.code     = GAPM_CONNECTION_DIRECT;
    cmd->op.addr_src = APP_CFG_ADDR_SRC(USER_CFG_ADDRESS_MODE);
    cmd->scan_interval = POLAR_SCAN_INTV;
    cmd->scan_window   = POLAR_SCAN_INTV;
    cmd->con_intv_min  = user_central_conf.con_intv_min;
    cmd->con_intv_max  = user_central_conf.con_intv_max;
    cmd->con_latency   = user_central_conf.con_latency;
    cmd->superv_to     = user_central_conf.superv_to;
    cmd->ce_len_min    = user_central_conf.ce_len_min;
    cmd->ce_len_max    = user_central_conf.ce_len_max;
    cmd->nb_peers      = 1;
    memcpy(&cmd->peers[0].addr, &s_polar.target_addr, BD_ADDR_LEN);
    cmd->peers[0].addr_type = s_polar.target_addr_type;
    KE_MSG_SEND(cmd);

    s_connect_timer = app_easy_timer(POLAR_CONNECT_TIMEOUT, polar_connect_timeout_cb);
}

static void polar_deferred_connect_cb(void)
{
    polar_log("connect defer");
    polar_start_connection();
}

/*
 * SETTINGS PARSING
 */
static void polar_parse_acc_settings(const uint8_t *data, uint16_t len)
{
    /* Build TLV payload selecting preferred settings */
    s_polar.acc_selected_tlvs_len = 0;
    s_polar.acc_sample_rate_hz    = 52;   /* default */

    uint16_t pos = 0;
    while (pos + 2 < len)
    {
        uint8_t type      = data[pos];
        uint8_t val_count = data[pos + 1];
        pos += 2;

        if (pos + (uint16_t)val_count * 2 > len) break;

        /* Pick preferred value */
        uint16_t selected = 0;
        uint8_t i;
        switch (type)
        {
        case POLAR_SET_SAMPLE_RATE:
            /* Prefer 52 Hz */
            selected = 52;
            for (i = 0; i < val_count; i++)
            {
                uint16_t v = (uint16_t)data[pos + i * 2] |
                             ((uint16_t)data[pos + i * 2 + 1] << 8);
                if (v == 52) { selected = v; break; }
            }
            s_polar.acc_sample_rate_hz = selected;
            break;
        case POLAR_SET_RESOLUTION:
            selected = 16;  /* 16-bit preferred */
            break;
        case POLAR_SET_RANGE:
            selected = 8;   /* 8g preferred */
            break;
        case POLAR_SET_CHANNELS:
            selected = 3;   /* XYZ */
            break;
        default:
            /* Take first available */
            if (val_count > 0)
                selected = (uint16_t)data[pos] |
                           ((uint16_t)data[pos + 1] << 8);
            break;
        }

        /* Append TLV: [type, count=1, lo, hi] */
        if (s_polar.acc_selected_tlvs_len + 4 <= POLAR_ACC_TLV_MAX_LEN)
        {
            s_polar.acc_selected_tlvs[s_polar.acc_selected_tlvs_len++] = type;
            s_polar.acc_selected_tlvs[s_polar.acc_selected_tlvs_len++] = 1;
            s_polar.acc_selected_tlvs[s_polar.acc_selected_tlvs_len++] =
                (uint8_t)(selected & 0xFF);
            s_polar.acc_selected_tlvs[s_polar.acc_selected_tlvs_len++] =
                (uint8_t)(selected >> 8);
        }

        pos += (uint16_t)val_count * 2;
    }
}

/*
 * PMD DATA PARSING
 */
static int32_t polar_parse_signed_le(const uint8_t *data, uint8_t size)
{
    int32_t val = 0;
    uint8_t i;
    for (i = 0; i < size; i++)
        val |= (int32_t)data[i] << (8 * i);
    /* Sign extend */
    if (size < 4 && (val & (1 << (8 * size - 1))))
        val |= ~((1 << (8 * size)) - 1);
    return val;
}

static int32_t polar_parse_signed_bits(const uint8_t *data,
                                       uint32_t bit_offset, uint8_t bit_width)
{
    int32_t val = 0;
    uint8_t i;
    for (i = 0; i < bit_width; i++)
    {
        uint32_t byte_idx = (bit_offset + i) / 8;
        uint8_t  bit_idx  = (bit_offset + i) % 8;
        if (data[byte_idx] & (1 << bit_idx))
            val |= (1 << i);
    }
    /* Sign extend */
    if (bit_width < 32 && (val & (1 << (bit_width - 1))))
        val |= ~((1 << bit_width) - 1);
    return val;
}

static void polar_store_sample(int32_t x, int32_t y, int32_t z)
{
    int16_t sample;
#if defined(CFG_IMU_AXIS_X)
    sample = (x > 32767) ? 32767 : (x < -32768) ? -32768 : (int16_t)x;
#elif defined(CFG_IMU_AXIS_Y)
    sample = (y > 32767) ? 32767 : (y < -32768) ? -32768 : (int16_t)y;
#elif defined(CFG_IMU_AXIS_Z)
    sample = (z > 32767) ? 32767 : (z < -32768) ? -32768 : (int16_t)z;
#endif
    pp_sample_store_push(sample);
}

static void polar_parse_raw_frame(uint8_t frame_type,
                                  const uint8_t *data, uint16_t len)
{
    uint8_t bytes_per_val = frame_type + 1;  /* 0->1, 1->2, 2->3 */
    uint8_t bytes_per_sample = bytes_per_val * 3;
    uint16_t sample_count = len / bytes_per_sample;
    uint16_t i;

    for (i = 0; i < sample_count; i++)
    {
        const uint8_t *p = data + i * bytes_per_sample;
        int32_t x = polar_parse_signed_le(p, bytes_per_val);
        int32_t y = polar_parse_signed_le(p + bytes_per_val, bytes_per_val);
        int32_t z = polar_parse_signed_le(p + 2 * bytes_per_val, bytes_per_val);
        polar_store_sample(x, y, z);
    }
}

static void polar_parse_compressed_frame(const uint8_t *data, uint16_t len)
{
    if (len < 6) return;

    /* Initial absolute sample (3 x int16 LE) */
    int32_t x = (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    int32_t y = (int16_t)((uint16_t)data[2] | ((uint16_t)data[3] << 8));
    int32_t z = (int16_t)((uint16_t)data[4] | ((uint16_t)data[5] << 8));
    polar_store_sample(x, y, z);

    uint16_t pos = 6;
    while (pos + 2 <= len)
    {
        uint8_t  delta_size   = data[pos];      /* bits per delta per axis */
        uint8_t  block_count  = data[pos + 1];  /* samples in this block */
        pos += 2;

        uint8_t  bits_per_sample = delta_size * 3;
        uint32_t bit_offset = (uint32_t)pos * 8;
        uint32_t total_bits = (uint32_t)block_count * bits_per_sample;
        uint16_t byte_count = (uint16_t)((total_bits + 7) / 8);

        if (pos + byte_count > len) break;

        uint8_t s;
        for (s = 0; s < block_count; s++)
        {
            int32_t dx = polar_parse_signed_bits(data, bit_offset, delta_size);
            bit_offset += delta_size;
            int32_t dy = polar_parse_signed_bits(data, bit_offset, delta_size);
            bit_offset += delta_size;
            int32_t dz = polar_parse_signed_bits(data, bit_offset, delta_size);
            bit_offset += delta_size;

            x += dx;
            y += dy;
            z += dz;
            polar_store_sample(x, y, z);
        }

        pos += byte_count;
    }
}

static void polar_parse_acc_packet(const uint8_t *value, uint16_t length)
{
    if (length < POLAR_PMD_DATA_HDR_LEN + 1) return;
    if (value[0] != POLAR_PMD_MEAS_ACC) return;

    uint8_t frame_info  = value[9];
    bool    compressed  = (frame_info & 0x80) != 0;
    uint8_t frame_type  = frame_info & 0x7F;

    const uint8_t *payload = value + POLAR_PMD_DATA_HDR_LEN;
    uint16_t payload_len   = length - POLAR_PMD_DATA_HDR_LEN;

    if (compressed)
        polar_parse_compressed_frame(payload, payload_len);
    else
        polar_parse_raw_frame(frame_type, payload, payload_len);
}

/*
 * CP RESPONSE HANDLING
 */
static void polar_handle_cp_event(const uint8_t *value, uint16_t length)
{
    if (length < 4) return;
    if (value[0] != POLAR_PMD_CP_RSP_CODE) return;

    uint8_t opcode  = value[1];
    uint8_t status  = value[3];

    if (opcode == POLAR_PMD_OP_GET_SETTINGS && status == POLAR_PMD_STATUS_SUCCESS)
    {
        /* Parse settings from response params */
        if (length > 5)
        {
            polar_parse_acc_settings(value + 5, length - 5);
        }

        /* Start accelerometer measurement */
        polar_log_acc_cfg(s_polar.acc_sample_rate_hz, s_polar.acc_selected_tlvs_len / 4);
        polar_log_state_change(s_polar.state, POLAR_START_ACC, "acc start");
        s_polar.state = POLAR_START_ACC;
        polar_send_cp_cmd(POLAR_PMD_OP_START_MEAS, POLAR_PMD_MEAS_ACC,
                          s_polar.acc_selected_tlvs,
                          s_polar.acc_selected_tlvs_len,
                          POLAR_SEQ_START_ACC);
    }
    else if (opcode == POLAR_PMD_OP_START_MEAS && status == POLAR_PMD_STATUS_SUCCESS)
    {
        polar_log_state_change(s_polar.state, POLAR_STREAMING, "streaming");
        s_polar.state = POLAR_STREAMING;
    }
    else
    {
        paddling_pulse_console_printf("POLAR: cp rsp op=%u st=%u\r\n", opcode, status);
    }
}

/*
 * GATT EVENT HANDLER
 */
static bool polar_handle_gatt_event(uint8_t conidx, uint16_t handle,
                                    uint16_t length, const uint8_t *value,
                                    bool indication)
{
    if (s_polar.conidx != conidx) return false;

    if (handle == s_polar.cp_handle && indication)
    {
        polar_handle_cp_event(value, length);
        return true;
    }

    if (handle == s_polar.data_handle)
    {
        polar_parse_acc_packet(value, length);
        return true;
    }

    return false;
}

/*
 * DISCOVERY INDICATION HANDLERS
 */
static void polar_on_disc_svc(const struct gattc_disc_svc_ind *ind)
{
    if (ind->uuid_len == ATT_UUID_128_LEN &&
        memcmp(ind->uuid, polar_pmd_svc_uuid, ATT_UUID_128_LEN) == 0)
    {
        s_polar.svc_start = ind->start_hdl;
        s_polar.svc_end   = ind->end_hdl;
        polar_log_range("svc", s_polar.svc_start, s_polar.svc_end);
    }
}

static void polar_on_disc_char(const struct gattc_disc_char_ind *ind)
{
    if (ind->uuid_len != ATT_UUID_128_LEN) return;

    if (memcmp(ind->uuid, polar_pmd_cp_uuid, ATT_UUID_128_LEN) == 0)
    {
        s_polar.cp_decl_handle = ind->attr_hdl;
        s_polar.cp_handle      = ind->pointer_hdl;
        polar_log_handle("cp", s_polar.cp_handle);
    }
    else if (memcmp(ind->uuid, polar_pmd_data_uuid, ATT_UUID_128_LEN) == 0)
    {
        s_polar.data_decl_handle = ind->attr_hdl;
        s_polar.data_handle      = ind->pointer_hdl;
        polar_log_handle("data", s_polar.data_handle);
    }
}

static void polar_on_disc_desc(const struct gattc_disc_char_desc_ind *ind)
{
    /* Match CCCD (0x2902) */
    if (ind->uuid_len == ATT_UUID_16_LEN)
    {
        uint16_t uuid16 = (uint16_t)ind->uuid[0] | ((uint16_t)ind->uuid[1] << 8);
        if (uuid16 == ATT_DESC_CLIENT_CHAR_CFG)
        {
            if (s_polar.state == POLAR_DISC_CP_CCCD)
            {
                s_polar.cp_cccd_handle = ind->attr_hdl;
                polar_log_handle("cp_cccd", s_polar.cp_cccd_handle);
            }
            else if (s_polar.state == POLAR_DISC_DATA_CCCD)
            {
                s_polar.data_cccd_handle = ind->attr_hdl;
                polar_log_handle("data_cccd", s_polar.data_cccd_handle);
            }
        }
    }
}

/*
 * STATE MACHINE — GATTC_CMP_EVT
 */
static void polar_on_gattc_cmp(const struct gattc_cmp_evt *evt)
{
    if (evt->status != ATT_ERR_NO_ERROR && evt->status != GAP_ERR_NO_ERROR)
    {
        /* Discovery/write failed — disconnect and retry */
        paddling_pulse_console_printf("POLAR: gatt fail op=%u st=%u\r\n", evt->operation, evt->status);
        struct gapc_disconnect_cmd *cmd = KE_MSG_ALLOC(
            GAPC_DISCONNECT_CMD,
            KE_BUILD_ID(TASK_GAPC, s_polar.conidx), TASK_APP,
            gapc_disconnect_cmd);
        cmd->operation = GAPC_DISCONNECT;
        cmd->reason    = CO_ERROR_REMOTE_USER_TERM_CON;
        KE_MSG_SEND(cmd);
        return;
    }

    switch (s_polar.state)
    {
    case POLAR_WAIT_MTU:
        if (evt->operation == GATTC_MTU_EXCH)
        {
            polar_log_state_change(s_polar.state, POLAR_DISC_SVC, "svc disc");
            s_polar.state = POLAR_DISC_SVC;
            polar_gatt_discover_svc(s_polar.conidx);
        }
        break;

    case POLAR_DISC_SVC:
        if (evt->operation == GATTC_DISC_BY_UUID_SVC)
        {
            if (s_polar.svc_start == 0)
            {
                polar_log("svc missing");
                break;  /* service not found */
            }
            polar_log_state_change(s_polar.state, POLAR_DISC_CHARS, "chars disc");
            s_polar.state = POLAR_DISC_CHARS;
            polar_gatt_discover_chars(s_polar.conidx);
        }
        break;

    case POLAR_DISC_CHARS:
        if (evt->operation == GATTC_DISC_ALL_CHAR)
        {
            if (s_polar.cp_handle == 0 || s_polar.data_handle == 0)
            {
                polar_log("chars missing");
                break;
            }
            polar_log_state_change(s_polar.state, POLAR_DISC_CP_CCCD, "cp cccd disc");
            s_polar.state = POLAR_DISC_CP_CCCD;
            polar_gatt_discover_desc(s_polar.conidx,
                                     s_polar.cp_handle + 1,
                                     s_polar.data_decl_handle - 1);
        }
        break;

    case POLAR_DISC_CP_CCCD:
        if (evt->operation == GATTC_DISC_DESC_CHAR)
        {
            if (s_polar.cp_cccd_handle == 0)
            {
                polar_log("cp cccd missing");
                break;
            }
            polar_log_state_change(s_polar.state, POLAR_DISC_DATA_CCCD, "data cccd disc");
            s_polar.state = POLAR_DISC_DATA_CCCD;
            polar_gatt_discover_desc(s_polar.conidx,
                                     s_polar.data_handle + 1,
                                     s_polar.svc_end);
        }
        break;

    case POLAR_DISC_DATA_CCCD:
        if (evt->operation == GATTC_DISC_DESC_CHAR)
        {
            if (s_polar.data_cccd_handle == 0)
            {
                polar_log("data cccd missing");
                break;
            }
            polar_log_state_change(s_polar.state, POLAR_ENABLE_CP_NTF, "cp ind req");
            s_polar.state = POLAR_ENABLE_CP_NTF;
            uint8_t cccd_val[2] = { 0x02, 0x00 };  /* indications */
            polar_gatt_write(s_polar.conidx, s_polar.cp_cccd_handle,
                             cccd_val, 2, GATTC_WRITE, POLAR_SEQ_CP_CCCD);
        }
        break;

    case POLAR_ENABLE_CP_NTF:
        if (evt->operation == GATTC_WRITE && evt->seq_num == POLAR_SEQ_CP_CCCD)
        {
            polar_log("cp ind on");
            polar_log_state_change(s_polar.state, POLAR_ENABLE_DATA_NTF, "data ntf req");
            s_polar.state = POLAR_ENABLE_DATA_NTF;
            uint8_t cccd_val[2] = { 0x01, 0x00 };  /* notifications */
            polar_gatt_write(s_polar.conidx, s_polar.data_cccd_handle,
                             cccd_val, 2, GATTC_WRITE, POLAR_SEQ_DATA_CCCD);
        }
        break;

    case POLAR_ENABLE_DATA_NTF:
        if (evt->operation == GATTC_WRITE && evt->seq_num == POLAR_SEQ_DATA_CCCD)
        {
            polar_log("data ntf on");
            polar_log_state_change(s_polar.state, POLAR_GET_ACC_SETTINGS, "acc settings req");
            s_polar.state = POLAR_GET_ACC_SETTINGS;
            polar_send_cp_cmd(POLAR_PMD_OP_GET_SETTINGS, POLAR_PMD_MEAS_ACC,
                              NULL, 0, POLAR_SEQ_GET_ACC_SET);
        }
        break;

    default:
        break;
    }
}

/*
 * HELPERS
 */
static void polar_clear_handles(void)
{
    s_polar.svc_start = 0;
    s_polar.svc_end   = 0;
    s_polar.cp_decl_handle   = 0;
    s_polar.cp_handle        = 0;
    s_polar.cp_cccd_handle   = 0;
    s_polar.data_decl_handle = 0;
    s_polar.data_handle      = 0;
    s_polar.data_cccd_handle = 0;
    s_polar.cp_rsp_len       = 0;
    s_polar.acc_selected_tlvs_len = 0;
}

static void polar_reset(void)
{
    if (s_connect_timer != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(s_connect_timer);
        s_connect_timer = EASY_TIMER_INVALID_TIMER;
    }
    if (s_polar.state != POLAR_IDLE)
    {
        polar_log_state_change(s_polar.state, POLAR_IDLE, "reset");
    }
    memset(&s_polar, 0, sizeof(s_polar));
    s_polar.conidx = GAP_INVALID_CONIDX;
}

static void polar_retry_if_needed(void)
{
    /* Retry scan if CSC notifications are still enabled */
    extern bool csc_meas_ntf_enabled;
    polar_reset();
    if (csc_meas_ntf_enabled)
    {
        polar_log("retry");
        polar_start_scan();
    }
    else
    {
        polar_log("retry skipped");
    }
}

/*
 * PUBLIC API
 */
bool pp_imu_polar_init(void)
{
    polar_log("init");
    polar_reset();
    polar_start_scan();
    return true;
}

void pp_imu_polar_start(void)
{
    /* Streaming starts automatically after GATT setup completes */
}

void pp_imu_polar_stop(void)
{
    if (s_polar.state == POLAR_STREAMING && s_polar.conidx != GAP_INVALID_CONIDX)
    {
        /* Disconnect from Polar sensor */
        polar_log("stop disconnect");
        struct gapc_disconnect_cmd *cmd = KE_MSG_ALLOC(
            GAPC_DISCONNECT_CMD,
            KE_BUILD_ID(TASK_GAPC, s_polar.conidx), TASK_APP,
            gapc_disconnect_cmd);
        cmd->operation = GAPC_DISCONNECT;
        cmd->reason    = CO_ERROR_REMOTE_USER_TERM_CON;
        KE_MSG_SEND(cmd);
    }
    polar_reset();
}

bool pp_imu_polar_is_running(void)
{
    return (s_polar.state == POLAR_STREAMING);
}

/*
 * BLE CALLBACKS
 */
void pp_imu_polar_on_adv_report(struct gapm_adv_report_ind const *param)
{
    if (s_polar.state != POLAR_SCANNING) return;

    if (polar_is_verity_sense(param->report.data, param->report.data_len))
    {
        memcpy(&s_polar.target_addr, &param->report.adv_addr,
               sizeof(struct bd_addr));
        s_polar.target_addr_type = param->report.adv_addr_type;
        polar_log_state_change(s_polar.state, POLAR_CONNECTING, "match");
        polar_log_addr("match", s_polar.target_addr_type, &s_polar.target_addr);
        s_polar.state = POLAR_CONNECTING;

        /* Cancel scan — completion callback will trigger connect */
        struct gapm_cancel_cmd *cmd = KE_MSG_ALLOC(
            GAPM_CANCEL_CMD, TASK_GAPM, TASK_APP, gapm_cancel_cmd);
        cmd->operation = GAPM_CANCEL;
        KE_MSG_SEND(cmd);
    }
}

void pp_imu_polar_on_scan_complete(uint8_t status)
{
    polar_log_status("scan complete", status);
    if (s_polar.state == POLAR_CONNECTING)
    {
        /* Deferred connect to avoid calling GAP within GAP callback */
        app_easy_timer(1, polar_deferred_connect_cb);
    }
    else if (s_polar.state == POLAR_SCANNING)
    {
        /* Scan timed out — retry */
        polar_log("scan retry");
        s_polar.state = POLAR_IDLE;
        polar_retry_if_needed();
    }
}

bool pp_imu_polar_on_connection(uint8_t conidx,
                                struct gapc_connection_req_ind const *param)
{
    if (s_polar.state != POLAR_CONNECTING) return false;
    if (memcmp(&param->peer_addr, &s_polar.target_addr,
               sizeof(struct bd_addr)) != 0)
        return false;

    if (s_connect_timer != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(s_connect_timer);
        s_connect_timer = EASY_TIMER_INVALID_TIMER;
    }

    s_polar.conidx = conidx;
    s_polar.conhdl = param->conhdl;
    polar_log_addr("peer", param->peer_addr_type, &param->peer_addr);
    polar_log_state_change(s_polar.state, POLAR_WAIT_MTU, "connected");
    s_polar.state  = POLAR_WAIT_MTU;
    polar_log_conn("connected", conidx, param->conhdl);

    /* Initiate MTU exchange */
    polar_log("mtu exch");
    struct gattc_exc_mtu_cmd *cmd = KE_MSG_ALLOC(
        GATTC_EXC_MTU_CMD,
        KE_BUILD_ID(TASK_GATTC, conidx), TASK_APP,
        gattc_exc_mtu_cmd);
    cmd->operation = GATTC_MTU_EXCH;
    cmd->seq_num   = 0;
    KE_MSG_SEND(cmd);

    return true;
}

bool pp_imu_polar_on_disconnect(uint16_t conhdl)
{
    if (s_polar.conhdl != conhdl || s_polar.state == POLAR_IDLE)
        return false;

    paddling_pulse_console_printf("POLAR: disconnect h=%u\r\n", conhdl);
    polar_retry_if_needed();
    return true;
}

/*
 * MESSAGE HANDLER — called from user_catch_rest_hndl
 */
bool pp_imu_polar_handle_message(ke_msg_id_t msgid,
                                 void const *param,
                                 ke_task_id_t dest_id,
                                 ke_task_id_t src_id)
{
    if (s_polar.state == POLAR_IDLE) return false;

    switch (msgid)
    {
    case GATTC_DISC_SVC_IND:
        polar_on_disc_svc((const struct gattc_disc_svc_ind *)param);
        return true;

    case GATTC_DISC_CHAR_IND:
        polar_on_disc_char((const struct gattc_disc_char_ind *)param);
        return true;

    case GATTC_DISC_CHAR_DESC_IND:
        polar_on_disc_desc((const struct gattc_disc_char_desc_ind *)param);
        return true;

    case GATTC_CMP_EVT:
    {
        const struct gattc_cmp_evt *evt = (const struct gattc_cmp_evt *)param;
        if (KE_IDX_GET(src_id) == s_polar.conidx)
        {
            polar_on_gattc_cmp(evt);
            return true;
        }
    } break;

    case GATTC_EVENT_IND:
    {
        const struct gattc_event_ind *ind = (const struct gattc_event_ind *)param;
        uint8_t conidx = KE_IDX_GET(src_id);
        return polar_handle_gatt_event(conidx, ind->handle,
                                       ind->length, ind->value, false);
    }

    case GATTC_EVENT_REQ_IND:
    {
        const struct gattc_event_ind *ind = (const struct gattc_event_ind *)param;
        uint8_t conidx = KE_IDX_GET(src_id);
        bool handled = polar_handle_gatt_event(conidx, ind->handle,
                                                ind->length, ind->value, true);
        if (handled)
        {
            polar_send_event_cfm(conidx, ind->handle);
        }
        return handled;
    }

    case GATTC_MTU_CHANGED_IND:
    {
        const struct gattc_mtu_changed_ind *ind =
            (const struct gattc_mtu_changed_ind *)param;
        if (KE_IDX_GET(src_id) == s_polar.conidx)
        {
            s_polar.mtu = ind->mtu;
            paddling_pulse_console_printf("POLAR: mtu=%u\r\n", s_polar.mtu);
            return true;
        }
    } break;

    case GAPM_CMP_EVT:
    {
        const struct gapm_cmp_evt *evt = (const struct gapm_cmp_evt *)param;
        if (evt->operation == GAPM_CONNECTION_DIRECT &&
            evt->status != GAP_ERR_NO_ERROR &&
            s_polar.state == POLAR_CONNECTING)
        {
            polar_log_status("connect fail", evt->status);
            polar_retry_if_needed();
            return true;
        }
    } break;

    default:
        break;
    }

    return false;
}

#endif /* CFG_IMU_POLAR */
