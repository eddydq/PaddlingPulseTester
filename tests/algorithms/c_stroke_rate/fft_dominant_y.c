#include "filters.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PROTOCOL PROTOCOL_SINGLE_AXIS
#define FFT_SIZE SAMPLE_STORE_CAPACITY
#define MIN_BIN  ((int)(0.33 * FFT_SIZE / SAMPLE_RATE_HZ))
#define MAX_BIN  ((int)(2.0 * FFT_SIZE / SAMPLE_RATE_HZ + 1))
#define TWO_PI   6.283185307179586

static int16_t filtered[SAMPLE_STORE_CAPACITY];

static int compute_spm(void)
{
    int best_bin = MIN_BIN;
    int64_t best_power = 0;
    int64_t neighbor_power[2] = {0, 0};

    int32_t sum = 0;
    for (int i = 0; i < FFT_SIZE; i++) sum += filtered[i];
    int16_t mean = (int16_t)(sum / FFT_SIZE);

    for (int bin = MIN_BIN; bin <= MAX_BIN && bin < FFT_SIZE / 2; bin++) {
        double coeff = 2.0 * cos(TWO_PI * bin / FFT_SIZE);
        int32_t coeff_q14 = (int32_t)(coeff * 16384.0);
        int64_t s0 = 0, s1 = 0, s2 = 0;
        for (int n = 0; n < FFT_SIZE; n++) {
            s0 = (int64_t)(filtered[n] - mean) + ((int64_t)coeff_q14 * s1 >> 14) - s2;
            s2 = s1;
            s1 = s0;
        }
        int64_t power = s1 * s1 + s2 * s2 - ((coeff_q14 * s1 >> 14) * s2);
        if (power > best_power) {
            best_power = power;
            best_bin = bin;
        }
    }

    if (best_power <= 0 || best_bin < MIN_BIN) return 0;

    for (int d = -1; d <= 1; d += 2) {
        int nb = best_bin + d;
        if (nb < MIN_BIN || nb > MAX_BIN || nb >= FFT_SIZE / 2) {
            neighbor_power[(d + 1) / 2] = best_power;
            continue;
        }
        double coeff = 2.0 * cos(TWO_PI * nb / FFT_SIZE);
        int32_t coeff_q14 = (int32_t)(coeff * 16384.0);
        int64_t s0 = 0, s1 = 0, s2 = 0;
        for (int n = 0; n < FFT_SIZE; n++) {
            s0 = (int64_t)(filtered[n] - mean) + ((int64_t)coeff_q14 * s1 >> 14) - s2;
            s2 = s1;
            s1 = s0;
        }
        neighbor_power[(d + 1) / 2] = s1 * s1 + s2 * s2 - ((coeff_q14 * s1 >> 14) * s2);
    }

    double alpha = (double)neighbor_power[0];
    double beta  = (double)best_power;
    double gamma = (double)neighbor_power[1];
    double denom = alpha - 2.0 * beta + gamma;
    double refined_bin = (double)best_bin;
    if (fabs(denom) > 1e-12)
        refined_bin += 0.5 * (alpha - gamma) / denom;

    double freq_hz = refined_bin * SAMPLE_RATE_HZ / FFT_SIZE;
    int spm = (int)(freq_hz * 60.0 + 0.5);
    if (spm < 20 || spm > 120) return 0;
    return spm;
}

int main(void)
{
    char line[8192];
    int16_t samples[SAMPLE_STORE_CAPACITY];
    biquad_state_t hpf;

    setvbuf(stdout, NULL, _IOLBF, 0);

    while (fgets(line, sizeof(line), stdin) != NULL) {
        if (!parse_single_axis_line(line, samples, SAMPLE_STORE_CAPACITY))
            continue;
        filters_hpf_gravity_init(&hpf);
        for (int i = 0; i < SAMPLE_STORE_CAPACITY; i++)
            filtered[i] = filters_hpf_gravity_process(&hpf, samples[i]);
        printf("%d\n", compute_spm());
        fflush(stdout);
    }
    return 0;
}
