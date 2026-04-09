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

static int16_t read_i16_le(const uint8_t *data)
{
    return (int16_t)read_u16_le(data);
}

static int32_t abs32(int32_t value)
{
    return value < 0 ? -value : value;
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
           inputs[0].length > 1 &&
           inputs[0].sample_rate_hz > 0;
}

static int16_t spm_from_intervals(uint32_t interval_sum, uint16_t interval_count, uint16_t sample_rate_hz)
{
    if (interval_sum == 0 || interval_count == 0 || sample_rate_hz == 0) {
        return 0;
    }
    return (int16_t)(((uint32_t)60U * sample_rate_hz * interval_count + interval_sum / 2U) / interval_sum);
}

pp_block_result_t pp_adaptive_peak_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    uint8_t threshold_factor;
    uint16_t min_distance;
    int32_t mean_abs = 0;
    int32_t threshold;
    uint16_t i;
    uint16_t peak_count = 0;
    uint16_t interval_count = 0;
    uint16_t last_peak = 0;
    uint32_t interval_sum = 0;

    (void)state;

    if (!validate_series_io(inputs, num_inputs, outputs, num_outputs) || !params || params_len < 4) {
        return pp_result(PP_ERR);
    }

    threshold_factor = params[0];
    min_distance = read_u16_le(&params[1]);
    if (min_distance == 0) {
        min_distance = 1;
    }
    (void)params[3];

    for (i = 0; i < inputs[0].length; i++) {
        mean_abs += abs32(inputs[0].data[i]);
    }
    mean_abs /= inputs[0].length;
    threshold = (mean_abs * threshold_factor) / 16;

    for (i = 1; i + 1U < inputs[0].length; i++) {
        int16_t prev = inputs[0].data[(uint16_t)(i - 1U)];
        int16_t curr = inputs[0].data[i];
        int16_t next = inputs[0].data[(uint16_t)(i + 1U)];
        if (curr > threshold && curr > prev && curr >= next) {
            if (peak_count == 0 || (uint16_t)(i - last_peak) >= min_distance) {
                if (peak_count > 0) {
                    interval_sum += (uint16_t)(i - last_peak);
                    interval_count++;
                }
                last_peak = i;
                peak_count++;
            }
        }
    }

    outputs[0].data[0] = spm_from_intervals(interval_sum, interval_count, inputs[0].sample_rate_hz);
    outputs[0].data[1] = (int16_t)peak_count;
    outputs[0].length = 2;
    outputs[0].kind = PP_KIND_CANDIDATE;
    outputs[0].axis = inputs[0].axis;
    outputs[0].sample_rate_hz = inputs[0].sample_rate_hz;

    return pp_result(PP_OK);
}

pp_block_result_t pp_zero_crossing_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    int16_t hysteresis;
    uint16_t min_interval;
    uint16_t i;
    uint16_t crossing_count = 0;
    uint16_t interval_count = 0;
    uint16_t last_crossing = 0;
    uint32_t interval_sum = 0;
    uint8_t below = 0;

    (void)state;

    if (!validate_series_io(inputs, num_inputs, outputs, num_outputs) || !params || params_len < 4) {
        return pp_result(PP_ERR);
    }

    hysteresis = read_i16_le(&params[0]);
    if (hysteresis < 0) {
        hysteresis = (int16_t)-hysteresis;
    }
    min_interval = read_u16_le(&params[2]);
    if (min_interval == 0) {
        min_interval = 1;
    }

    for (i = 0; i < inputs[0].length; i++) {
        int16_t value = inputs[0].data[i];
        if (value <= -hysteresis) {
            below = 1;
        } else if (below && value >= hysteresis) {
            if (crossing_count == 0 || (uint16_t)(i - last_crossing) >= min_interval) {
                if (crossing_count > 0) {
                    interval_sum += (uint16_t)(i - last_crossing);
                    interval_count++;
                }
                last_crossing = i;
                crossing_count++;
                below = 0;
            }
        }
    }

    outputs[0].data[0] = spm_from_intervals(interval_sum, interval_count, inputs[0].sample_rate_hz);
    outputs[0].data[1] = (int16_t)crossing_count;
    outputs[0].length = 2;
    outputs[0].kind = PP_KIND_CANDIDATE;
    outputs[0].axis = inputs[0].axis;
    outputs[0].sample_rate_hz = inputs[0].sample_rate_hz;

    return pp_result(PP_OK);
}
