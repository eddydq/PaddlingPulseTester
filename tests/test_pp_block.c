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

/* Test: source stubs produce raw windows on host targets */
static void test_source_stubs(void) {
    uint8_t params[] = {100, 0, 12};
    int16_t out_buf[12] = {0};
    pp_packet_t output = { .data = out_buf, .length = 12 };
    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_LIS3DH_SOURCE, NULL, 0, params, 3, NULL, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_RAW_WINDOW);
    assert(output.axis == PP_AXIS_ALL);
    assert(output.sample_rate_hz == 100);
    assert(output.length == 12);
    assert(out_buf[0] != out_buf[3]);

    output.length = 12;
    result = pp_block_exec(PP_BLOCK_POLAR_SOURCE, NULL, 0, NULL, 0, NULL, &output, 1);
    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_RAW_WINDOW);
    assert(output.sample_rate_hz == 52);
    printf("  PASS: test_source_stubs\n");
}

/* Test: hpf_gravity removes DC offset from series */
static void test_hpf_gravity(void) {
    int16_t series[] = {1005, 1010, 1005, 1000, 995, 990, 995, 1000};
    pp_packet_t input = {
        .data = series, .length = 8,
        .kind = PP_KIND_SERIES, .axis = PP_AXIS_Z,
        .sample_rate_hz = 100
    };

    uint8_t params[] = {1, 2};
    int16_t out_buf[8];
    pp_packet_t output = { .data = out_buf, .length = 8 };
    uint8_t state[64] = {0};

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_HPF_GRAVITY, &input, 1, params, 2, state, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_SERIES);
    assert(output.axis == PP_AXIS_Z);
    assert(output.length == 8);
    assert(out_buf[0] < 500);
    printf("  PASS: test_hpf_gravity\n");
}

/* Test: lowpass smooths a step input */
static void test_lowpass(void) {
    int16_t series[] = {0, 0, 0, 0, 1000, 1000, 1000, 1000};
    pp_packet_t input = {
        .data = series, .length = 8,
        .kind = PP_KIND_SERIES, .axis = PP_AXIS_Z,
        .sample_rate_hz = 100
    };

    uint8_t params[] = {10, 2};
    int16_t out_buf[8];
    pp_packet_t output = { .data = out_buf, .length = 8 };
    uint8_t state[64] = {0};

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_LOWPASS, &input, 1, params, 2, state, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_SERIES);
    assert(output.axis == PP_AXIS_Z);
    assert(output.length == 8);
    assert(out_buf[0] < 500);
    assert(out_buf[4] > 0);
    assert(out_buf[4] < 1000);
    printf("  PASS: test_lowpass\n");
}

/* Test: autocorrelation finds dominant period in a periodic signal */
static void test_autocorrelation(void) {
    int16_t series[512];
    int i;
    for (i = 0; i < 512; i++) {
        int phase = i % 100;
        series[i] = (int16_t)((phase < 50) ? (phase * 200 - 5000) : ((100 - phase) * 200 - 5000));
    }
    pp_packet_t input = {
        .data = series, .length = 512,
        .kind = PP_KIND_SERIES, .axis = PP_AXIS_Z,
        .sample_rate_hz = 100
    };

    uint8_t params[] = {50, 0, 200, 0, 30, 80};
    int16_t out_buf[2];
    pp_packet_t output = { .data = out_buf, .length = 2 };

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_AUTOCORRELATION, &input, 1, params, 6, NULL, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_CANDIDATE);
    assert(output.axis == PP_AXIS_Z);
    assert(output.length == 2);
    assert(out_buf[0] >= 55 && out_buf[0] <= 65);
    assert(out_buf[1] >= 0);
    printf("  PASS: test_autocorrelation (spm=%d)\n", out_buf[0]);
}

/* Test: fft_dominant finds dominant frequency */
static void test_fft_dominant(void) {
    int16_t series[256];
    int i;
    for (i = 0; i < 256; i++) {
        int phase = i % 100;
        series[i] = (int16_t)((phase < 50) ? (phase * 200 - 5000) : ((100 - phase) * 200 - 5000));
    }
    pp_packet_t input = {
        .data = series, .length = 256,
        .kind = PP_KIND_SERIES, .axis = PP_AXIS_Z,
        .sample_rate_hz = 100
    };

    uint8_t params[] = {0, 5, 0};
    int16_t out_buf[2];
    pp_packet_t output = { .data = out_buf, .length = 2 };

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_FFT_DOMINANT, &input, 1, params, 3, NULL, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_CANDIDATE);
    assert(output.axis == PP_AXIS_Z);
    assert(output.length == 2);
    assert(out_buf[0] >= 50 && out_buf[0] <= 80);
    printf("  PASS: test_fft_dominant (spm=%d)\n", out_buf[0]);
}

