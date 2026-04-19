#ifndef _PADDLING_PULSE_IMU_POLAR_H_
#define _PADDLING_PULSE_IMU_POLAR_H_

#include <stdbool.h>
#include <stdint.h>

#if defined(CFG_IMU_POLAR) || defined(CFG_IMU_DUAL)

#include "paddling_pulse_stroke_rate.h"
#include "ke_msg.h"
#include "gapc_task.h"
#include "gapm_task.h"

extern const pp_stroke_rate_params_t pp_imu_polar_params;

bool pp_imu_polar_init(void);
void pp_imu_polar_start(void);
void pp_imu_polar_stop(void);
bool pp_imu_polar_is_running(void);

/**
 * @brief Sample rate the Polar strap negotiated via PMD settings.
 *
 * Meaningful only after the strap has answered the PMD settings query and
 * begun streaming. Before that, returns PP_STROKE_RATE_DEFAULT_POLAR_HZ.
 */
uint16_t pp_imu_polar_get_actual_sample_rate_hz(void);

/* BLE callback handlers — registered in user_callback_config.h */
void pp_imu_polar_on_adv_report(struct gapm_adv_report_ind const *param);
void pp_imu_polar_on_scan_complete(uint8_t status);
void pp_imu_polar_on_connect_failed(void);

/* Connection handlers — called from user_app_connection/disconnect */
bool pp_imu_polar_on_connection(uint8_t conidx,
                                struct gapc_connection_req_ind const *param);
bool pp_imu_polar_on_disconnect(uint16_t conhdl);

/* Message handler — called from user_catch_rest_hndl */
bool pp_imu_polar_handle_message(ke_msg_id_t msgid,
                                 void const *param,
                                 ke_task_id_t dest_id,
                                 ke_task_id_t src_id);

#endif /* CFG_IMU_POLAR || CFG_IMU_DUAL */

#endif
