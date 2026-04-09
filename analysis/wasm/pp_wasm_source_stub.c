#include <emscripten.h>
#include <string.h>
#include "pp_block.h"

#define PP_WASM_SOURCE_MAX_SAMPLES 512

static int16_t s_source_data[PP_WASM_SOURCE_MAX_SAMPLES * 3];
static uint16_t s_source_len;
static uint16_t s_source_rate_hz = 52;

static pp_block_result_t pp_result(uint8_t status)
{
    pp_block_result_t result = { .status = status };
    return result;
}

EMSCRIPTEN_KEEPALIVE
void pp_wasm_set_source_data(const int16_t *data, uint16_t len, uint16_t sample_rate_hz)
{
    if (!data) {
        s_source_len = 0;
        return;
    }
    if (len > (uint16_t)(PP_WASM_SOURCE_MAX_SAMPLES * 3U)) {
        len = (uint16_t)(PP_WASM_SOURCE_MAX_SAMPLES * 3U);
    }
    memcpy(s_source_data, data, (size_t)len * sizeof(int16_t));
    s_source_len = len;
    s_source_rate_hz = sample_rate_hz ? sample_rate_hz : 52;
}

static pp_block_result_t wasm_source_exec(
    uint16_t default_rate_hz,
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    uint16_t capacity;
    uint16_t len;
    uint16_t i;

    (void)inputs;
    (void)num_inputs;
    (void)params;
    (void)params_len;
    (void)state;

    if (!outputs || num_outputs != 1 || !outputs[0].data) {
        return pp_result(PP_ERR);
    }

    capacity = outputs[0].length;
    len = s_source_len < capacity ? s_source_len : capacity;
    if (len == 0) {
        len = capacity >= 12U ? 12U : capacity;
        for (i = 0; i < len; i++) {
            outputs[0].data[i] = (int16_t)(i * 10);
        }
    } else {
        memcpy(outputs[0].data, s_source_data, (size_t)len * sizeof(int16_t));
    }

    outputs[0].length = len;
    outputs[0].kind = PP_KIND_RAW_WINDOW;
    outputs[0].axis = PP_AXIS_ALL;
    outputs[0].sample_rate_hz = s_source_rate_hz ? s_source_rate_hz : default_rate_hz;
    return pp_result(PP_OK);
}

pp_block_result_t pp_lis3dh_source_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    return wasm_source_exec(100, inputs, num_inputs, params, params_len, state, outputs, num_outputs);
}

pp_block_result_t pp_mpu6050_source_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    return wasm_source_exec(100, inputs, num_inputs, params, params_len, state, outputs, num_outputs);
}

pp_block_result_t pp_polar_source_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    return wasm_source_exec(52, inputs, num_inputs, params, params_len, state, outputs, num_outputs);
}
