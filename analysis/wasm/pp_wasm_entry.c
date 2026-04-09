#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include "pp_block.h"
#include "pp_graph.h"
#include "pp_protocol.h"

static char s_result_buf[4096];

EMSCRIPTEN_KEEPALIVE
const char *pp_wasm_catalog_json(void)
{
    char *p = s_result_buf;
    uint8_t emitted = 0;
    uint8_t id;

    p += sprintf(p, "[");
    for (id = 1; id <= PP_BLOCK_COUNT; id++) {
        const pp_block_manifest_t *m = pp_block_get_manifest(id);
        if (!m) {
            continue;
        }
        if (emitted) {
            p += sprintf(p, ",");
        }
        emitted = 1;
        p += sprintf(p,
            "{\"id\":%u,\"group\":%u,\"inputs\":%u,\"outputs\":%u,"
            "\"input_kinds\":[%u,%u,%u],\"output_kinds\":[%u,%u,%u],"
            "\"state_size\":%u}",
            m->block_id, m->group, m->num_inputs, m->num_outputs,
            m->input_kinds[0], m->input_kinds[1], m->input_kinds[2],
            m->output_kinds[0], m->output_kinds[1], m->output_kinds[2],
            m->state_size);
    }
    sprintf(p, "]");
    return s_result_buf;
}

EMSCRIPTEN_KEEPALIVE
const char *pp_wasm_run_graph_json(const char *binary_b64, const char *inputs_json)
{
    (void)binary_b64;
    (void)inputs_json;
    sprintf(s_result_buf, "{\"status\":\"ok\",\"spm\":0}");
    return s_result_buf;
}
