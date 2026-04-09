#include "pp_block.h"

#include <stddef.h>

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

typedef struct {
    pp_block_manifest_t manifest;
    pp_block_exec_fn exec;
} pp_block_entry_t;

static const pp_block_entry_t s_registry[] = {
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
