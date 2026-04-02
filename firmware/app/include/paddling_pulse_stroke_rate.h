/**
 ****************************************************************************************
 * @file paddling_pulse_stroke_rate.h
 * @brief Stroke rate estimation via autocorrelation + Kalman filter (Q16.16).
 ****************************************************************************************
 */

#ifndef _PADDLING_PULSE_STROKE_RATE_H_
#define _PADDLING_PULSE_STROKE_RATE_H_

#include <stdint.h>

/* Tunable parameters — override in da14531_config_basic.h if needed */
#ifndef PP_STROKE_RATE_INTERVAL_MS
#define PP_STROKE_RATE_INTERVAL_MS          1000        /* Algorithm update period (ms) */
#endif
#ifndef PP_STROKE_RATE_MIN_RPM
#define PP_STROKE_RATE_MIN_RPM              30
#endif
#ifndef PP_STROKE_RATE_MAX_RPM
#define PP_STROKE_RATE_MAX_RPM              200
#endif
#ifndef PP_STROKE_RATE_WINDOW
#define PP_STROKE_RATE_WINDOW               512
#endif
#ifndef PP_KALMAN_Q
#define PP_KALMAN_Q                         262144      /* 4.0  Q16.16 */
#endif
#ifndef PP_KALMAN_R
#define PP_KALMAN_R                         131072      /* 2.0  Q16.16 */
#endif
#ifndef PP_KALMAN_P_MAX
#define PP_KALMAN_P_MAX                     655360000   /* 10000.0 Q16.16 */
#endif
#ifndef PP_AUTOCORR_CONFIDENCE_MIN
#define PP_AUTOCORR_CONFIDENCE_MIN          19661       /* 0.3  Q16.16 */
#endif
#ifndef PP_AUTOCORR_HARMONIC_PCT
#define PP_AUTOCORR_HARMONIC_PCT            80
#endif
#ifndef PP_AUTOCORR_ENERGY_MIN
#define PP_AUTOCORR_ENERGY_MIN              5000
#endif
#ifndef PP_KALMAN_CONFIRM_TOLERANCE_RPM
#define PP_KALMAN_CONFIRM_TOLERANCE_RPM     15
#endif
#ifndef PP_KALMAN_INVALID_MAX
#define PP_KALMAN_INVALID_MAX               3
#endif
#ifndef PP_KALMAN_MAX_JUMP_RPM
#define PP_KALMAN_MAX_JUMP_RPM              20
#endif
#ifndef PP_KALMAN_CONFIRM_COUNT
#define PP_KALMAN_CONFIRM_COUNT             3
#endif

/** @brief Reset Kalman state and counters. Call before first use. */
void pp_stroke_rate_init(void);

/**
 * @brief Run autocorrelation + Kalman update on current sample store.
 * @return Filtered cadence in RPM (0 = no valid measurement, max 255).
 */
uint8_t pp_stroke_rate_update(void);

/** @brief Last computed RPM without re-running the algorithm. */
uint8_t pp_stroke_rate_get_rpm(void);

/**
 * @brief Convert cadence RPM to CSCP crank revolution deltas.
 * @param cadence_rpm  Cadence in RPM (0-255).
 * @param revs_delta   Output: cumulative revolution increment.
 * @param time_delta   Output: last crank event time increment (1/1024 s units).
 */
void pp_cadence_to_crank(uint8_t cadence_rpm,
                         uint16_t *revs_delta,
                         uint16_t *time_delta);

#endif // _PADDLING_PULSE_STROKE_RATE_H_
