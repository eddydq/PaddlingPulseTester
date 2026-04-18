#include "pp_block.h"

static pp_block_result_t pp_result(uint8_t status)
{
    pp_block_result_t result = { .status = status };
    return result;
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static int validate_series_io(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    return inputs && outputs &&
           num_inputs == 1 &&
           num_outputs == 1 &&
           inputs[0].data &&
           outputs[0].data &&
           inputs[0].kind == PP_KIND_SERIES &&
           inputs[0].length > 2 &&
           inputs[0].sample_rate_hz > 0;
}

static int32_t abs32(int32_t value)
{
    return value < 0 ? -value : value;
}

static int16_t estimate_spm_by_period(
    const int16_t *series,
    uint16_t length,
    uint16_t sample_rate_hz,
    uint16_t min_lag,
    uint16_t max_lag,
    uint8_t confidence_min,
    int16_t *confidence_out)
{
    uint16_t i;
    uint16_t lag;
    int32_t sum = 0;
    int32_t energy = 0;
    int32_t best_val = -2147483647L;
    uint16_t best_lag = 0;
    int16_t mean;

    if (min_lag < 1) {
        min_lag = 1;
    }
    if (max_lag >= length) {
        max_lag = (uint16_t)(length - 1U);
    }
    if (min_lag >= max_lag) {
        return 0;
    }

    for (i = 0; i < length; i++) {
        sum += series[i];
    }
    mean = (int16_t)(sum / (int32_t)length);

    for (i = 0; i < length; i++) {
        int32_t d = (int32_t)series[i] - mean;
        energy += (d * d) >> 8;
    }
    if (energy <= 0) {
        return 0;
    }

    for (lag = min_lag; lag <= max_lag; lag++) {
        int32_t acc = 0;
        uint16_t n = (uint16_t)(length - lag);
        for (i = 0; i < n; i++) {
            int32_t a = (int32_t)series[i] - mean;
            int32_t b = (int32_t)series[(uint16_t)(i + lag)] - mean;
            acc += (a * b) >> 8;
        }
        if (acc > best_val) {
            best_val = acc;
            best_lag = lag;
        }
    }

    if (best_lag == 0 || best_val <= 0) {
        if (confidence_out) {
            *confidence_out = 0;
        }
        return 0;
    }

    {
        int32_t confidence = (best_val * 100L) / energy;
        if (confidence < 0) {
            confidence = 0;
        }
        if (confidence > 100) {
            confidence = 100;
        }
        if (confidence_out) {
            *confidence_out = (int16_t)confidence;
        }
        if (confidence < confidence_min) {
            return 0;
        }
    }

    return (int16_t)(((uint32_t)60U * sample_rate_hz + (best_lag / 2U)) / best_lag);
}

pp_block_result_t pp_autocorrelation_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    uint16_t min_lag;
    uint16_t max_lag;
    uint8_t confidence_min;
    int16_t confidence = 0;
    int16_t spm;

    (void)state;

    if (!validate_series_io(inputs, num_inputs, outputs, num_outputs) || !params || params_len < 6) {
        return pp_result(PP_ERR);
    }

    min_lag = read_u16_le(&params[0]);
    max_lag = read_u16_le(&params[2]);
    confidence_min = params[4];
    (void)params[5];

    spm = estimate_spm_by_period(
        inputs[0].data,
        inputs[0].length,
        inputs[0].sample_rate_hz,
        min_lag,
        max_lag,
        confidence_min,
        &confidence);

    outputs[0].data[0] = spm;
    outputs[0].data[1] = confidence;
    outputs[0].length = 2;
    outputs[0].kind = PP_KIND_CANDIDATE;
    outputs[0].axis = inputs[0].axis;
    outputs[0].sample_rate_hz = inputs[0].sample_rate_hz;

    return pp_result(PP_OK);
}

pp_block_result_t pp_fft_dominant_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    uint8_t min_hz;
    uint8_t max_hz;
    uint16_t min_lag;
    uint16_t max_lag;
    int16_t confidence = 0;
    int16_t spm;

    (void)state;

    if (!validate_series_io(inputs, num_inputs, outputs, num_outputs) || !params || params_len < 3) {
        return pp_result(PP_ERR);
    }

    min_hz = params[0];
    max_hz = params[1] ? params[1] : 5;
    (void)params[2];

    if (max_hz == 0) {
        max_hz = 1;
    }
    min_lag = (uint16_t)(inputs[0].sample_rate_hz / max_hz);
    if (min_lag < 1) {
        min_lag = 1;
    }
    if (min_hz == 0) {
        max_lag = (uint16_t)(inputs[0].sample_rate_hz * 2U);
    } else {
        max_lag = (uint16_t)(inputs[0].sample_rate_hz / min_hz);
    }
    if (max_lag >= inputs[0].length) {
        max_lag = (uint16_t)(inputs[0].length - 1U);
    }

    spm = estimate_spm_by_period(
        inputs[0].data,
        inputs[0].length,
        inputs[0].sample_rate_hz,
        min_lag,
        max_lag,
        0,
        &confidence);

    if (spm == 0) {
        int32_t max_abs = 0;
        uint16_t i;
        for (i = 0; i < inputs[0].length; i++) {
            int32_t v = abs32(inputs[0].data[i]);
            if (v > max_abs) {
                max_abs = v;
            }
        }
        confidence = max_abs > 0 ? 1 : 0;
    }

    outputs[0].data[0] = spm;
    outputs[0].data[1] = confidence;
    outputs[0].length = 2;
    outputs[0].kind = PP_KIND_CANDIDATE;
    outputs[0].axis = inputs[0].axis;
    outputs[0].sample_rate_hz = inputs[0].sample_rate_hz;

    return pp_result(PP_OK);
}
