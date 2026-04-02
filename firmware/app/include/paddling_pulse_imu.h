/**
 ****************************************************************************************
 * @file paddling_pulse_imu.h
 * @brief Common IMU driver interface — compile-time routed via #ifdef.
 ****************************************************************************************
 */

#ifndef _PADDLING_PULSE_IMU_H_
#define _PADDLING_PULSE_IMU_H_

#include "da14531_config_basic.h"

/* Mutual exclusivity guards */
#if defined(CFG_IMU_POLAR) + defined(CFG_IMU_MPU6050) + defined(CFG_IMU_LIS3DH) != 1
    #error "Exactly one CFG_IMU_* must be defined"
#endif

#if defined(CFG_IMU_AXIS_X) + defined(CFG_IMU_AXIS_Y) + defined(CFG_IMU_AXIS_Z) != 1
    #error "Exactly one CFG_IMU_AXIS_* must be defined"
#endif

/* Include active driver header */
#if defined(CFG_IMU_LIS3DH)
    #include "paddling_pulse_imu_lis3dh.h"
#elif defined(CFG_IMU_MPU6050)
    #include "paddling_pulse_imu_mpu6050.h"
#elif defined(CFG_IMU_POLAR)
    #include "paddling_pulse_imu_polar.h"
#endif

/* --- Unified API: each call maps to the selected driver --- */

static __inline bool pp_imu_init(void)
{
#if defined(CFG_IMU_LIS3DH)
    return pp_imu_lis3dh_init();
#elif defined(CFG_IMU_MPU6050)
    return pp_imu_mpu6050_init();
#elif defined(CFG_IMU_POLAR)
    return pp_imu_polar_init();
#endif
}

static __inline void pp_imu_start(void)
{
#if defined(CFG_IMU_LIS3DH)
    pp_imu_lis3dh_start();
#elif defined(CFG_IMU_MPU6050)
    pp_imu_mpu6050_start();
#elif defined(CFG_IMU_POLAR)
    pp_imu_polar_start();
#endif
}

static __inline void pp_imu_stop(void)
{
#if defined(CFG_IMU_LIS3DH)
    pp_imu_lis3dh_stop();
#elif defined(CFG_IMU_MPU6050)
    pp_imu_mpu6050_stop();
#elif defined(CFG_IMU_POLAR)
    pp_imu_polar_stop();
#endif
}

static __inline void pp_imu_process(void)
{
#if defined(CFG_IMU_LIS3DH)
    pp_imu_lis3dh_process();
#elif defined(CFG_IMU_MPU6050)
    pp_imu_mpu6050_process();
#elif defined(CFG_IMU_POLAR)
    /* Polar data arrives via GATTC_EVENT_IND — nothing to poll */
#endif
}

static __inline bool pp_imu_is_running(void)
{
#if defined(CFG_IMU_LIS3DH)
    return pp_imu_lis3dh_is_running();
#elif defined(CFG_IMU_MPU6050)
    return pp_imu_mpu6050_is_running();
#elif defined(CFG_IMU_POLAR)
    return pp_imu_polar_is_running();
#endif
}

static __inline const char *pp_imu_get_name(void)
{
#if defined(CFG_IMU_LIS3DH)
    return "LIS3DH";
#elif defined(CFG_IMU_MPU6050)
    return "MPU6050";
#elif defined(CFG_IMU_POLAR)
    return "Polar";
#endif
}

#endif // _PADDLING_PULSE_IMU_H_
