#ifndef FILTERS_H
#define FILTERS_H

#include <stdint.h>

#define SAMPLE_STORE_CAPACITY 512
#define SAMPLE_RATE_HZ        52
#define FP_SHIFT               16
#define FP_ONE                 (1 << FP_SHIFT)

#define PROTOCOL_MAGNITUDE   1
#define PROTOCOL_SINGLE_AXIS 2

/* Q16.16 fixed-point helpers */
int32_t fp_mul(int32_t a, int32_t b);
int32_t fp_div(int32_t a, int32_t b);

/* Vector magnitude: isqrt(x^2 + y^2 + z^2) */
int16_t filters_vector_magnitude(int16_t x, int16_t y, int16_t z);

/* 2nd-order IIR biquad filter state */
typedef struct {
    int32_t x1, x2;
    int32_t y1, y2;
    int32_t b0, b1, b2;
    int32_t a1, a2;
} biquad_state_t;

/* HPF at 0.25 Hz for gravity removal (Q15 coefficients, 52 Hz sample rate) */
void filters_hpf_gravity_init(biquad_state_t *state);
int16_t filters_hpf_gravity_process(biquad_state_t *state, int16_t sample);

/* LPF at 3.0 Hz (Q15 coefficients, 52 Hz sample rate) */
void filters_lpf_init(biquad_state_t *state);
int16_t filters_lpf_process(biquad_state_t *state, int16_t sample);

/* Adaptive envelope tracker */
typedef struct {
    int32_t env_max;
    int32_t env_min;
    int32_t decay_fp;
} envelope_state_t;

void filters_envelope_init(envelope_state_t *state);
void filters_envelope_update(envelope_state_t *state, int16_t sample);

/* 2D Kalman smoother state */
typedef struct {
    int32_t rate_fp;
    int32_t accel_fp;
    int32_t P00, P01, P10, P11;
    int32_t Q_fp;
    int32_t R_fp;
} kalman2d_state_t;

void filters_kalman2d_init(kalman2d_state_t *state);
int32_t filters_kalman2d_update(kalman2d_state_t *state, int32_t meas_rpm_fp, int valid);

/* Stdin parsing helpers */
int parse_single_axis_line(char *line, int16_t *samples, int count);
int parse_magnitude_line(char *line, int16_t *x, int16_t *y, int16_t *z, int count);

#endif /* FILTERS_H */
