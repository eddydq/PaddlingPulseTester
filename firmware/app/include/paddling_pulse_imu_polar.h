#ifndef _PADDLING_PULSE_IMU_POLAR_H_
#define _PADDLING_PULSE_IMU_POLAR_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef CFG_IMU_POLAR

#include "ke_msg.h"
#include "gapc_task.h"
#include "gapm_task.h"

bool pp_imu_polar_init(void);
void pp_imu_polar_start(void);
void pp_imu_polar_stop(void);
bool pp_imu_polar_is_running(void);

/* BLE callback handlers — registered in user_callback_config.h */
void pp_imu_polar_on_adv_report(struct gapm_adv_report_ind const *param);
void pp_imu_polar_on_scan_complete(uint8_t status);

/* Connection handlers — called from user_app_connection/disconnect */
bool pp_imu_polar_on_connection(uint8_t conidx,
                                struct gapc_connection_req_ind const *param);
bool pp_imu_polar_on_disconnect(uint16_t conhdl);

/* Message handler — called from user_catch_rest_hndl */
bool pp_imu_polar_handle_message(ke_msg_id_t msgid,
                                 void const *param,
                                 ke_task_id_t dest_id,
                                 ke_task_id_t src_id);

#endif /* CFG_IMU_POLAR */

#endif
