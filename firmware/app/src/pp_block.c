#include "pp_block.h"

#include <stddef.h>

extern pp_block_result_t pp_lis3dh_source_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_mpu6050_source_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_polar_source_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_select_axis_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_vector_mag_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_hpf_gravity_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_lowpass_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_autocorrelation_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_fft_dominant_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_adaptive_peak_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_zero_crossing_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_spm_range_gate_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_peak_selector_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_confidence_gate_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_kalman_2d_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

extern pp_block_result_t pp_confirmation_exec(
    const pp_packet_t *inputs,
    uint8_t num_inputs,
    const uint8_t *params,
    uint16_t params_len,
    uint8_t *state,
    pp_packet_t *outputs,
    uint8_t num_outputs);

typedef struct {
    pp_block_manifest_t manifest;
    pp_block_exec_fn exec;
} pp_block_entry_t;

static const pp_block_entry_t s_registry[] = {
    {
        .manifest = {
            .block_id = PP_BLOCK_LIS3DH_SOURCE,
            .group = 0,
            .num_inputs = 0,
            .num_outputs = 1,
            .input_kinds = {0, 0, 0},
            .output_kinds = {PP_KIND_RAW_WINDOW, 0, 0},
            .state_size = 4
        },
        .exec = pp_lis3dh_source_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_MPU6050_SOURCE,
            .group = 0,
            .num_inputs = 0,
            .num_outputs = 1,
            .input_kinds = {0, 0, 0},
            .output_kinds = {PP_KIND_RAW_WINDOW, 0, 0},
            .state_size = 4
        },
        .exec = pp_mpu6050_source_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_POLAR_SOURCE,
            .group = 0,
            .num_inputs = 0,
            .num_outputs = 1,
            .input_kinds = {0, 0, 0},
            .output_kinds = {PP_KIND_RAW_WINDOW, 0, 0},
            .state_size = 4
        },
        .exec = pp_polar_source_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_SELECT_AXIS,
            .group = 1,
            .num_inputs = 1,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_RAW_WINDOW, 0, 0},
            .output_kinds = {PP_KIND_SERIES, 0, 0},
            .state_size = 0
        },
        .exec = pp_select_axis_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_VECTOR_MAG,
            .group = 1,
            .num_inputs = 1,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_RAW_WINDOW, 0, 0},
            .output_kinds = {PP_KIND_SERIES, 0, 0},
            .state_size = 0
        },
        .exec = pp_vector_mag_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_HPF_GRAVITY,
            .group = 2,
            .num_inputs = 1,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_SERIES, 0, 0},
            .output_kinds = {PP_KIND_SERIES, 0, 0},
            .state_size = 8
        },
        .exec = pp_hpf_gravity_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_LOWPASS,
            .group = 2,
            .num_inputs = 1,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_SERIES, 0, 0},
            .output_kinds = {PP_KIND_SERIES, 0, 0},
            .state_size = 8
        },
        .exec = pp_lowpass_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_AUTOCORRELATION,
            .group = 3,
            .num_inputs = 1,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_SERIES, 0, 0},
            .output_kinds = {PP_KIND_CANDIDATE, 0, 0},
            .state_size = 0
        },
        .exec = pp_autocorrelation_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_FFT_DOMINANT,
            .group = 3,
            .num_inputs = 1,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_SERIES, 0, 0},
            .output_kinds = {PP_KIND_CANDIDATE, 0, 0},
            .state_size = 0
        },
        .exec = pp_fft_dominant_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_ADAPTIVE_PEAK,
            .group = 4,
            .num_inputs = 1,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_SERIES, 0, 0},
            .output_kinds = {PP_KIND_CANDIDATE, 0, 0},
            .state_size = 8
        },
        .exec = pp_adaptive_peak_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_ZERO_CROSSING,
            .group = 4,
            .num_inputs = 1,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_SERIES, 0, 0},
            .output_kinds = {PP_KIND_CANDIDATE, 0, 0},
            .state_size = 0
        },
        .exec = pp_zero_crossing_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_SPM_RANGE_GATE,
            .group = 5,
            .num_inputs = 1,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_CANDIDATE, 0, 0},
            .output_kinds = {PP_KIND_CANDIDATE, 0, 0},
            .state_size = 0
        },
        .exec = pp_spm_range_gate_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_PEAK_SELECTOR,
            .group = 5,
            .num_inputs = 2,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_CANDIDATE, PP_KIND_SERIES, 0},
            .output_kinds = {PP_KIND_CANDIDATE, 0, 0},
            .state_size = 0
        },
        .exec = pp_peak_selector_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_CONFIDENCE_GATE,
            .group = 5,
            .num_inputs = 1,
            .num_outputs = 2,
            .input_kinds = {PP_KIND_CANDIDATE, 0, 0},
            .output_kinds = {PP_KIND_CANDIDATE, PP_KIND_CANDIDATE, 0},
            .state_size = 0
        },
        .exec = pp_confidence_gate_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_KALMAN_2D,
            .group = 6,
            .num_inputs = 1,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_CANDIDATE, 0, 0},
            .output_kinds = {PP_KIND_ESTIMATE, 0, 0},
            .state_size = 16
        },
        .exec = pp_kalman_2d_exec
    },
    {
        .manifest = {
            .block_id = PP_BLOCK_CONFIRMATION,
            .group = 6,
            .num_inputs = 1,
            .num_outputs = 1,
            .input_kinds = {PP_KIND_ESTIMATE, 0, 0},
            .output_kinds = {PP_KIND_ESTIMATE, 0, 0},
            .state_size = 8
        },
        .exec = pp_confirmation_exec
    },
};

#define PP_REGISTRY_SIZE (sizeof(s_registry) / sizeof(s_registry[0]))

static const pp_block_entry_t *find_entry(uint8_t block_id)
{
    size_t i;
    for (i = 0; i < PP_REGISTRY_SIZE; i++) {
        if (s_registry[i].manifest.block_id == block_id) {
            return &s_registry[i];
        }
    }
    return NULL;
}

const pp_block_manifest_t *pp_block_get_manifest(uint8_t block_id)
{
    const pp_block_entry_t *entry = find_entry(block_id);
    return entry ? &entry->manifest : NULL;
}

pp_block_result_t pp_block_exec(
    uint8_t            block_id,
    const pp_packet_t *inputs,
    uint8_t            num_inputs,
    const uint8_t     *params,
    uint16_t           params_len,
    uint8_t           *state,
    pp_packet_t       *outputs,
    uint8_t            num_outputs)
{
    const pp_block_entry_t *entry = find_entry(block_id);
    pp_block_result_t result = { .status = PP_ERR };

    if (!entry || !entry->exec) {
        return result;
    }

    return entry->exec(inputs, num_inputs, params, params_len, state, outputs, num_outputs);
}
