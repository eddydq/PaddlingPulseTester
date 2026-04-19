#include "da14531_config_basic.h"

#ifdef CFG_IMU_DUAL

#include <stdbool.h>
#include <stdint.h>

#include "arch.h"
#include "app_easy_timer.h"

#include "paddling_pulse_imu_manager.h"
#include "paddling_pulse_imu_manager_logic.h"
#include "paddling_pulse_imu_lis3dh.h"
#include "paddling_pulse_imu_polar.h"
#include "paddling_pulse_sample_store.h"
#include "paddling_pulse_stroke_rate.h"

typedef struct
{
    pp_imu_state_t state;
    pp_imu_source_t source;
    pp_imu_override_t override;
    uint16_t rate_hz;
    timer_hnd scan_timer;
} pp_imu_manager_ctx_t;

static pp_imu_manager_ctx_t s_ctx __SECTION_ZERO("retention_mem_area0");

static const pp_stroke_rate_params_t *params_for(pp_imu_source_t src)
{
    if (src == PP_IMU_POLAR)
    {
        return &pp_imu_polar_params;
    }

    return &pp_imu_lis3dh_params;
}

static void stop_current_driver(void)
{
    if (s_ctx.source == PP_IMU_LIS3DH)
    {
        pp_imu_lis3dh_stop();
    }
    else if (s_ctx.source == PP_IMU_POLAR)
    {
        pp_imu_polar_stop();
    }
}

static void start_driver(pp_imu_source_t src)
{
    if (src == PP_IMU_LIS3DH)
    {
        pp_imu_lis3dh_start();
    }
    else if (src == PP_IMU_POLAR)
    {
        pp_imu_polar_start();
    }
}

static void cancel_scan_timer(void)
{
    if (s_ctx.scan_timer != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(s_ctx.scan_timer);
        s_ctx.scan_timer = EASY_TIMER_INVALID_TIMER;
    }
}

static void boot_scan_timeout_cb(void)
{
    s_ctx.scan_timer = EASY_TIMER_INVALID_TIMER;
    pp_imu_manager_on_event(PP_IMU_EV_BOOT_SCAN_TIMEOUT);
}

static void reconnect_timeout_cb(void)
{
    s_ctx.scan_timer = EASY_TIMER_INVALID_TIMER;
    pp_imu_manager_on_event(PP_IMU_EV_RECONNECT_TIMEOUT);
}

static void switch_source(pp_imu_source_t new_source, uint16_t new_rate_hz)
{
    uint16_t clamped = pp_imu_manager_logic_clamp_rate(new_rate_hz);

    if (!pp_imu_manager_logic_switch_needed(s_ctx.source,
                                            s_ctx.rate_hz,
                                            new_source,
                                            clamped))
    {
        return;
    }

    stop_current_driver();
    pp_sample_store_init(clamped);
    pp_stroke_rate_init(params_for(new_source));
    start_driver(new_source);

    s_ctx.source = new_source;
    s_ctx.rate_hz = clamped;
}

static void enter_state(pp_imu_state_t next)
{
    pp_imu_state_t previous = s_ctx.state;

    s_ctx.state = next;

    switch (next)
    {
    case PP_IMU_STATE_POLAR_SEEKING:
        cancel_scan_timer();
        if (s_ctx.override == PP_IMU_OVERRIDE_AUTO)
        {
            uint32_t timeout_ms = (previous == PP_IMU_STATE_POLAR_ACTIVE)
                                ? PP_POLAR_RECONNECT_TIMEOUT_MS
                                : PP_POLAR_BOOT_SCAN_TIMEOUT_MS;
            timer_callback callback = (previous == PP_IMU_STATE_POLAR_ACTIVE)
                                    ? reconnect_timeout_cb
                                    : boot_scan_timeout_cb;
            s_ctx.scan_timer = app_easy_timer(timeout_ms, callback);
        }
        break;

    case PP_IMU_STATE_POLAR_ACTIVE:
        cancel_scan_timer();
        switch_source(PP_IMU_POLAR, pp_imu_polar_get_actual_sample_rate_hz());
        break;

    case PP_IMU_STATE_LIS3DH_ACTIVE:
        cancel_scan_timer();
        switch_source(PP_IMU_LIS3DH, pp_imu_lis3dh_params.sample_rate_hz);
        break;

    case PP_IMU_STATE_IDLE:
    default:
        cancel_scan_timer();
        stop_current_driver();
        pp_sample_store_reset();
        s_ctx.source = PP_IMU_NONE;
        s_ctx.rate_hz = 0;
        break;
    }
}

void pp_imu_manager_init(void)
{
    s_ctx.state = PP_IMU_STATE_IDLE;
    s_ctx.source = PP_IMU_NONE;
    s_ctx.rate_hz = 0;
    s_ctx.scan_timer = EASY_TIMER_INVALID_TIMER;

    (void)pp_imu_lis3dh_init();
    (void)pp_imu_polar_init();
}

void pp_imu_manager_start(void)
{
    if (s_ctx.override == PP_IMU_OVERRIDE_LIS3DH)
    {
        enter_state(PP_IMU_STATE_LIS3DH_ACTIVE);
        return;
    }

    enter_state(PP_IMU_STATE_POLAR_SEEKING);
}

void pp_imu_manager_stop(void)
{
    enter_state(PP_IMU_STATE_IDLE);
}

void pp_imu_manager_process(void)
{
    if (s_ctx.source == PP_IMU_LIS3DH)
    {
        pp_imu_lis3dh_process();
    }
}

void pp_imu_manager_on_event(pp_imu_event_t ev)
{
    pp_imu_state_t next = pp_imu_manager_logic_next_state(s_ctx.state,
                                                          s_ctx.override,
                                                          ev);
    if (next != s_ctx.state)
    {
        enter_state(next);
    }
}

bool pp_imu_manager_set_override(pp_imu_override_t target)
{
    if ((target != PP_IMU_OVERRIDE_AUTO) &&
        (target != PP_IMU_OVERRIDE_LIS3DH) &&
        (target != PP_IMU_OVERRIDE_POLAR))
    {
        return false;
    }

    s_ctx.override = target;

    if ((target == PP_IMU_OVERRIDE_LIS3DH) && (s_ctx.source != PP_IMU_LIS3DH))
    {
        enter_state(PP_IMU_STATE_LIS3DH_ACTIVE);
    }
    else if ((target == PP_IMU_OVERRIDE_POLAR) &&
             (s_ctx.source != PP_IMU_POLAR))
    {
        enter_state(PP_IMU_STATE_POLAR_SEEKING);
    }

    return true;
}

pp_imu_override_t pp_imu_manager_get_override(void)
{
    return s_ctx.override;
}

pp_imu_source_t pp_imu_manager_get_source(void)
{
    return s_ctx.source;
}

pp_imu_state_t pp_imu_manager_get_state(void)
{
    return s_ctx.state;
}

uint16_t pp_imu_manager_get_rate_hz(void)
{
    return s_ctx.rate_hz;
}

#endif /* CFG_IMU_DUAL */
