#include "da14531_config_basic.h"
#ifdef CFG_IMU_LIS3DH
/* Implementation in subsequent commit */
#include "paddling_pulse_imu_lis3dh.h"
bool pp_imu_lis3dh_init(void)       { return false; }
void pp_imu_lis3dh_start(void)      { }
void pp_imu_lis3dh_stop(void)       { }
void pp_imu_lis3dh_process(void)    { }
bool pp_imu_lis3dh_is_running(void) { return false; }
#endif
