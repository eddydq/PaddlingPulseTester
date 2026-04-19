/**
 ****************************************************************************************
 * @file paddling_pulse_stroke_rate.c
 * @brief Autocorrelation-based stroke rate + Kalman filter, Q16.16 fixed-point.
 ****************************************************************************************
 */

#include "paddling_pulse_stroke_rate.h"
#include "paddling_pulse_sample_store.h"
#include <stdbool.h>
#include <string.h>

#if defined(__arm__) || defined(__ARMCC_VERSION)
#include "arch.h"
#endif

#ifndef __SECTION_ZERO
#define __SECTION_ZERO(name)
#endif

/*
 * FIXED-POINT HELPERS
 */

#define FP_SHIFT  16
#define FP_ONE    (1 << FP_SHIFT)

static int32_t fp_mul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * (int64_t)b) >> FP_SHIFT);
}

static int32_t fp_div(int32_t a, int32_t b)
{
    if (b == 0) return 0;
    return (int32_t)(((int64_t)a << FP_SHIFT) / (int64_t)b);
}

static uint16_t u16_gcd(uint16_t a, uint16_t b)
{
    while (b != 0)
    {
        uint16_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/*
 * KALMAN STATE (retention RAM)
 */

static struct {
    int32_t rate_fp;
    int32_t P_fp;
} s_kalman __SECTION_ZERO("retention_mem_area0");

static const pp_stroke_rate_params_t *s_params __SECTION_ZERO("retention_mem_area0");
static uint8_t s_last_rpm       __SECTION_ZERO("retention_mem_area0");
static uint8_t s_invalid_count  __SECTION_ZERO("retention_mem_area0");
static uint8_t s_confirm_count  __SECTION_ZERO("retention_mem_area0");
static int32_t s_confirm_rpm_fp __SECTION_ZERO("retention_mem_area0");

/*
 * INIT
 */

void pp_stroke_rate_init(const pp_stroke_rate_params_t *params)
{
    if (params == NULL)
    {
        return;
    }

    s_params = params;
    s_kalman.rate_fp = 0;
    s_kalman.P_fp    = s_params->kalman_p_max;
    s_last_rpm       = 0;
    s_invalid_count  = 0;
    s_confirm_count  = 0;
    s_confirm_rpm_fp = 0;
}

/*
 * AUTOCORRELATION ESTIMATOR
 * Returns Q16.16 RPM or 0 if no valid measurement.
 */

static int32_t autocorr_estimate(void)
{
    uint16_t count   = pp_sample_store_get_count();
    uint16_t rate_hz = pp_sample_store_get_rate_hz();
    uint16_t window  = (count < s_params->window_samples)
                       ? count : s_params->window_samples;

    if (window < 64 || rate_hz == 0)
    {
        return 0;
    }

    uint16_t offset = count - window;

    /* --- Mean --- */
    int32_t sum = 0;
    uint16_t i;
    for (i = 0; i < window; i++)
    {
        sum += pp_sample_store_get(offset + i);
    }
    int16_t mean = (int16_t)(sum / (int32_t)window);

    /* --- Lag bounds from RPM range --- */
    uint16_t min_lag = (uint16_t)((60UL * rate_hz) / s_params->max_rpm);
    uint16_t max_lag = (uint16_t)((60UL * rate_hz) / s_params->min_rpm);
    if (max_lag >= window) max_lag = window - 1;
    if (min_lag < 1)       min_lag = 1;
    if (min_lag >= max_lag) return 0;

    /* --- Energy (autocorrelation at lag 0) --- */
    int32_t energy = 0;
    for (i = 0; i < window; i++)
    {
        int32_t d = pp_sample_store_get(offset + i) - mean;
        energy += (d * d) >> 8;
    }
    if (energy < s_params->autocorr_energy_min)
    {
        return 0;
    }

    /* --- Lag scan: find peak --- */
    int32_t  best_val  = -0x7FFFFFFF;
    uint16_t best_lag  = min_lag;
    int32_t  best_prev = 0;
    int32_t  prev_acc  = 0;

    uint16_t lag;
    for (lag = min_lag; lag <= max_lag; lag++)
    {
        int32_t  acc = 0;
        uint16_t n   = window - lag;
        for (i = 0; i < n; i++)
        {
            int32_t a = pp_sample_store_get(offset + i)       - mean;
            int32_t b = pp_sample_store_get(offset + i + lag)  - mean;
            acc += (a * b) >> 8;
        }
        if (acc > best_val)
        {
            best_val  = acc;
            best_lag  = lag;
            best_prev = prev_acc;
        }
        prev_acc = acc;
    }

    /* --- Confidence threshold --- */
    int32_t confidence = fp_div(best_val, energy);
    if (confidence < s_params->autocorr_confidence_min)
    {
        return 0;
    }

    /* --- Floor rejection --- */
    if (best_lag == min_lag)
    {
        return 0;
    }

    /* --- Harmonic guard: check half-lag --- */
    uint16_t half_lag = best_lag / 2;
    if (half_lag >= min_lag)
    {
        int32_t  half_acc = 0;
        uint16_t hn       = window - half_lag;
        for (i = 0; i < hn; i++)
        {
            int32_t ah = pp_sample_store_get(offset + i)              - mean;
            int32_t bh = pp_sample_store_get(offset + i + half_lag)   - mean;
            half_acc += (ah * bh) >> 8;
        }
        if (half_acc * 100 >= best_val * s_params->autocorr_harmonic_pct)
        {
            /* Use half-lag (integer precision) */
            int32_t numerator = (int32_t)(60UL * rate_hz) * FP_ONE;
            return fp_div(numerator, (int32_t)half_lag * FP_ONE);
        }
    }

    /* --- Compute best_next for interpolation --- */
    int32_t best_next = best_val;
    if (best_lag < max_lag)
    {
        best_next = 0;
        uint16_t nn = window - (best_lag + 1);
        for (i = 0; i < nn; i++)
        {
            int32_t an = pp_sample_store_get(offset + i)                - mean;
            int32_t bn = pp_sample_store_get(offset + i + best_lag + 1) - mean;
            best_next += (an * bn) >> 8;
        }
    }

    /* --- Sub-sample parabolic interpolation --- */
    int32_t refined_lag_fp;
    int32_t denom = 2 * (best_prev - 2 * best_val + best_next);
    if (denom != 0 && best_lag > min_lag && best_lag < max_lag)
    {
        int32_t delta_fp = fp_div(best_prev - best_next, denom);
        refined_lag_fp   = (int32_t)best_lag * FP_ONE + delta_fp;
    }
    else
    {
        refined_lag_fp = (int32_t)best_lag * FP_ONE;
    }

    if (refined_lag_fp <= 0)
    {
        return 0;
    }

    /* RPM = 60 * sample_rate / refined_lag */
    int32_t numerator = (int32_t)(60UL * rate_hz) * FP_ONE;
    return fp_div(numerator, refined_lag_fp);
}

/*
 * KALMAN FILTER
 */

static void kalman_update(int32_t meas_rpm_fp, bool valid)
{
    int32_t P_predict = s_kalman.P_fp + s_params->kalman_q;
    if (P_predict > s_params->kalman_p_max)
    {
        P_predict = s_params->kalman_p_max;
    }

    if (!valid)
    {
        s_invalid_count++;
        if (s_invalid_count >= s_params->kalman_invalid_max)
        {
            s_kalman.rate_fp = 0;
            s_kalman.P_fp    = s_params->kalman_p_max;
            s_last_rpm       = 0;
            s_confirm_count  = 0;
        }
        return;
    }

    s_invalid_count = 0;

    /* --- Cold start: require PP_KALMAN_CONFIRM_COUNT consistent readings --- */
    if (s_kalman.rate_fp == 0)
    {
        int32_t tolerance_fp = (int32_t)s_params->kalman_confirm_tolerance_rpm * FP_ONE;
        if (s_confirm_count == 0)
        {
            s_confirm_rpm_fp = meas_rpm_fp;
            s_confirm_count  = 1;
            return;
        }
        int32_t diff = meas_rpm_fp - s_confirm_rpm_fp;
        if (diff < 0) diff = -diff;

        if (diff > tolerance_fp)
        {
            s_confirm_rpm_fp = meas_rpm_fp;
            s_confirm_count  = 1;
            return;
        }
        s_confirm_count++;
        if (s_confirm_count >= s_params->kalman_confirm_count)
        {
            s_kalman.rate_fp = meas_rpm_fp;
            s_kalman.P_fp    = s_params->kalman_r;
            s_confirm_count  = 0;
        }
        return;
    }

    /* --- Normal update: reject implausible jumps --- */
    int32_t max_jump_fp  = (int32_t)s_params->kalman_max_jump_rpm * FP_ONE;
    int32_t innovation   = meas_rpm_fp - s_kalman.rate_fp;
    if (innovation < 0) innovation = -innovation;

    if (innovation > max_jump_fp)
    {
        s_invalid_count++;
        if (s_invalid_count >= s_params->kalman_invalid_max)
        {
            s_kalman.rate_fp = 0;
            s_kalman.P_fp    = s_params->kalman_p_max;
            s_last_rpm       = 0;
            s_confirm_count  = 0;
        }
        return;
    }

    /* --- Kalman gain and update --- */
    int32_t K = fp_div(P_predict, P_predict + s_params->kalman_r);
    s_kalman.rate_fp = s_kalman.rate_fp +
                       fp_mul(K, meas_rpm_fp - s_kalman.rate_fp);
    s_kalman.P_fp    = P_predict - fp_mul(K, P_predict);
}

/*
 * PUBLIC API
 */

uint8_t pp_stroke_rate_update(void)
{
    if (s_params == NULL)
    {
        return 0;
    }

    int32_t raw_rpm_fp = autocorr_estimate();
    bool    valid      = (raw_rpm_fp > 0);

    kalman_update(raw_rpm_fp, valid);

    int32_t rpm = (s_kalman.rate_fp + FP_ONE / 2) >> FP_SHIFT;
    if (rpm < 0)   rpm = 0;
    if (rpm > 255) rpm = 255;
    s_last_rpm = (uint8_t)rpm;
    return s_last_rpm;
}

uint8_t pp_stroke_rate_get_rpm(void)
{
    return s_last_rpm;
}

void pp_cadence_to_crank(uint8_t cadence_rpm,
                         uint16_t *revs_delta, uint16_t *time_delta)
{
    if (cadence_rpm == 0)
    {
        *revs_delta = 0;
        *time_delta = 0;
        return;
    }
    const uint16_t K = 61440;  /* 60 * 1024 */
    uint16_t g = u16_gcd(K, cadence_rpm);
    *revs_delta = cadence_rpm / g;
    *time_delta = K / g;
}
