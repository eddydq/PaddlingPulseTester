#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PP_SAMPLE_STORE_CAPACITY          512
#define PP_STROKE_RATE_MIN_RPM            30
#define PP_STROKE_RATE_MAX_RPM            200
#define PP_STROKE_RATE_WINDOW             512
#define PP_KALMAN_Q                       262144
#define PP_KALMAN_R                       131072
#define PP_KALMAN_P_MAX                   655360000
#define PP_AUTOCORR_CONFIDENCE_MIN        19661
#define PP_AUTOCORR_HARMONIC_PCT          80
#define PP_AUTOCORR_ENERGY_MIN            5000
#define PP_KALMAN_CONFIRM_TOLERANCE_RPM   15
#define PP_KALMAN_INVALID_MAX             3
#define PP_KALMAN_MAX_JUMP_RPM            20
#define PP_KALMAN_CONFIRM_COUNT           3
#define SAMPLE_RATE_HZ                    52

#define FP_SHIFT 16
#define FP_ONE   (1 << FP_SHIFT)

static struct {
    int16_t  samples[PP_SAMPLE_STORE_CAPACITY];
    uint16_t wr_idx;
    uint16_t count;
    uint16_t sample_rate_hz;
} s_store;

static struct {
    int32_t rate_fp;
    int32_t P_fp;
} s_kalman;

static uint8_t s_last_rpm;
static uint8_t s_invalid_count;
static uint8_t s_confirm_count;
static int32_t s_confirm_rpm_fp;

static int32_t fp_mul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * (int64_t)b) >> FP_SHIFT);
}

static int32_t fp_div(int32_t a, int32_t b)
{
    if (b == 0)
    {
        return 0;
    }
    return (int32_t)(((int64_t)a << FP_SHIFT) / (int64_t)b);
}

static void pp_sample_store_init(uint16_t sample_rate_hz)
{
    memset(&s_store, 0, sizeof(s_store));
    s_store.sample_rate_hz = sample_rate_hz;
}

static void pp_sample_store_push(int16_t sample)
{
    s_store.samples[s_store.wr_idx] = sample;
    s_store.wr_idx = (uint16_t)((s_store.wr_idx + 1U) % PP_SAMPLE_STORE_CAPACITY);
    if (s_store.count < PP_SAMPLE_STORE_CAPACITY)
    {
        s_store.count++;
    }
}

static int16_t pp_sample_store_get(uint16_t ordered_index)
{
    uint16_t oldest;
    uint16_t idx;

    if (ordered_index >= s_store.count)
    {
        return 0;
    }

    oldest = (uint16_t)((s_store.wr_idx + PP_SAMPLE_STORE_CAPACITY - s_store.count)
                        % PP_SAMPLE_STORE_CAPACITY);
    idx = (uint16_t)((oldest + ordered_index) % PP_SAMPLE_STORE_CAPACITY);
    return s_store.samples[idx];
}

static uint16_t pp_sample_store_get_count(void)
{
    return s_store.count;
}

static uint16_t pp_sample_store_get_rate_hz(void)
{
    return s_store.sample_rate_hz;
}

static void pp_stroke_rate_init(void)
{
    s_kalman.rate_fp = 0;
    s_kalman.P_fp = PP_KALMAN_P_MAX;
    s_last_rpm = 0;
    s_invalid_count = 0;
    s_confirm_count = 0;
    s_confirm_rpm_fp = 0;
}

