#ifndef _PADDLING_PULSE_IMU_MANAGER_LOGIC_H_
#define _PADDLING_PULSE_IMU_MANAGER_LOGIC_H_

#include <stdbool.h>
#include <stdint.h>

#include "paddling_pulse_imu_manager.h"

typedef enum
{
    PP_IMU_LIFECYCLE_STOP_OLD = 0,
    PP_IMU_LIFECYCLE_SAMPLE_STORE_INIT = 1,
    PP_IMU_LIFECYCLE_STROKE_RATE_INIT = 2,
    PP_IMU_LIFECYCLE_START_NEW = 3,
    PP_IMU_LIFECYCLE_STEPS = 4,
} pp_imu_lifecycle_step_t;

pp_imu_state_t pp_imu_manager_logic_next_state(pp_imu_state_t current,
                                               pp_imu_override_t override,
                                               pp_imu_event_t ev);

bool pp_imu_manager_logic_switch_needed(pp_imu_source_t current_source,
                                        uint16_t current_rate_hz,
                                        pp_imu_source_t new_source,
                                        uint16_t new_rate_hz);

uint16_t pp_imu_manager_logic_clamp_rate(uint16_t rate_hz);

void pp_imu_manager_logic_fill_switch_sequence(
    pp_imu_lifecycle_step_t out[PP_IMU_LIFECYCLE_STEPS]);

#endif /* _PADDLING_PULSE_IMU_MANAGER_LOGIC_H_ */
