#ifndef _PADDLING_PULSE_IMU_MPU6050_H_
#define _PADDLING_PULSE_IMU_MPU6050_H_

#include <stdbool.h>

bool pp_imu_mpu6050_init(void);
void pp_imu_mpu6050_start(void);
void pp_imu_mpu6050_stop(void);
void pp_imu_mpu6050_process(void);
bool pp_imu_mpu6050_is_running(void);

#endif