static int32_t autocorr_estimate(void)
{
    uint16_t count = pp_sample_store_get_count();
    uint16_t rate_hz = pp_sample_store_get_rate_hz();
    uint16_t window = (count < PP_STROKE_RATE_WINDOW) ? count : PP_STROKE_RATE_WINDOW;
    uint16_t offset;
    int32_t sum = 0;
    uint16_t i;
    uint16_t min_lag;
    uint16_t max_lag;
    int32_t energy = 0;
    int32_t best_val = -0x7FFFFFFF;
    uint16_t best_lag;
    int32_t best_prev = 0;
    int32_t prev_acc = 0;
    uint16_t lag;
    int16_t mean;
    int32_t confidence;

    if (window < 64 || rate_hz == 0)
    {
        return 0;
    }

    offset = (uint16_t)(count - window);

    for (i = 0; i < window; i++)
    {
        sum += pp_sample_store_get((uint16_t)(offset + i));
    }
    mean = (int16_t)(sum / (int32_t)window);

    min_lag = (uint16_t)((60UL * rate_hz) / PP_STROKE_RATE_MAX_RPM);
    max_lag = (uint16_t)((60UL * rate_hz) / PP_STROKE_RATE_MIN_RPM);
    if (max_lag >= window)
    {
        max_lag = (uint16_t)(window - 1U);
    }
    if (min_lag < 1U)
    {
        min_lag = 1U;
    }
    if (min_lag >= max_lag)
    {
        return 0;
    }

    for (i = 0; i < window; i++)
    {
        int32_t d = (int32_t)pp_sample_store_get((uint16_t)(offset + i)) - mean;
        energy += (d * d) >> 8;
    }
    if (energy < PP_AUTOCORR_ENERGY_MIN)
    {
        return 0;
    }

    best_lag = min_lag;
    for (lag = min_lag; lag <= max_lag; lag++)
    {
        int32_t acc = 0;
        uint16_t n = (uint16_t)(window - lag);
        for (i = 0; i < n; i++)
        {
            int32_t a = (int32_t)pp_sample_store_get((uint16_t)(offset + i)) - mean;
            int32_t b = (int32_t)pp_sample_store_get((uint16_t)(offset + i + lag)) - mean;
            acc += (a * b) >> 8;
        }
        if (acc > best_val)
        {
            best_val = acc;
            best_lag = lag;
            best_prev = prev_acc;
        }
        prev_acc = acc;
    }

    confidence = fp_div(best_val, energy);
    if (confidence < PP_AUTOCORR_CONFIDENCE_MIN)
    {
        return 0;
    }

    if (best_lag == min_lag)
    {
        return 0;
    }

    {
        uint16_t half_lag = (uint16_t)(best_lag / 2U);
        if (half_lag >= min_lag)
        {
            int32_t half_acc = 0;
            uint16_t hn = (uint16_t)(window - half_lag);
            for (i = 0; i < hn; i++)
            {
                int32_t a = (int32_t)pp_sample_store_get((uint16_t)(offset + i)) - mean;
                int32_t b = (int32_t)pp_sample_store_get((uint16_t)(offset + i + half_lag)) - mean;
                half_acc += (a * b) >> 8;
            }
            if (half_acc * 100 >= best_val * PP_AUTOCORR_HARMONIC_PCT)
            {
                int32_t numerator = (int32_t)(60UL * rate_hz) * FP_ONE;
                return fp_div(numerator, (int32_t)half_lag * FP_ONE);
            }
        }
    }

    {
        int32_t best_next = best_val;
        int32_t refined_lag_fp;
        int32_t denom;

        if (best_lag < max_lag)
        {
            uint16_t nn = (uint16_t)(window - (best_lag + 1U));
            best_next = 0;
            for (i = 0; i < nn; i++)
            {
                int32_t a = (int32_t)pp_sample_store_get((uint16_t)(offset + i)) - mean;
                int32_t b = (int32_t)pp_sample_store_get((uint16_t)(offset + i + best_lag + 1U)) - mean;
                best_next += (a * b) >> 8;
            }
        }

        denom = 2 * (best_prev - 2 * best_val + best_next);
        if (denom != 0 && best_lag > min_lag && best_lag < max_lag)
        {
            int32_t delta_fp = fp_div(best_prev - best_next, denom);
            refined_lag_fp = (int32_t)best_lag * FP_ONE + delta_fp;
        }
        else
        {
            refined_lag_fp = (int32_t)best_lag * FP_ONE;
        }

        if (refined_lag_fp <= 0)
        {
            return 0;
        }

        {
            int32_t numerator = (int32_t)(60UL * rate_hz) * FP_ONE;
            return fp_div(numerator, refined_lag_fp);
        }
    }
}

