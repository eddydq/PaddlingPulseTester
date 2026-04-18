#include "pp_block.h"

#if !defined(PP_TARGET_TEST) && !defined(PP_TARGET_WASM)
#include "paddling_pulse_sample_store.h"
#endif

static const pp_source_caps_t s_lis3dh_caps = {
    .sample_rates = {1, 10, 25, 50, 100, 200, 400, 0},
    .num_rates = 7,
    .resolutions = {8, 10, 12, 0},
    .num_resolutions = 3,
    .axes_available = {PP_AXIS_X, PP_AXIS_Y, PP_AXIS_Z},
    .num_axes = 3,
    .gyro_available = 0
};

static const pp_source_caps_t s_mpu6050_caps = {
    .sample_rates = {4, 10, 25, 50, 100, 200, 400, 1000},
    .num_rates = 8,
    .resolutions = {16, 0, 0, 0},
    .num_resolutions = 1,
    .axes_available = {PP_AXIS_X, PP_AXIS_Y, PP_AXIS_Z},
    .num_axes = 3,
    .gyro_available = 1
};

static const pp_source_caps_t s_polar_caps = {
    .sample_rates = {52, 0, 0, 0, 0, 0, 0, 0},
    .num_rates = 1,
    .resolutions = {16, 0, 0, 0},
    .num_resolutions = 1,
    .axes_available = {PP_AXIS_X, PP_AXIS_Y, PP_AXIS_Z},
    .num_axes = 3,
    .gyro_available = 0
};

const pp_source_caps_t *pp_source_get_caps(uint8_t block_id)
{
    switch (block_id) {
    case PP_BLOCK_LIS3DH_SOURCE:
        return &s_lis3dh_caps;
    case PP_BLOCK_MPU6050_SOURCE:
        return &s_mpu6050_caps;
    case PP_BLOCK_POLAR_SOURCE:
        return &s_polar_caps;
    default:
        return 0;
    }
}

#if !defined(PP_TARGET_WASM)

static pp_block_result_t pp_result(uint8_t status)
{
    pp_block_result_t result = { .status = status };
    return result;
}

static uint16_t read_u16_le_or_default(const uint8_t *data, uint16_t len, uint16_t fallback)
{
    if (!data || len < 2U) {
        return fallback;
    }
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

#if defined(PP_TARGET_TEST)
static pp_block_result_t fill_test_source(
    uint8_t block_id,
    uint16_t sample_rate_hz,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    uint16_t capacity;
    uint16_t samples;
    uint16_t i;

    if (!outputs || num_outputs != 1 || !outputs[0].data) {
        return pp_result(PP_ERR);
    }

    capacity = outputs[0].length;
    samples = (uint16_t)(capacity / 3U);
    if (samples == 0) {
        return pp_result(PP_ERR);
    }

    for (i = 0; i < samples; i++) {
        uint16_t phase = (uint16_t)(i % 50U);
        int16_t wave = (int16_t)((phase < 25U) ? (int16_t)(phase * 160U - 2000U) : (int16_t)((50U - phase) * 160U - 2000U));
        int16_t base = (int16_t)(wave + block_id * 10);
        outputs[0].data[(uint16_t)(i * 3U + 0U)] = base;
        outputs[0].data[(uint16_t)(i * 3U + 1U)] = (int16_t)(base + 1);
        outputs[0].data[(uint16_t)(i * 3U + 2U)] = (int16_t)(base + 2);
    }

    outputs[0].length = (uint16_t)(samples * 3U);
    outputs[0].kind = PP_KIND_RAW_WINDOW;
    outputs[0].axis = PP_AXIS_ALL;
    outputs[0].sample_rate_hz = sample_rate_hz;
    return pp_result(PP_OK);
}
#else
static pp_block_result_t fill_sample_store_source(
    uint16_t sample_rate_hz,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    uint16_t capacity;
    uint16_t samples;
    uint16_t count;
    uint16_t offset;
    uint16_t i;

    if (!outputs || num_outputs != 1 || !outputs[0].data) {
        return pp_result(PP_ERR);
    }

    capacity = (uint16_t)(outputs[0].length / 3U);
    count = pp_sample_store_get_count();
    samples = count < capacity ? count : capacity;
    offset = (uint16_t)(count - samples);

    for (i = 0; i < samples; i++) {
        int16_t sample = pp_sample_store_get((uint16_t)(offset + i));
        outputs[0].data[(uint16_t)(i * 3U + 0U)] = sample;
        outputs[0].data[(uint16_t)(i * 3U + 1U)] = sample;
        outputs[0].data[(uint16_t)(i * 3U + 2U)] = sample;
    }

    outputs[0].length = (uint16_t)(samples * 3U);
    outputs[0].kind = PP_KIND_RAW_WINDOW;
    outputs[0].axis = PP_AXIS_ALL;
    outputs[0].sample_rate_hz = sample_rate_hz ? sample_rate_hz : pp_sample_store_get_rate_hz();
    return pp_result(PP_OK);
}
#endif

static pp_block_result_t source_exec_common(
    uint8_t block_id,
    uint16_t default_sample_rate_hz,
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    uint16_t sample_rate_hz;

    (void)inputs;
    (void)num_inputs;
    (void)state;

    sample_rate_hz = read_u16_le_or_default(params, params_len, default_sample_rate_hz);
    if (sample_rate_hz == 0) {
        sample_rate_hz = default_sample_rate_hz;
    }

#if defined(PP_TARGET_TEST)
    return fill_test_source(block_id, sample_rate_hz, outputs, num_outputs);
#else
    return fill_sample_store_source(sample_rate_hz, outputs, num_outputs);
#endif
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
    return source_exec_common(PP_BLOCK_LIS3DH_SOURCE, 100, inputs, num_inputs, params, params_len, state, outputs, num_outputs);
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
    return source_exec_common(PP_BLOCK_MPU6050_SOURCE, 100, inputs, num_inputs, params, params_len, state, outputs, num_outputs);
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
    return source_exec_common(PP_BLOCK_POLAR_SOURCE, 52, inputs, num_inputs, params, params_len, state, outputs, num_outputs);
}

#endif /* !PP_TARGET_WASM */
