#include "pp_block.h"

static pp_block_result_t pp_result(uint8_t status)
{
    pp_block_result_t result = { .status = status };
    return result;
}

pp_block_result_t pp_select_axis_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    uint8_t axis;
    uint16_t num_samples;
    uint16_t i;
    const int16_t *src;
    int16_t *dst;

    (void)state;

    if (!inputs || !outputs || num_inputs != 1 || num_outputs != 1 || !params || params_len < 1) {
        return pp_result(PP_ERR);
    }
    if (!inputs[0].data || !outputs[0].data || inputs[0].kind != PP_KIND_RAW_WINDOW) {
        return pp_result(PP_ERR);
    }

    axis = params[0];
    if (axis > PP_AXIS_Z || (inputs[0].length % 3U) != 0U) {
        return pp_result(PP_ERR);
    }

    num_samples = (uint16_t)(inputs[0].length / 3U);
    src = inputs[0].data;
    dst = outputs[0].data;

    for (i = 0; i < num_samples; i++) {
        dst[i] = src[(uint16_t)(i * 3U + axis)];
    }

    outputs[0].length = num_samples;
    outputs[0].kind = PP_KIND_SERIES;
    outputs[0].axis = axis;
    outputs[0].sample_rate_hz = inputs[0].sample_rate_hz;

    return pp_result(PP_OK);
}

static int16_t isqrt32(int32_t val)
{
    int32_t result = 0;
    int32_t bit = 1L << 30;

    if (val <= 0) {
        return 0;
    }

    while (bit > val) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (val >= result + bit) {
            val -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }

    if (result > 32767) {
        return 32767;
    }
    return (int16_t)result;
}

pp_block_result_t pp_vector_mag_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    uint16_t num_samples;
    uint16_t i;
    const int16_t *src;
    int16_t *dst;

    (void)params;
    (void)params_len;
    (void)state;

    if (!inputs || !outputs || num_inputs != 1 || num_outputs != 1) {
        return pp_result(PP_ERR);
    }
    if (!inputs[0].data || !outputs[0].data || inputs[0].kind != PP_KIND_RAW_WINDOW) {
        return pp_result(PP_ERR);
    }
    if ((inputs[0].length % 3U) != 0U) {
        return pp_result(PP_ERR);
    }

    num_samples = (uint16_t)(inputs[0].length / 3U);
    src = inputs[0].data;
    dst = outputs[0].data;

    for (i = 0; i < num_samples; i++) {
        int32_t x = src[(uint16_t)(i * 3U + 0U)];
        int32_t y = src[(uint16_t)(i * 3U + 1U)];
        int32_t z = src[(uint16_t)(i * 3U + 2U)];
        dst[i] = isqrt32(x * x + y * y + z * z);
    }

    outputs[0].length = num_samples;
    outputs[0].kind = PP_KIND_SERIES;
    outputs[0].axis = PP_AXIS_MAG;
    outputs[0].sample_rate_hz = inputs[0].sample_rate_hz;

    return pp_result(PP_OK);
}
