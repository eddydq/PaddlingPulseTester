#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "paddling_pulse_sample_store.h"
#include "paddling_pulse_stroke_rate.h"

#define TEST_SAMPLE_RATE_HZ      100u
#define TEST_SIGNAL_PERIOD       50u
#define TEST_SIGNAL_HALF_PERIOD  (TEST_SIGNAL_PERIOD / 2u)
#define TEST_SIGNAL_AMPLITUDE    1000
#define TEST_SIGNAL_SAMPLES      128u
#define TEST_SMALL_WINDOW        63u
#define TEST_LARGE_WINDOW        128u
#define TEST_CONFIRM_UPDATES     3u

static const pp_stroke_rate_params_t params_small_window = {
    .axis = PP_AXIS_Z,
    .sample_rate_hz = TEST_SAMPLE_RATE_HZ,
    .window_samples = TEST_SMALL_WINDOW,
    .min_rpm = PP_STROKE_RATE_MIN_RPM,
    .max_rpm = PP_STROKE_RATE_MAX_RPM,
    .kalman_q = PP_KALMAN_Q,
    .kalman_r = PP_KALMAN_R,
    .kalman_p_max = PP_KALMAN_P_MAX,
    .autocorr_confidence_min = PP_AUTOCORR_CONFIDENCE_MIN,
    .autocorr_energy_min = PP_AUTOCORR_ENERGY_MIN,
    .autocorr_harmonic_pct = PP_AUTOCORR_HARMONIC_PCT,
    .kalman_confirm_tolerance_rpm = PP_KALMAN_CONFIRM_TOLERANCE_RPM,
    .kalman_max_jump_rpm = PP_KALMAN_MAX_JUMP_RPM,
    .kalman_invalid_max = PP_KALMAN_INVALID_MAX,
    .kalman_confirm_count = PP_KALMAN_CONFIRM_COUNT,
};

static const pp_stroke_rate_params_t params_large_window = {
    .axis = PP_AXIS_Z,
    .sample_rate_hz = TEST_SAMPLE_RATE_HZ,
    .window_samples = TEST_LARGE_WINDOW,
    .min_rpm = PP_STROKE_RATE_MIN_RPM,
    .max_rpm = PP_STROKE_RATE_MAX_RPM,
    .kalman_q = PP_KALMAN_Q,
    .kalman_r = PP_KALMAN_R,
    .kalman_p_max = PP_KALMAN_P_MAX,
    .autocorr_confidence_min = PP_AUTOCORR_CONFIDENCE_MIN,
    .autocorr_energy_min = PP_AUTOCORR_ENERGY_MIN,
    .autocorr_harmonic_pct = PP_AUTOCORR_HARMONIC_PCT,
    .kalman_confirm_tolerance_rpm = PP_KALMAN_CONFIRM_TOLERANCE_RPM,
    .kalman_max_jump_rpm = PP_KALMAN_MAX_JUMP_RPM,
    .kalman_invalid_max = PP_KALMAN_INVALID_MAX,
    .kalman_confirm_count = PP_KALMAN_CONFIRM_COUNT,
};

static void load_periodic_signal(void)
{
    uint16_t i;

    pp_sample_store_init(TEST_SAMPLE_RATE_HZ);
    for (i = 0; i < TEST_SIGNAL_SAMPLES; ++i)
    {
        int16_t sample = ((i % TEST_SIGNAL_PERIOD) < TEST_SIGNAL_HALF_PERIOD)
                       ? TEST_SIGNAL_AMPLITUDE
                       : -TEST_SIGNAL_AMPLITUDE;
        pp_sample_store_push(sample);
    }
}

static uint8_t run_updates(const pp_stroke_rate_params_t *params)
{
    uint8_t rpm = 0;
    uint8_t i;

    pp_stroke_rate_init(params);
    for (i = 0; i < TEST_CONFIRM_UPDATES; ++i)
    {
        rpm = pp_stroke_rate_update();
    }
    return rpm;
}

static void test_window_samples_come_from_params(void)
{
    load_periodic_signal();
    assert(run_updates(&params_small_window) == 0);

    load_periodic_signal();
    assert(run_updates(&params_large_window) > 0);
}

static void test_init_ignores_null_params(void)
{
    load_periodic_signal();
    pp_stroke_rate_init(&params_large_window);
    pp_stroke_rate_init(NULL);

    assert(pp_stroke_rate_update() == 0);
    assert(pp_stroke_rate_update() == 0);
    assert(pp_stroke_rate_update() > 0);
}

int main(void)
{
    test_window_samples_come_from_params();
    test_init_ignores_null_params();
    printf("stroke-rate params tests passed\n");
    return 0;
}
