#include "filters.h"
#include <stdio.h>
#include <string.h>

#define PROTOCOL PROTOCOL_SINGLE_AXIS
#define MIN_PEAK_INTERVAL ((SAMPLE_RATE_HZ * 60) / 120)
#define MAX_PEAK_INTERVAL ((SAMPLE_RATE_HZ * 60) / 20)

static int16_t filtered[SAMPLE_STORE_CAPACITY];

static int compute_spm(void)
{
    int peaks[64];
    int peak_count = 0;

    int32_t env_maxs[SAMPLE_STORE_CAPACITY];
    int32_t env_mins[SAMPLE_STORE_CAPACITY];
    {
        envelope_state_t env_pass;
        filters_envelope_init(&env_pass);
        for (int i = 0; i < SAMPLE_STORE_CAPACITY; i++) {
            filters_envelope_update(&env_pass, filtered[i]);
            env_maxs[i] = env_pass.env_max;
            env_mins[i] = env_pass.env_min;
        }
    }

    for (int i = 1; i < SAMPLE_STORE_CAPACITY - 1 && peak_count < 64; i++) {
        int32_t v = (int32_t)filtered[i] << FP_SHIFT;
        int32_t dynamic_range = env_maxs[i] - env_mins[i];
        if (dynamic_range <= 0) continue;
        int32_t threshold = env_mins[i] + fp_mul(dynamic_range, 39322);
        if (v > threshold
            && filtered[i] >= filtered[i - 1]
            && filtered[i] >= filtered[i + 1]) {
            if (peak_count == 0 || (i - peaks[peak_count - 1]) >= MIN_PEAK_INTERVAL) {
                peaks[peak_count++] = i;
            }
        }
    }

    if (peak_count < 2) return 0;

    int valid_intervals = 0;
    int32_t interval_sum = 0;
    for (int i = 1; i < peak_count; i++) {
        int interval = peaks[i] - peaks[i - 1];
        if (interval >= MIN_PEAK_INTERVAL && interval <= MAX_PEAK_INTERVAL) {
            interval_sum += interval;
            valid_intervals++;
        }
    }
    if (valid_intervals == 0) return 0;

    int32_t mean_interval_fp = fp_div(interval_sum * FP_ONE, valid_intervals * FP_ONE);
    if (mean_interval_fp <= 0) return 0;
    int32_t spm_fp = fp_div((int32_t)(60 * SAMPLE_RATE_HZ) * FP_ONE, mean_interval_fp);
    return (int)((spm_fp + FP_ONE / 2) >> FP_SHIFT);
}

int main(void)
{
    char line[8192];
    int16_t samples[SAMPLE_STORE_CAPACITY];
    biquad_state_t hpf, lpf;

    setvbuf(stdout, NULL, _IOLBF, 0);

    while (fgets(line, sizeof(line), stdin) != NULL) {
        if (!parse_single_axis_line(line, samples, SAMPLE_STORE_CAPACITY))
            continue;

        filters_hpf_gravity_init(&hpf);
        filters_lpf_init(&lpf);

        for (int i = 0; i < SAMPLE_STORE_CAPACITY; i++) {
            int16_t hp = filters_hpf_gravity_process(&hpf, samples[i]);
            filtered[i] = filters_lpf_process(&lpf, hp);
        }

        printf("%d\n", compute_spm());
        fflush(stdout);
    }
    return 0;
}
