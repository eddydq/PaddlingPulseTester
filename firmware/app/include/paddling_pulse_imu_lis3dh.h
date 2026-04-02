#ifndef _PADDLING_PULSE_IMU_LIS3DH_H_
#define _PADDLING_PULSE_IMU_LIS3DH_H_

#include <stdbool.h>

bool pp_imu_lis3dh_init(void);
void pp_imu_lis3dh_start(void);
void pp_imu_lis3dh_stop(void);
void pp_imu_lis3dh_process(void);
bool pp_imu_lis3dh_is_running(void);

#endif
