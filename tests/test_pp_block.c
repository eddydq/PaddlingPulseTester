#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "pp_block.h"

/* Test: select_axis extracts Z from a 3-axis raw_window packet */
static void test_select_axis_z(void) {
    /* 4 samples of XYZ interleaved: (10,20,30), (40,50,60), (70,80,90), (100,110,120) */
    int16_t raw[] = {10,20,30, 40,50,60, 70,80,90, 100,110,120};
    pp_packet_t input = {
        .data = raw,
        .length = 12,       /* 4 samples x 3 axes */
        .kind = PP_KIND_RAW_WINDOW,
        .axis = PP_AXIS_ALL,
        .sample_rate_hz = 100
    };

    uint8_t params[] = {PP_AXIS_Z};  /* select Z axis */
    int16_t out_buf[4];
    pp_packet_t output = { .data = out_buf, .length = 4 };

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_SELECT_AXIS, &input, 1, params, 1, NULL, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_SERIES);
    assert(output.axis == PP_AXIS_Z);
    assert(output.length == 4);
    assert(out_buf[0] == 30);
    assert(out_buf[1] == 60);
    assert(out_buf[2] == 90);
    assert(out_buf[3] == 120);
    printf("  PASS: test_select_axis_z\n");
}

/* Test: vector_magnitude computes sqrt(x^2+y^2+z^2) in Q15 */
static void test_vector_magnitude(void) {
    /* Single sample: (3,4,0) -> magnitude = 5 */
    int16_t raw[] = {3, 4, 0};
    pp_packet_t input = {
        .data = raw,
        .length = 3,
        .kind = PP_KIND_RAW_WINDOW,
        .axis = PP_AXIS_ALL,
        .sample_rate_hz = 100
    };

    int16_t out_buf[1];
    pp_packet_t output = { .data = out_buf, .length = 1 };

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_VECTOR_MAG, &input, 1, NULL, 0, NULL, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_SERIES);
    assert(output.axis == PP_AXIS_MAG);
    assert(out_buf[0] == 5);
    printf("  PASS: test_vector_magnitude\n");
}

/* Test: block registry returns correct manifest */
static void test_block_registry(void) {
    const pp_block_manifest_t *m = pp_block_get_manifest(PP_BLOCK_SELECT_AXIS);
    assert(m != NULL);
    assert(m->block_id == PP_BLOCK_SELECT_AXIS);
    assert(m->num_inputs == 1);
    assert(m->num_outputs == 1);
    assert(m->input_kinds[0] == PP_KIND_RAW_WINDOW);
    assert(m->output_kinds[0] == PP_KIND_SERIES);
    assert(m->state_size == 0);
    printf("  PASS: test_block_registry\n");
}

int main(void) {
    printf("test_pp_block:\n");
    test_select_axis_z();
    test_vector_magnitude();
    test_block_registry();
    printf("All tests passed.\n");
    return 0;
}
