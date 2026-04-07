#include "filters.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int32_t fp_mul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * (int64_t)b) >> FP_SHIFT);
}

int32_t fp_div(int32_t a, int32_t b)
{
    if (b == 0) return 0;
    return (int32_t)(((int64_t)a << FP_SHIFT) / (int64_t)b);
}

/* Integer square root via Newton's method */
static uint32_t isqrt32(uint32_t n)
{
    if (n == 0) return 0;
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

int16_t filters_vector_magnitude(int16_t x, int16_t y, int16_t z)
{
    uint32_t sum = (uint32_t)((int32_t)x * x + (int32_t)y * y + (int32_t)z * z);
    uint32_t mag = isqrt32(sum);
    return (int16_t)(mag > 32767 ? 32767 : mag);
}

/*
 * Biquad coefficients pre-computed for 52 Hz sample rate, Q15 format.
 * HPF 0.25 Hz: designed with bilinear transform of 2nd-order Butterworth.
 */
void filters_hpf_gravity_init(biquad_state_t *state)
{
    memset(state, 0, sizeof(*state));
    /* 0.25 Hz HPF at 52 Hz: very close to unity passthrough.
     * b = [0.9849, -1.9699, 0.9849], a = [1, -1.9698, 0.9700] in Q15 */
    state->b0 =  32272;  /* 0.9849 * 32768 */
    state->b1 = -64544;  /* -1.9699 * 32768 */
    state->b2 =  32272;
    state->a1 = -64541;  /* -1.9698 * 32768 */
    state->a2 =  31785;  /* 0.9700 * 32768 */
}

static int16_t biquad_process(biquad_state_t *s, int16_t in)
{
    int32_t x0 = (int32_t)in << 15;
    int32_t y0 = (int64_t)s->b0 * x0 / 32768
               + (int64_t)s->b1 * s->x1 / 32768
               + (int64_t)s->b2 * s->x2 / 32768
               - (int64_t)s->a1 * s->y1 / 32768
               - (int64_t)s->a2 * s->y2 / 32768;
    s->x2 = s->x1; s->x1 = x0;
    s->y2 = s->y1; s->y1 = y0;
    return (int16_t)(y0 >> 15);
}

int16_t filters_hpf_gravity_process(biquad_state_t *state, int16_t sample)
{
    return biquad_process(state, sample);
}

void filters_lpf_init(biquad_state_t *state)
{
    memset(state, 0, sizeof(*state));
    /* 3.0 Hz LPF at 52 Hz, 2nd-order Butterworth, Q15:
     * b = [0.0232, 0.0463, 0.0232], a = [1, -1.4891, 0.5817] in Q15 */
    state->b0 =   760;   /* 0.0232 * 32768 */
    state->b1 =  1517;   /* 0.0463 * 32768 */
    state->b2 =   760;
    state->a1 = -48789;  /* -1.4891 * 32768 */
    state->a2 =  19058;  /* 0.5817 * 32768 */
}

int16_t filters_lpf_process(biquad_state_t *state, int16_t sample)
{
    return biquad_process(state, sample);
}

void filters_envelope_init(envelope_state_t *state)
{
    state->env_max = 0;
    state->env_min = 0;
    state->decay_fp = 62259;  /* 0.95 * 65536 */
}

void filters_envelope_update(envelope_state_t *state, int16_t sample)
{
    int32_t v = (int32_t)sample << FP_SHIFT;
    /* 1.0 - 0.95 = 0.05 => 3277 in Q16.16 */
    int32_t one_minus_decay = 3277;
    int32_t tracked_max = fp_mul(state->env_max, state->decay_fp) + fp_mul(v, one_minus_decay);
    int32_t tracked_min = fp_mul(state->env_min, state->decay_fp) + fp_mul(v, one_minus_decay);
    state->env_max = (v > tracked_max) ? v : tracked_max;
    state->env_min = (v < tracked_min) ? v : tracked_min;
}

void filters_kalman2d_init(kalman2d_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->P00 = 1000 * FP_ONE;
    state->P11 = 1000 * FP_ONE;
    state->Q_fp = 4 * FP_ONE;
    state->R_fp = 2 * FP_ONE;
}

int32_t filters_kalman2d_update(kalman2d_state_t *state, int32_t meas_rpm_fp, int valid)
{
    /* Predict: rate += accel, accel unchanged */
    int32_t pred_rate = state->rate_fp + state->accel_fp;
    int32_t P00 = state->P00 + state->P10 + state->P01 + state->P11 + state->Q_fp;
    int32_t P01 = state->P01 + state->P11;
    int32_t P10 = state->P10 + state->P11;
    int32_t P11 = state->P11 + state->Q_fp;

    if (valid && meas_rpm_fp > 0) {
        int32_t S = P00 + state->R_fp;
        if (S > 0) {
            int32_t K0 = fp_div(P00, S);
            int32_t K1 = fp_div(P10, S);
            int32_t innov = meas_rpm_fp - pred_rate;
            state->rate_fp = pred_rate + fp_mul(K0, innov);
            state->accel_fp = state->accel_fp + fp_mul(K1, innov);
            state->P00 = P00 - fp_mul(K0, P00);
            state->P01 = P01 - fp_mul(K0, P01);
            state->P10 = P10 - fp_mul(K1, P00);
            state->P11 = P11 - fp_mul(K1, P01);
        } else {
            state->rate_fp = pred_rate;
            state->P00 = P00; state->P01 = P01;
            state->P10 = P10; state->P11 = P11;
        }
    } else {
        state->rate_fp = pred_rate;
        state->P00 = P00; state->P01 = P01;
        state->P10 = P10; state->P11 = P11;
    }

    return state->rate_fp > 0 ? state->rate_fp : 0;
}

/* --- Stdin parsing --- */

static int parse_samples(char *line, int16_t *out, int count)
{
    char *cursor = line;
    int parsed = 0;

    while (*cursor && isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '\0') return 0;

    while (parsed < count) {
        char *endptr = NULL;
        long value = strtol(cursor, &endptr, 10);
        if (cursor == endptr) {
            fprintf(stderr, "Parse error at sample %d\n", parsed + 1);
            exit(1);
        }
        if (value < -32768L || value > 32767L) {
            fprintf(stderr, "Sample out of int16 range\n");
            exit(1);
        }
        out[parsed++] = (int16_t)value;
        cursor = endptr;
    }
    return 1;
}

int parse_single_axis_line(char *line, int16_t *samples, int count)
{
    return parse_samples(line, samples, count);
}

int parse_magnitude_line(char *line, int16_t *x, int16_t *y, int16_t *z, int count)
{
    int16_t buf[SAMPLE_STORE_CAPACITY * 3];
    if (!parse_samples(line, buf, count * 3)) return 0;
    memcpy(x, buf, count * sizeof(int16_t));
    memcpy(y, buf + count, count * sizeof(int16_t));
    memcpy(z, buf + 2 * count, count * sizeof(int16_t));
    return 1;
}
