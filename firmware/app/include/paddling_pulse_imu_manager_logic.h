#ifndef _PADDLING_PULSE_IMU_MANAGER_LOGIC_H_
#define _PADDLING_PULSE_IMU_MANAGER_LOGIC_H_

#include <stdbool.h>
#include <stdint.h>

#include "paddling_pulse_imu_manager.h"

pp_imu_state_t pp_imu_manager_logic_next_state(pp_imu_state_t current,
                                               pp_imu_override_t override,
                                               pp_imu_event_t ev);

bool pp_imu_manager_logic_switch_needed(pp_imu_source_t current_source,
                                        uint16_t current_rate_hz,
                                        pp_imu_source_t new_source,
                                        uint16_t new_rate_hz);

uint16_t pp_imu_manager_logic_clamp_rate(uint16_t rate_hz);

#endif /* _PADDLING_PULSE_IMU_MANAGER_LOGIC_H_ */
