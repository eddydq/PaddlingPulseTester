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

static void copy_packet(pp_packet_t *dst, const pp_packet_t *src)
{
    uint16_t i;
    for (i = 0; i < src->length; i++) {
        dst->data[i] = src->data[i];
    }
    dst->length = src->length;
    dst->kind = src->kind;
    dst->axis = src->axis;
    dst->sample_rate_hz = src->sample_rate_hz;
}

pp_block_result_t pp_spm_range_gate_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    int16_t spm;

    (void)state;

    if (!inputs || !outputs || num_inputs != 1 || num_outputs != 1 ||
        !inputs[0].data || !outputs[0].data || inputs[0].kind != PP_KIND_CANDIDATE ||
        inputs[0].length < 1 || !params || params_len < 2) {
        return pp_result(PP_ERR);
    }

    spm = inputs[0].data[0];
    if (spm < params[0] || spm > params[1]) {
        outputs[0].length = 0;
        outputs[0].kind = PP_KIND_CANDIDATE;
        return pp_result(PP_SKIP);
    }

    copy_packet(&outputs[0], &inputs[0]);
    return pp_result(PP_OK);
}

pp_block_result_t pp_peak_selector_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    int16_t min_prominence;
    uint16_t min_distance;
    int16_t best_value = 0;
    int16_t best_prominence = 0;
    uint16_t best_index = 0;
    uint16_t last_selected = 0;
    uint16_t i;
    uint8_t found = 0;

    (void)state;

    if (!inputs || !outputs || num_inputs != 2 || num_outputs != 1 ||
        !inputs[0].data || !inputs[1].data || !outputs[0].data ||
        inputs[0].kind != PP_KIND_CANDIDATE || inputs[1].kind != PP_KIND_SERIES ||
        inputs[1].length < 3 || !params || params_len < 4) {
        return pp_result(PP_ERR);
    }

    min_prominence = read_i16_le(&params[0]);
    min_distance = read_u16_le(&params[2]);
    if (min_distance == 0) {
        min_distance = 1;
    }

    for (i = 1; i + 1U < inputs[1].length; i++) {
        int16_t left = inputs[1].data[(uint16_t)(i - 1U)];
        int16_t value = inputs[1].data[i];
        int16_t right = inputs[1].data[(uint16_t)(i + 1U)];
        int16_t shoulder = left > right ? left : right;
        int16_t prominence = (int16_t)(value - shoulder);

        if (value > left && value >= right && prominence >= min_prominence) {
            if (!found || (uint16_t)(i - last_selected) >= min_distance) {
                last_selected = i;
                if (!found || prominence > best_prominence || value > best_value) {
                    best_index = i;
                    best_value = value;
                    best_prominence = prominence;
                    found = 1;
                }
            }
        }
    }

    if (!found) {
        outputs[0].length = 0;
        outputs[0].kind = PP_KIND_CANDIDATE;
        return pp_result(PP_SKIP);
    }

    outputs[0].data[0] = (int16_t)best_index;
    outputs[0].data[1] = best_value;
    outputs[0].length = 2;
    outputs[0].kind = PP_KIND_CANDIDATE;
    outputs[0].axis = inputs[1].axis;
    outputs[0].sample_rate_hz = inputs[1].sample_rate_hz;

    return pp_result(PP_OK);
}

pp_block_result_t pp_confidence_gate_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    uint8_t min_confidence;
    int16_t fallback_value;

    (void)state;

    if (!inputs || !outputs || num_inputs != 1 || num_outputs != 2 ||
        !inputs[0].data || !outputs[0].data || !outputs[1].data ||
        inputs[0].kind != PP_KIND_CANDIDATE || inputs[0].length < 2 ||
        !params || params_len < 3) {
        return pp_result(PP_ERR);
    }

    min_confidence = params[0];
    fallback_value = read_i16_le(&params[1]);

    outputs[0].kind = PP_KIND_CANDIDATE;
    outputs[1].kind = PP_KIND_CANDIDATE;
    outputs[0].axis = inputs[0].axis;
    outputs[1].axis = inputs[0].axis;
    outputs[0].sample_rate_hz = inputs[0].sample_rate_hz;
    outputs[1].sample_rate_hz = inputs[0].sample_rate_hz;

    if (inputs[0].data[1] >= min_confidence) {
        copy_packet(&outputs[0], &inputs[0]);
        outputs[1].length = 0;
    } else {
        outputs[0].length = 0;
        outputs[1].data[0] = fallback_value;
        outputs[1].data[1] = inputs[0].data[1];
        outputs[1].length = 2;
    }

    return pp_result(PP_OK);
}
