#include "da14531_config_basic.h"
#ifdef CFG_IMU_POLAR
#include "paddling_pulse_imu_polar.h"
bool pp_imu_polar_init(void)       { return false; }
void pp_imu_polar_start(void)      { }
void pp_imu_polar_stop(void)       { }
bool pp_imu_polar_is_running(void) { return false; }
void pp_imu_polar_on_adv_report(struct gapm_adv_report_ind const *param) { (void)param; }
void pp_imu_polar_on_scan_complete(uint8_t status) { (void)status; }
bool pp_imu_polar_on_connection(uint8_t conidx,
                                struct gapc_connection_req_ind const *param)
{ (void)conidx; (void)param; return false; }
bool pp_imu_polar_on_disconnect(uint16_t conhdl) { (void)conhdl; return false; }
bool pp_imu_polar_handle_message(ke_msg_id_t msgid, void const *param,
                                 ke_task_id_t dest_id, ke_task_id_t src_id)
{ (void)msgid; (void)param; (void)dest_id; (void)src_id; return false; }
#endif
