#include "pp_block.h"

typedef struct {
    int32_t rate_q8;
    int32_t p_q8;
    uint8_t initialized;
    uint8_t reserved[3];
} pp_kalman_state_t;

typedef struct {
    int16_t last_value;
    uint8_t count;
    uint8_t initialized;
} pp_confirmation_state_t;

static pp_block_result_t pp_result(uint8_t status)
{
    pp_block_result_t result = { .status = status };
    return result;
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static int32_t abs32(int32_t value)
{
    return value < 0 ? -value : value;
}

static void copy_estimate(pp_packet_t *dst, const pp_packet_t *src)
{
    uint16_t i;
    for (i = 0; i < src->length; i++) {
        dst->data[i] = src->data[i];
    }
    dst->length = src->length;
    dst->kind = PP_KIND_ESTIMATE;
    dst->axis = src->axis;
    dst->sample_rate_hz = src->sample_rate_hz;
}

pp_block_result_t pp_kalman_2d_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    pp_kalman_state_t local_state = {0};
    pp_kalman_state_t *kalman;
    int32_t meas_q8;
    int32_t q_q8;
    int32_t r_q8;
    int32_t p_max_q8;
    int32_t p_predict;
    int32_t k_q8;
    uint8_t max_jump;

    if (!inputs || !outputs || num_inputs != 1 || num_outputs != 1 ||
        !inputs[0].data || !outputs[0].data || inputs[0].kind != PP_KIND_CANDIDATE ||
        inputs[0].length < 1 || !params || params_len < 7) {
        return pp_result(PP_ERR);
    }

    kalman = state ? (pp_kalman_state_t *)state : &local_state;
    meas_q8 = (int32_t)inputs[0].data[0] << 8;
    q_q8 = read_u16_le(&params[0]);
    r_q8 = read_u16_le(&params[2]);
    p_max_q8 = read_u16_le(&params[4]);
    max_jump = params[6];

    if (q_q8 <= 0) {
        q_q8 = 1 << 8;
    }
    if (r_q8 <= 0) {
        r_q8 = 1 << 8;
    }
    if (p_max_q8 <= 0) {
        p_max_q8 = 10000L << 8;
    }

    if (!kalman->initialized) {
        kalman->rate_q8 = meas_q8;
        kalman->p_q8 = r_q8;
        kalman->initialized = 1;
    } else if (max_jump > 0 && abs32(meas_q8 - kalman->rate_q8) > ((int32_t)max_jump << 8)) {
        outputs[0].length = 0;
        outputs[0].kind = PP_KIND_ESTIMATE;
        return pp_result(PP_SKIP);
    } else {
        p_predict = kalman->p_q8 + q_q8;
        if (p_predict > p_max_q8) {
            p_predict = p_max_q8;
        }
        k_q8 = (p_predict << 8) / (p_predict + r_q8);
        kalman->rate_q8 += (k_q8 * (meas_q8 - kalman->rate_q8)) >> 8;
        kalman->p_q8 = p_predict - ((k_q8 * p_predict) >> 8);
    }

    outputs[0].data[0] = (int16_t)((kalman->rate_q8 + 128) >> 8);
    outputs[0].data[1] = (inputs[0].length > 1) ? inputs[0].data[1] : 0;
    outputs[0].length = 2;
    outputs[0].kind = PP_KIND_ESTIMATE;
    outputs[0].axis = inputs[0].axis;
    outputs[0].sample_rate_hz = inputs[0].sample_rate_hz;

    return pp_result(PP_OK);
}

pp_block_result_t pp_confirmation_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs)
{
    pp_confirmation_state_t local_state = {0};
    pp_confirmation_state_t *confirmation;
    uint8_t required_count;
    uint8_t tolerance_pct;
    int16_t value;
    int32_t tolerance;

    if (!inputs || !outputs || num_inputs != 1 || num_outputs != 1 ||
        !inputs[0].data || !outputs[0].data || inputs[0].kind != PP_KIND_ESTIMATE ||
        inputs[0].length < 1 || !params || params_len < 2) {
        return pp_result(PP_ERR);
    }

    confirmation = state ? (pp_confirmation_state_t *)state : &local_state;
    required_count = params[0] ? params[0] : 1;
    tolerance_pct = params[1];
    value = inputs[0].data[0];

    tolerance = (abs32(confirmation->last_value) * tolerance_pct) / 100;
    if (tolerance < 1) {
        tolerance = 1;
    }

    if (!confirmation->initialized || abs32((int32_t)value - confirmation->last_value) > tolerance) {
        confirmation->last_value = value;
        confirmation->count = 1;
        confirmation->initialized = 1;
    } else if (confirmation->count < required_count) {
        confirmation->count++;
    }

    if (confirmation->count < required_count) {
        outputs[0].length = 0;
        outputs[0].kind = PP_KIND_ESTIMATE;
        return pp_result(PP_SKIP);
    }

    copy_estimate(&outputs[0], &inputs[0]);
    return pp_result(PP_OK);
}
