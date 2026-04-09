#include "pp_block.h"

typedef struct {
    int16_t prev_x;
    int16_t prev_y;
    uint8_t initialized;
    uint8_t reserved;
} pp_iir_state_t;

static pp_block_result_t pp_result(uint8_t status)
{
    pp_block_result_t result = { .status = status };
    return result;
}

static int16_t saturate_i16(int32_t value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (int16_t)value;
}

static uint16_t coeff_q15(uint16_t numerator, uint16_t denominator)
{
    if (denominator == 0) {
        return 0;
    }
    if (numerator >= denominator) {
        return 32767;
    }
    return (uint16_t)(((uint32_t)numerator * 32768UL) / denominator);
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
           inputs[0].kind == PP_KIND_SERIES;
}

pp_block_result_t pp_hpf_gravity_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    pp_iir_state_t local_state = {0};
    pp_iir_state_t *filter_state;
    uint16_t cutoff_hz;
    uint16_t alpha;
    uint16_t i;

    if (!validate_series_io(inputs, num_inputs, outputs, num_outputs) || !params || params_len < 1) {
        return pp_result(PP_ERR);
    }

    filter_state = state ? (pp_iir_state_t *)state : &local_state;
    cutoff_hz = params[0] ? params[0] : 1;
    alpha = coeff_q15(inputs[0].sample_rate_hz, (uint16_t)(inputs[0].sample_rate_hz + cutoff_hz));

    for (i = 0; i < inputs[0].length; i++) {
        int16_t x = inputs[0].data[i];
        int32_t y;
        if (!filter_state->initialized) {
            filter_state->prev_x = x;
            filter_state->prev_y = 0;
            filter_state->initialized = 1;
        }
        y = ((int32_t)alpha * ((int32_t)filter_state->prev_y + (int32_t)x - (int32_t)filter_state->prev_x)) >> 15;
        outputs[0].data[i] = saturate_i16(y);
        filter_state->prev_x = x;
        filter_state->prev_y = outputs[0].data[i];
    }

    outputs[0].length = inputs[0].length;
    outputs[0].kind = PP_KIND_SERIES;
    outputs[0].axis = inputs[0].axis;
    outputs[0].sample_rate_hz = inputs[0].sample_rate_hz;

    return pp_result(PP_OK);
}

pp_block_result_t pp_lowpass_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    pp_iir_state_t local_state = {0};
    pp_iir_state_t *filter_state;
    uint16_t cutoff_hz;
    uint16_t beta;
    uint16_t i;

    if (!validate_series_io(inputs, num_inputs, outputs, num_outputs) || !params || params_len < 1) {
        return pp_result(PP_ERR);
    }

    filter_state = state ? (pp_iir_state_t *)state : &local_state;
    cutoff_hz = params[0] ? params[0] : 1;
    beta = coeff_q15(cutoff_hz, (uint16_t)(inputs[0].sample_rate_hz + cutoff_hz));

    for (i = 0; i < inputs[0].length; i++) {
        int16_t x = inputs[0].data[i];
        int32_t y;
        if (!filter_state->initialized) {
            filter_state->prev_y = x;
            filter_state->initialized = 1;
        }
        y = (int32_t)filter_state->prev_y + (((int32_t)beta * ((int32_t)x - (int32_t)filter_state->prev_y)) >> 15);
        outputs[0].data[i] = saturate_i16(y);
        filter_state->prev_y = outputs[0].data[i];
        filter_state->prev_x = x;
    }

    outputs[0].length = inputs[0].length;
    outputs[0].kind = PP_KIND_SERIES;
    outputs[0].axis = inputs[0].axis;
    outputs[0].sample_rate_hz = inputs[0].sample_rate_hz;

    return pp_result(PP_OK);
}
