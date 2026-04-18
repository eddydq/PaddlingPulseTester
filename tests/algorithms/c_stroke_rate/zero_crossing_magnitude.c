#include "filters.h"
#include <stdio.h>
#include <string.h>

#define PROTOCOL PROTOCOL_MAGNITUDE
/* Hysteresis band in raw int16 units (approx 0.05g at typical scale) */
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

    /* SPM = crossings / (duration_in_minutes) */
    /* duration = 512 / 52 / 60 = 0.1641 min */
    /* SPM = count * 52 * 60 / 512 */
    return (int)((long)count * 60L * SAMPLE_RATE_HZ / SAMPLE_STORE_CAPACITY);
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

        /* Bandpass = HPF + LPF cascade */
        filters_hpf_gravity_init(&hpf);
        filters_lpf_init(&lpf);

        for (int i = 0; i < SAMPLE_STORE_CAPACITY; i++) {
            int16_t mag = filters_vector_magnitude(x[i], y[i], z[i]);
            int16_t hp = filters_hpf_gravity_process(&hpf, mag);
            filtered[i] = filters_lpf_process(&lpf, hp);
        }

        printf("%d\n", compute_spm());
        fflush(stdout);
    }
    return 0;
}