static void kalman_update(int32_t meas_rpm_fp, bool valid)
{
    int32_t P_predict = s_kalman.P_fp + PP_KALMAN_Q;

    if (P_predict > PP_KALMAN_P_MAX)
    {
        P_predict = PP_KALMAN_P_MAX;
    }

    if (!valid)
    {
        s_invalid_count++;
        if (s_invalid_count >= PP_KALMAN_INVALID_MAX)
        {
            s_kalman.rate_fp = 0;
            s_kalman.P_fp = PP_KALMAN_P_MAX;
            s_last_rpm = 0;
            s_confirm_count = 0;
        }
        return;
    }

    s_invalid_count = 0;

    if (s_kalman.rate_fp == 0)
    {
        int32_t tolerance_fp = (int32_t)PP_KALMAN_CONFIRM_TOLERANCE_RPM * FP_ONE;
        if (s_confirm_count == 0)
        {
            s_confirm_rpm_fp = meas_rpm_fp;
            s_confirm_count = 1;
            return;
        }

        {
            int32_t diff = meas_rpm_fp - s_confirm_rpm_fp;
            if (diff < 0)
            {
                diff = -diff;
            }
            if (diff > tolerance_fp)
            {
                s_confirm_rpm_fp = meas_rpm_fp;
                s_confirm_count = 1;
                return;
            }
        }

        s_confirm_count++;
        if (s_confirm_count >= PP_KALMAN_CONFIRM_COUNT)
        {
            s_kalman.rate_fp = meas_rpm_fp;
            s_kalman.P_fp = PP_KALMAN_R;
            s_confirm_count = 0;
        }
        return;
    }

    {
        int32_t max_jump_fp = (int32_t)PP_KALMAN_MAX_JUMP_RPM * FP_ONE;
        int32_t innovation = meas_rpm_fp - s_kalman.rate_fp;
        if (innovation < 0)
        {
            innovation = -innovation;
        }

        if (innovation > max_jump_fp)
        {
            s_invalid_count++;
            if (s_invalid_count >= PP_KALMAN_INVALID_MAX)
            {
                s_kalman.rate_fp = 0;
                s_kalman.P_fp = PP_KALMAN_P_MAX;
                s_last_rpm = 0;
                s_confirm_count = 0;
            }
            return;
        }
    }

    {
        int32_t K = fp_div(P_predict, P_predict + PP_KALMAN_R);
        s_kalman.rate_fp = s_kalman.rate_fp +
                           fp_mul(K, meas_rpm_fp - s_kalman.rate_fp);
        s_kalman.P_fp = P_predict - fp_mul(K, P_predict);
    }
}

static uint8_t pp_stroke_rate_update(void)
{
    int32_t raw_rpm_fp = autocorr_estimate();
    bool valid = (raw_rpm_fp > 0);
    int32_t rpm;

    kalman_update(raw_rpm_fp, valid);

    rpm = (s_kalman.rate_fp + FP_ONE / 2) >> FP_SHIFT;
    if (rpm < 0)
    {
        rpm = 0;
    }
    if (rpm > 255)
    {
        rpm = 255;
    }
    s_last_rpm = (uint8_t)rpm;
    return s_last_rpm;
}

static bool parse_sample_line(char *line, int16_t *samples, size_t sample_count)
{
    char *cursor = line;
    size_t parsed = 0;

    while (*cursor != '\0' && isspace((unsigned char)*cursor))
    {
        cursor++;
    }

    if (*cursor == '\0')
    {
        return false;
    }

    while (parsed < sample_count)
    {
        char *endptr = NULL;
        long value;

        value = strtol(cursor, &endptr, 10);
        if (cursor == endptr)
        {
            fprintf(stderr, "Failed to parse integer %u on input line.\n", (unsigned)(parsed + 1U));
            exit(1);
        }
        if (value < -32768L || value > 32767L)
        {
            fprintf(stderr, "Input sample out of int16 range.\n");
            exit(1);
        }

        samples[parsed] = (int16_t)value;
        parsed++;
        cursor = endptr;
    }

    while (*cursor != '\0' && isspace((unsigned char)*cursor))
    {
        cursor++;
    }

    if (*cursor != '\0')
    {
        fprintf(stderr, "Unexpected trailing data after %u samples.\n", (unsigned)sample_count);
        exit(1);
    }

    return true;
}

int main(void)
{
    char line[8192];
    int16_t samples[PP_SAMPLE_STORE_CAPACITY];
    size_t i;

    pp_sample_store_init(SAMPLE_RATE_HZ);
    pp_stroke_rate_init();
    setvbuf(stdout, NULL, _IOLBF, 0);

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        if (!parse_sample_line(line, samples, PP_SAMPLE_STORE_CAPACITY))
        {
            continue;
        }

        for (i = 0; i < PP_SAMPLE_STORE_CAPACITY; i++)
        {
            pp_sample_store_push(samples[i]);
        }

        printf("%u\n", (unsigned)pp_stroke_rate_update());
        fflush(stdout);
    }

    return 0;
}
