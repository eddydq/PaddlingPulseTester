#include "da14531_config_basic.h"
#ifdef CFG_IMU_MPU6050
#include "paddling_pulse_imu_mpu6050.h"
bool pp_imu_mpu6050_init(void)       { return false; }
void pp_imu_mpu6050_start(void)      { }
void pp_imu_mpu6050_stop(void)       { }
void pp_imu_mpu6050_process(void)    { }
bool pp_imu_mpu6050_is_running(void) { return false; }
#endif
