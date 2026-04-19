#include "paddling_pulse_imu_manager_logic.h"
#include "paddling_pulse_sample_store.h"

pp_imu_state_t pp_imu_manager_logic_next_state(pp_imu_state_t current,
                                               pp_imu_override_t override,
                                               pp_imu_event_t ev)
{
    switch (current)
    {
    case PP_IMU_STATE_POLAR_SEEKING:
        switch (ev)
        {
        case PP_IMU_EV_POLAR_STREAMING:
            return PP_IMU_STATE_POLAR_ACTIVE;

        case PP_IMU_EV_BOOT_SCAN_TIMEOUT:
        case PP_IMU_EV_RECONNECT_TIMEOUT:
        case PP_IMU_EV_POLAR_SCAN_FAIL:
            return (override == PP_IMU_OVERRIDE_AUTO)
                 ? PP_IMU_STATE_LIS3DH_ACTIVE
                 : PP_IMU_STATE_POLAR_SEEKING;

        default:
            return current;
        }

    case PP_IMU_STATE_POLAR_ACTIVE:
        if (ev == PP_IMU_EV_POLAR_DISCONNECT)
        {
            return PP_IMU_STATE_POLAR_SEEKING;
        }
        return current;

    case PP_IMU_STATE_LIS3DH_ACTIVE:
    case PP_IMU_STATE_IDLE:
    default:
        return current;
    }
}

bool pp_imu_manager_logic_switch_needed(pp_imu_source_t current_source,
                                        uint16_t current_rate_hz,
                                        pp_imu_source_t new_source,
                                        uint16_t new_rate_hz)
{
    return (current_source != new_source) || (current_rate_hz != new_rate_hz);
}

uint16_t pp_imu_manager_logic_clamp_rate(uint16_t rate_hz)
{
    if (rate_hz < PP_SAMPLE_STORE_MIN_RATE_HZ)
    {
        return PP_SAMPLE_STORE_MIN_RATE_HZ;
    }
    if (rate_hz > PP_SAMPLE_STORE_MAX_RATE_HZ)
    {
        return PP_SAMPLE_STORE_MAX_RATE_HZ;
    }
    return rate_hz;
}