/* Test: adaptive_peak_detect estimates SPM from known peak spacing */
static void test_adaptive_peak_detect(void) {
    int16_t series[30] = {0};
    pp_packet_t input;
    uint8_t params[] = {8, 5, 0, 200};
    int16_t out_buf[2];
    pp_packet_t output = { .data = out_buf, .length = 2 };
    uint8_t state[16] = {0};

    series[5] = 1000;
    series[15] = 1100;
    series[25] = 1050;

    input.data = series;
    input.length = 30;
    input.kind = PP_KIND_SERIES;
    input.axis = PP_AXIS_Z;
    input.sample_rate_hz = 10;

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_ADAPTIVE_PEAK, &input, 1, params, 4, state, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_CANDIDATE);
    assert(output.length == 2);
    assert(out_buf[0] >= 55 && out_buf[0] <= 65);
    assert(out_buf[1] >= 3);
    printf("  PASS: test_adaptive_peak_detect (spm=%d peaks=%d)\n", out_buf[0], out_buf[1]);
}

/* Test: zero_crossing_detect estimates SPM from upward zero crossings */
static void test_zero_crossing_detect(void) {
    int16_t series[40];
    int i;
    pp_packet_t input;
    uint8_t params[] = {50, 0, 5, 0};
    int16_t out_buf[2];
    pp_packet_t output = { .data = out_buf, .length = 2 };

    for (i = 0; i < 40; i++) {
        int phase = i % 10;
        series[i] = (phase < 5) ? -500 : 500;
    }

    input.data = series;
    input.length = 40;
    input.kind = PP_KIND_SERIES;
    input.axis = PP_AXIS_Z;
    input.sample_rate_hz = 10;

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_ZERO_CROSSING, &input, 1, params, 4, NULL, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_CANDIDATE);
    assert(output.length == 2);
    assert(out_buf[0] >= 55 && out_buf[0] <= 65);
    assert(out_buf[1] >= 3);
    printf("  PASS: test_zero_crossing_detect (spm=%d crossings=%d)\n", out_buf[0], out_buf[1]);
}

/* Test: spm_range_gate passes in-range candidates and skips out-of-range */
static void test_spm_range_gate(void) {
    int16_t candidate[] = {60, 80};
    int16_t out_buf[2] = {0};
    pp_packet_t input = {
        .data = candidate, .length = 2,
        .kind = PP_KIND_CANDIDATE, .axis = PP_AXIS_Z,
        .sample_rate_hz = 10
    };
    pp_packet_t output = { .data = out_buf, .length = 2 };
    uint8_t params[] = {30, 200};

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_SPM_RANGE_GATE, &input, 1, params, 2, NULL, &output, 1
    );
    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_CANDIDATE);
    assert(out_buf[0] == 60);

    candidate[0] = 250;
    result = pp_block_exec(
        PP_BLOCK_SPM_RANGE_GATE, &input, 1, params, 2, NULL, &output, 1
    );
    assert(result.status == PP_SKIP);
    printf("  PASS: test_spm_range_gate\n");
}

/* Test: peak_selector selects the most prominent local peak */
static void test_peak_selector(void) {
    int16_t candidate[] = {60, 80};
    int16_t series[] = {0, 20, 100, 20, 0, 40, 300, 40, 0};
    pp_packet_t inputs[2] = {
        {.data = candidate, .length = 2, .kind = PP_KIND_CANDIDATE, .axis = PP_AXIS_Z, .sample_rate_hz = 10},
        {.data = series, .length = 9, .kind = PP_KIND_SERIES, .axis = PP_AXIS_Z, .sample_rate_hz = 10}
    };
    uint8_t params[] = {50, 0, 2, 0};
    int16_t out_buf[2] = {0};
    pp_packet_t output = { .data = out_buf, .length = 2 };

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_PEAK_SELECTOR, inputs, 2, params, 4, NULL, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_CANDIDATE);
    assert(out_buf[0] == 6);
    assert(out_buf[1] == 300);
    printf("  PASS: test_peak_selector\n");
}

