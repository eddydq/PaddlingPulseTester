#include "filters.h"
#include <stdio.h>
#include <string.h>

#define PROTOCOL PROTOCOL_MAGNITUDE
#define MIN_PEAK_INTERVAL ((SAMPLE_RATE_HZ * 60) / 120)
#define MAX_PEAK_INTERVAL ((SAMPLE_RATE_HZ * 60) / 20)
/* 0.6 and 0.3 in Q16.16 */
#define HIGH_FRAC 39322
#define LOW_FRAC  19661

static int16_t filtered[SAMPLE_STORE_CAPACITY];

static int compute_spm(void)
{
    int triggers[64];
    int trig_count = 0;
    int armed = 0;

    int32_t env_maxs[SAMPLE_STORE_CAPACITY];
    int32_t env_mins[SAMPLE_STORE_CAPACITY];
    {
        envelope_state_t env;
        filters_envelope_init(&env);
        for (int i = 0; i < SAMPLE_STORE_CAPACITY; i++) {
            filters_envelope_update(&env, filtered[i]);
            env_maxs[i] = env.env_max;
            env_mins[i] = env.env_min;
        }
    }

    for (int i = 0; i < SAMPLE_STORE_CAPACITY && trig_count < 64; i++) {
        int32_t v = (int32_t)filtered[i] << FP_SHIFT;
        int32_t range = env_maxs[i] - env_mins[i];
        if (range <= 0) continue;
        int32_t high_thresh = env_mins[i] + fp_mul(range, HIGH_FRAC);
        int32_t low_thresh = env_mins[i] + fp_mul(range, LOW_FRAC);
        if (!armed && v >= high_thresh) {
            armed = 1;
            if (trig_count == 0 || (i - triggers[trig_count - 1]) >= MIN_PEAK_INTERVAL)
                triggers[trig_count++] = i;
        } else if (armed && v <= low_thresh) {
            armed = 0;
        }
    }

    if (trig_count < 2) return 0;

    int valid = 0;
    int32_t sum = 0;
    for (int i = 1; i < trig_count; i++) {
        int interval = triggers[i] - triggers[i - 1];
        if (interval >= MIN_PEAK_INTERVAL && interval <= MAX_PEAK_INTERVAL) {
            sum += interval;
            valid++;
        }
    }
    if (valid == 0) return 0;

    int32_t mean_fp = fp_div(sum * FP_ONE, valid * FP_ONE);
    if (mean_fp <= 0) return 0;
    int32_t spm_fp = fp_div((int32_t)(60 * SAMPLE_RATE_HZ) * FP_ONE, mean_fp);
    return (int)((spm_fp + FP_ONE / 2) >> FP_SHIFT);
}

int main(void)
{
    char line[32768];
    int16_t x[SAMPLE_STORE_CAPACITY], y[SAMPLE_STORE_CAPACITY], z[SAMPLE_STORE_CAPACITY];
    biquad_state_t hpf, lpf;

    setvbuf(stdout, NULL, _IOLBF, 0);

    while (fgets(line, sizeof(line), stdin) != NULL) {
        if (!parse_magnitude_line(line, x, y, z, SAMPLE_STORE_CAPACITY))
            continue;
        filters_hpf_gravity_init(&hpf);
        filters_lpf_init(&lpf);
        for (int i = 0; i < SAMPLE_STORE_CAPACITY; i++) {
            int16_t mag = filters_vector_magnitude(x[i], y[i], z[i]);
            filtered[i] = filters_lpf_process(&lpf, filters_hpf_gravity_process(&hpf, mag));
        }
        printf("%d\n", compute_spm());
        fflush(stdout);
    }
    return 0;
}
