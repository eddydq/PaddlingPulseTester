#ifndef _PADDLING_PULSE_IMU_MANAGER_H_
#define _PADDLING_PULSE_IMU_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>

#include "paddling_pulse_stroke_rate.h"

#ifndef PP_POLAR_BOOT_SCAN_TIMEOUT_MS
#define PP_POLAR_BOOT_SCAN_TIMEOUT_MS  10000
#endif
#ifndef PP_POLAR_RECONNECT_TIMEOUT_MS
#define PP_POLAR_RECONNECT_TIMEOUT_MS  5000
#endif
#ifndef PP_POLAR_RSSI_WINDOW_MS
#define PP_POLAR_RSSI_WINDOW_MS  2000
#endif

typedef enum
{
    PP_IMU_NONE = 0,
    PP_IMU_LIS3DH = 1,
    PP_IMU_POLAR = 2,
} pp_imu_source_t;

/*
 * AUTO is the zero enumerator so cold-boot __SECTION_ZERO gives the
 * right default.
 */
typedef enum
{
    PP_IMU_OVERRIDE_AUTO = 0,
    PP_IMU_OVERRIDE_LIS3DH = 1,
    PP_IMU_OVERRIDE_POLAR = 2,
} pp_imu_override_t;

typedef enum
{
    PP_IMU_STATE_IDLE = 0,
    PP_IMU_STATE_POLAR_SEEKING = 1,
    PP_IMU_STATE_POLAR_ACTIVE = 2,
    PP_IMU_STATE_LIS3DH_ACTIVE = 3,
} pp_imu_state_t;

typedef enum
{
    PP_IMU_EV_POLAR_STREAMING = 0,
    PP_IMU_EV_POLAR_DISCONNECT = 1,
    PP_IMU_EV_POLAR_SCAN_FAIL = 2,
    PP_IMU_EV_RECONNECT_TIMEOUT = 3,
    PP_IMU_EV_BOOT_SCAN_TIMEOUT = 4,
} pp_imu_event_t;

void pp_imu_manager_init(void);
void pp_imu_manager_start(void);
void pp_imu_manager_stop(void);
void pp_imu_manager_process(void);

void pp_imu_manager_on_event(pp_imu_event_t ev);

bool pp_imu_manager_set_override(pp_imu_override_t target);
pp_imu_override_t pp_imu_manager_get_override(void);
pp_imu_source_t pp_imu_manager_get_source(void);
pp_imu_state_t pp_imu_manager_get_state(void);
uint16_t pp_imu_manager_get_rate_hz(void);

#endif /* _PADDLING_PULSE_IMU_MANAGER_H_ */
