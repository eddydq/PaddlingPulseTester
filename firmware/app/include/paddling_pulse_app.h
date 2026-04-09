/**
 ****************************************************************************************
 *
 * @file paddling_pulse_app.h
 *
 * @brief PaddlingPulse application header file.
 *
 * Copyright (C) 2015-2025 Renesas Electronics Corporation and/or its affiliates.
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

#ifndef _PADDLING_PULSE_APP_H_
#define _PADDLING_PULSE_APP_H_

/**
 ****************************************************************************************
 * @addtogroup APP
 * @ingroup
 *
 * @brief
 *
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
 
#include "gapc_task.h"                 // gap functions and messages
#include "app_task.h"                  // application task
#include "app.h"                       // application definitions
#include "app_callback.h"
#include "app_cscps.h"


/*
 * DEFINES
 ****************************************************************************************
 */

/* Duration of timer for connection parameter update request */
#define APP_PARAM_UPDATE_REQUEST_TO         (1000)   // 1000*10ms = 10sec, The maximum allowed value is 41943sec (4194300 * 10ms)

/* Advertising data update timer */
#define APP_ADV_DATA_UPDATE_TO              (3000)   // 3000*10ms = 30sec, The maximum allowed value is 41943sec (4194300 * 10ms)

/* CSC measurement notification timer */
#define APP_CSC_MEAS_NTF_TO                 (100)    // 100*10ms = 1sec

/* IMU FIFO drain timer */
#define APP_IMU_PROCESS_TO                  (25)     // 25*10ms = 250ms

/* Stroke rate update timer (derived from algorithm tunable) */
#include "paddling_pulse_stroke_rate.h"
#define APP_STROKE_RATE_TO                  (PP_STROKE_RATE_INTERVAL_MS / 10)

/* Cycling cadence sensor appearance */
#define APP_CSCP_DEVICE_APPEARANCE          (0x0484)

/* Default simulated cadence until a real stroke source is connected */
#define APP_CSCP_DEFAULT_CADENCE_RPM        (60)

/* Manufacturer specific data constants */
#define APP_AD_MSD_COMPANY_ID               (0xABCD)
#define APP_AD_MSD_COMPANY_ID_LEN           (2)
#define APP_AD_MSD_DATA_LEN                 (sizeof(uint16_t))

/*
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief Application callback function.
 ****************************************************************************************
*/
void user_on_cscps_cfg_ntfind_ind(uint8_t conidx, const struct cscps_cfg_ntfind_ind *param);

/**
 ****************************************************************************************
 * @brief Returns the device appearance as a cycling cadence sensor.
 * @param[in,out] appearance Pointer to the GAP appearance value.
 ****************************************************************************************
*/
void user_app_on_get_dev_appearance(uint16_t *appearance);

/**
 ****************************************************************************************
 * @brief Application initialization function.
 ****************************************************************************************
*/
void user_app_init(void);

/**
 ****************************************************************************************
 * @brief Load the stored OTA pipeline or build the default graph.
 ****************************************************************************************
*/
void paddling_pulse_pipeline_init(void);

/**
 ****************************************************************************************
 * @brief Advertising function.
 ****************************************************************************************
*/
void user_app_adv_start(void);

/**
 ****************************************************************************************
 * @brief Connection function.
 * @param[in] conidx        Connection Id index
 * @param[in] param         Pointer to GAPC_CONNECTION_REQ_IND message
 ****************************************************************************************
*/
void user_app_connection(const uint8_t conidx, struct gapc_connection_req_ind const *param);

/**
 ****************************************************************************************
 * @brief Undirect advertising completion function.
 * @param[in] status Command complete event message status
 ****************************************************************************************
*/
void user_app_adv_undirect_complete(uint8_t status);

/**
 ****************************************************************************************
 * @brief Disconnection function.
 * @param[in] param         Pointer to GAPC_DISCONNECT_IND message
 ****************************************************************************************
*/
void user_app_disconnect(struct gapc_disconnect_ind const *param);

/**
 ****************************************************************************************
 * @brief Handles the messages that are not handled by the SDK internal mechanisms.
 * @param[in] msgid   Id of the message received.
 * @param[in] param   Pointer to the parameters of the message.
 * @param[in] dest_id ID of the receiving task instance.
 * @param[in] src_id  ID of the sending task instance.
 ****************************************************************************************
*/
void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id);

/// @} APP

#endif // _PADDLING_PULSE_APP_H_
