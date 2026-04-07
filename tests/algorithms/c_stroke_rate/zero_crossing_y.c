#include "filters.h"
#include <stdio.h>
#include <string.h>

#define PROTOCOL PROTOCOL_SINGLE_AXIS
#define HYSTERESIS_BAND 50

static int16_t filtered[SAMPLE_STORE_CAPACITY];

static int compute_spm(void)
{
    int count = 0;
    int armed = 0;

    for (int i = 0; i < SAMPLE_STORE_CAPACITY; i++) {
        if (!armed && filtered[i] < -HYSTERESIS_BAND) {
            armed = 1;
        } else if (armed && filtered[i] > HYSTERESIS_BAND) {
            count++;
            armed = 0;
        }
    }

    if (count < 2) return 0;
    return (int)((long)count * 60L * SAMPLE_RATE_HZ / SAMPLE_STORE_CAPACITY);
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