/* Test: confidence_gate routes candidate to pass or fallback output */
static void test_confidence_gate(void) {
    int16_t candidate[] = {60, 80};
    pp_packet_t input = {
        .data = candidate, .length = 2,
        .kind = PP_KIND_CANDIDATE, .axis = PP_AXIS_Z,
        .sample_rate_hz = 10
    };
    uint8_t params[] = {50, 0, 0};
    int16_t pass_buf[2] = {0};
    int16_t reject_buf[2] = {0};
    pp_packet_t outputs[2] = {
        {.data = pass_buf, .length = 2},
        {.data = reject_buf, .length = 2}
    };

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_CONFIDENCE_GATE, &input, 1, params, 3, NULL, outputs, 2
    );
    assert(result.status == PP_OK);
    assert(outputs[0].length == 2);
    assert(pass_buf[0] == 60);
    assert(outputs[1].length == 0);

    candidate[1] = 20;
    result = pp_block_exec(
        PP_BLOCK_CONFIDENCE_GATE, &input, 1, params, 3, NULL, outputs, 2
    );
    assert(result.status == PP_OK);
    assert(outputs[0].length == 0);
    assert(outputs[1].length == 2);
    assert(reject_buf[0] == 0);
    printf("  PASS: test_confidence_gate\n");
}

/* Test: kalman_2d smooths noisy SPM candidates with persistent state */
static void test_kalman_2d(void) {
    int16_t candidate[] = {58, 80};
    pp_packet_t input = {
        .data = candidate, .length = 2,
        .kind = PP_KIND_CANDIDATE, .axis = PP_AXIS_Z,
        .sample_rate_hz = 10
    };
    uint8_t params[] = {0, 1, 0, 1, 0x10, 0x27, 20};
    uint8_t state[32] = {0};
    int16_t out_buf[2] = {0};
    pp_packet_t output = { .data = out_buf, .length = 2 };

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_KALMAN_2D, &input, 1, params, 7, state, &output, 1
    );
    assert(result.status == PP_OK);

    candidate[0] = 62;
    result = pp_block_exec(
        PP_BLOCK_KALMAN_2D, &input, 1, params, 7, state, &output, 1
    );
    assert(result.status == PP_OK);

    candidate[0] = 60;
    result = pp_block_exec(
        PP_BLOCK_KALMAN_2D, &input, 1, params, 7, state, &output, 1
    );
    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_ESTIMATE);
    assert(out_buf[0] >= 58 && out_buf[0] <= 62);
    printf("  PASS: test_kalman_2d (spm=%d)\n", out_buf[0]);
}

/* Test: confirmation_filter requires N consistent readings */
static void test_confirmation_filter(void) {
    int16_t estimate[] = {60, 80};
    pp_packet_t input = {
        .data = estimate, .length = 2,
        .kind = PP_KIND_ESTIMATE, .axis = PP_AXIS_Z,
        .sample_rate_hz = 10
    };
    uint8_t params[] = {3, 10};
    uint8_t state[16] = {0};
    int16_t out_buf[2] = {0};
    pp_packet_t output = { .data = out_buf, .length = 2 };
    pp_block_result_t result;

    result = pp_block_exec(PP_BLOCK_CONFIRMATION, &input, 1, params, 2, state, &output, 1);
    assert(result.status == PP_SKIP);
    result = pp_block_exec(PP_BLOCK_CONFIRMATION, &input, 1, params, 2, state, &output, 1);
    assert(result.status == PP_SKIP);
    result = pp_block_exec(PP_BLOCK_CONFIRMATION, &input, 1, params, 2, state, &output, 1);
    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_ESTIMATE);
    assert(out_buf[0] == 60);
    printf("  PASS: test_confirmation_filter\n");
}

int main(void) {
    printf("test_pp_block:\n");
    test_select_axis_z();
    test_vector_magnitude();
    test_block_registry();
    test_source_stubs();
    test_hpf_gravity();
    test_lowpass();
    test_autocorrelation();
    test_fft_dominant();
    test_adaptive_peak_detect();
    test_zero_crossing_detect();
    test_spm_range_gate();
    test_peak_selector();
    test_confidence_gate();
    test_kalman_2d();
    test_confirmation_filter();
    printf("All tests passed.\n");
    return 0;
}
