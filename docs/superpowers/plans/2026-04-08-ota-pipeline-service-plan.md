# OTA Pipeline Service Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a runtime-configurable DSP pipeline to the DA14531 that can be composed on the website, previewed via WASM, and uploaded over BLE.

**Architecture:** 16 modular C blocks with a uniform interface compile to both ARM (firmware) and WASM (browser). A hybrid binary protocol (fixed header + TLV body) encodes the graph topology, parameters, and precomputed data. The website flow builder compiles graphs to this binary format, previews via WASM, and uploads via Web Bluetooth with chunked writes. The DA14531 saves the pipeline to retained RAM, soft-resets, and boots into the new configuration.

**Tech Stack:** C (armclang for DA14531, emcc for WASM), JavaScript (vanilla, Web Bluetooth API, Web Workers), existing Renesas DA14531 SDK.

**Spec:** `docs/superpowers/specs/2026-04-08-ota-pipeline-service-design.md`

---

## File Structure

### Firmware — New Files

| File | Responsibility |
|------|---------------|
| `firmware/app/include/pp_block.h` | Block interface: pp_packet_t, pp_block_manifest_t, pp_block_exec_fn, pp_block_result_t, packet kind enum, block registry API |
| `firmware/app/include/pp_graph.h` | Graph executor: pp_node_t, pp_edge_t, pp_graph_t, topological sort, execution loop |
| `firmware/app/include/pp_protocol.h` | Binary protocol: header struct, TLV tags, parse/validate functions, CRC-16 |
| `firmware/app/include/pp_storage.h` | Storage interface: pp_storage_t, retained RAM read/write, validation |
| `firmware/app/include/pp_pipeline_service.h` | BLE pipeline service: GATT service setup, chunk handler, status codes |
| `firmware/app/src/pp_block.c` | Block registry (const array mapping ID → manifest + exec_fn) |
| `firmware/app/src/pp_block_source.c` | Source blocks: lis3dh_source, mpu6050_source, polar_source |
| `firmware/app/src/pp_block_representation.c` | select_axis, vector_magnitude |
| `firmware/app/src/pp_block_pretraitement.c` | hpf_gravity, lowpass |
| `firmware/app/src/pp_block_estimation.c` | autocorrelation, fft_dominant |
| `firmware/app/src/pp_block_detection.c` | adaptive_peak_detect, zero_crossing_detect |
| `firmware/app/src/pp_block_validation.c` | spm_range_gate, peak_selector, confidence_gate |
| `firmware/app/src/pp_block_suivi.c` | kalman_2d, confirmation_filter |
| `firmware/app/src/pp_graph.c` | Graph executor: build from TLV, topological sort, execute cycle |
| `firmware/app/src/pp_protocol.c` | Binary protocol parser/validator |
| `firmware/app/src/pp_storage.c` | Retained RAM storage backend |
| `firmware/app/src/pp_pipeline_service.c` | BLE GATT service, chunk reassembly, status notifications |

### Firmware — Modified Files

| File | Change |
|------|--------|
| `firmware/config/da14531_config_basic.h` | Add CFG_I2C_EEPROM_ENABLE (future), increase CFG_RET_DATA_SIZE, add PP_MAX_NODES/PP_MAX_EDGES/PP_PIPELINE_MAX_BYTES defines |
| `firmware/app/src/paddling_pulse_app.c` | Boot: check retained RAM → parse pipeline → build graph. Replace hardcoded stroke_rate call with graph executor. Init pipeline BLE service. |
| `firmware/app/include/paddling_pulse_app.h` | Expose pipeline init function |
| `firmware/config/user_profiles_config.h` | Enable custom pipeline service profile |
| `Makefile` | Add new .c files to SOURCES list |

### Tests — New Files

| File | Responsibility |
|------|---------------|
| `tests/test_pp_block.c` | Block interface tests: each block runs with known input, produces expected output |
| `tests/test_pp_protocol.c` | Protocol encode/decode round-trip tests |
| `tests/test_pp_graph.c` | Graph builder, topological sort, execution order, cycle detection |
| `tests/Makefile` | Build and run C unit tests with a minimal test harness (no SDK dependency) |

### Website — New Files (eddydq.github.io)

| File | Responsibility |
|------|---------------|
| `flow-compiler.js` | Graph JSON → binary pipeline encoder (header + TLV) |
| `flow-ble-upload.js` | Web Bluetooth: connect, chunk, write, read status |
| `flow-compiler.test.js` | Unit tests for binary encoding |

### Website — Modified Files

| File | Change |
|------|--------|
| `flow.js` | Add "Upload to DA14531" and "Preview" buttons, wire to compiler + upload modules |
| `flow.html` | Add upload UI elements (progress bar, status indicator) |

### WASM Build — New Files

| File | Responsibility |
|------|---------------|
| `analysis/wasm/pp_wasm_entry.c` | WASM entry points: pp_wasm_catalog_json, pp_wasm_run_graph_json |
| `analysis/wasm/pp_wasm_source_stub.c` | Source block stubs that read from JS-provided buffers |
| `analysis/wasm/Makefile` | emcc build: compile all blocks + graph executor → flow-runtime.wasm |

---

## Task 1: Block Interface and First Block

**Files:**
- Create: `firmware/app/include/pp_block.h`
- Create: `firmware/app/src/pp_block.c`
- Create: `firmware/app/src/pp_block_representation.c`
- Create: `tests/test_pp_block.c`
- Create: `tests/Makefile`

### Step 1: Write the test harness and first test

- [ ] Create `tests/Makefile` with a minimal C test runner (compile with gcc, no SDK dependency):

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -I../firmware/app/include -DPP_TARGET_TEST
TESTS = test_pp_block test_pp_protocol test_pp_graph

all: $(TESTS)
	@for t in $(TESTS); do echo "--- $$t ---"; ./$$t; done

test_pp_block: test_pp_block.c ../firmware/app/src/pp_block.c ../firmware/app/src/pp_block_representation.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TESTS)
```

- [ ] Create `tests/test_pp_block.c` with a test for `select_axis`:

```c
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
        .length = 12,       /* 4 samples × 3 axes */
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

/* Test: vector_magnitude computes sqrt(x²+y²+z²) in Q15 */
static void test_vector_magnitude(void) {
    /* Single sample: (3,4,0) → magnitude = 5 */
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
```

- [ ] Run: `cd tests && mingw32-make test_pp_block`
- [ ] Expected: compile errors (pp_block.h does not exist yet)

### Step 2: Write pp_block.h

- [ ] Create `firmware/app/include/pp_block.h`:

```c
#ifndef PP_BLOCK_H
#define PP_BLOCK_H

#include <stdint.h>
#include <stddef.h>

/* Packet kinds — must match protocol encoding */
enum {
    PP_KIND_RAW_WINDOW = 0,
    PP_KIND_SERIES     = 1,
    PP_KIND_CANDIDATE  = 2,
    PP_KIND_ESTIMATE   = 3
};

/* Axis identifiers */
enum {
    PP_AXIS_X   = 0,
    PP_AXIS_Y   = 1,
    PP_AXIS_Z   = 2,
    PP_AXIS_MAG = 3,
    PP_AXIS_ALL = 0xFF
};

/* Block IDs — must match protocol encoding */
enum {
    PP_BLOCK_LIS3DH_SOURCE      = 0x01,
    PP_BLOCK_MPU6050_SOURCE     = 0x02,
    PP_BLOCK_POLAR_SOURCE       = 0x03,
    PP_BLOCK_SELECT_AXIS        = 0x04,
    PP_BLOCK_VECTOR_MAG         = 0x05,
    PP_BLOCK_HPF_GRAVITY        = 0x06,
    PP_BLOCK_LOWPASS            = 0x07,
    PP_BLOCK_AUTOCORRELATION    = 0x08,
    PP_BLOCK_FFT_DOMINANT       = 0x09,
    PP_BLOCK_ADAPTIVE_PEAK      = 0x0A,
    PP_BLOCK_ZERO_CROSSING      = 0x0B,
    PP_BLOCK_SPM_RANGE_GATE     = 0x0C,
    PP_BLOCK_PEAK_SELECTOR      = 0x0D,
    PP_BLOCK_CONFIDENCE_GATE    = 0x0E,
    PP_BLOCK_KALMAN_2D          = 0x0F,
    PP_BLOCK_CONFIRMATION       = 0x10,
    PP_BLOCK_COUNT              = 16
};

/* Status codes */
enum {
    PP_OK    = 0,
    PP_ERR   = 1,
    PP_SKIP  = 2   /* block produced no output (e.g., confidence gate rejected) */
};

typedef struct {
    int16_t  *data;
    uint16_t  length;
    uint8_t   kind;
    uint8_t   axis;
    uint16_t  sample_rate_hz;
} pp_packet_t;

typedef struct {
    uint8_t  block_id;
    uint8_t  group;
    uint8_t  num_inputs;
    uint8_t  num_outputs;
    uint8_t  input_kinds[3];
    uint8_t  output_kinds[3];
    uint16_t state_size;
} pp_block_manifest_t;

typedef struct {
    uint8_t status;    /* PP_OK, PP_ERR, PP_SKIP */
} pp_block_result_t;

/* Execute a block by ID */
pp_block_result_t pp_block_exec(
    uint8_t            block_id,
    const pp_packet_t *inputs,
    uint8_t            num_inputs,
    const uint8_t     *params,
    uint16_t           params_len,
    uint8_t           *state,
    pp_packet_t       *outputs,
    uint8_t            num_outputs
);

/* Get manifest for a block ID (NULL if unknown) */
const pp_block_manifest_t *pp_block_get_manifest(uint8_t block_id);

#endif /* PP_BLOCK_H */
```

### Step 3: Write pp_block.c (registry) and pp_block_representation.c

- [ ] Create `firmware/app/src/pp_block.c`:

```c
#include "pp_block.h"
#include <stddef.h>

/* Forward declarations — each block file registers via extern */
extern pp_block_result_t pp_select_axis_exec(
    const pp_packet_t *in, uint8_t nin,
    const uint8_t *params, uint16_t plen,
    uint8_t *state, pp_packet_t *out, uint8_t nout);

extern pp_block_result_t pp_vector_mag_exec(
    const pp_packet_t *in, uint8_t nin,
    const uint8_t *params, uint16_t plen,
    uint8_t *state, pp_packet_t *out, uint8_t nout);

/* Block exec function signature */
typedef pp_block_result_t (*pp_block_fn)(
    const pp_packet_t *, uint8_t,
    const uint8_t *, uint16_t,
    uint8_t *, pp_packet_t *, uint8_t);

typedef struct {
    pp_block_manifest_t manifest;
    pp_block_fn         exec;
} pp_block_entry_t;

static const pp_block_entry_t s_registry[] = {
    /* 0x04: select_axis */
    {
        .manifest = {
            .block_id = PP_BLOCK_SELECT_AXIS,
            .group = 1, /* representation */
            .num_inputs = 1, .num_outputs = 1,
            .input_kinds = {PP_KIND_RAW_WINDOW, 0, 0},
            .output_kinds = {PP_KIND_SERIES, 0, 0},
            .state_size = 0
        },
        .exec = pp_select_axis_exec
    },
    /* 0x05: vector_magnitude */
    {
        .manifest = {
            .block_id = PP_BLOCK_VECTOR_MAG,
            .group = 1, /* representation */
            .num_inputs = 1, .num_outputs = 1,
            .input_kinds = {PP_KIND_RAW_WINDOW, 0, 0},
            .output_kinds = {PP_KIND_SERIES, 0, 0},
            .state_size = 0
        },
        .exec = pp_vector_mag_exec
    },
};

#define REGISTRY_SIZE (sizeof(s_registry) / sizeof(s_registry[0]))

static const pp_block_entry_t *find_entry(uint8_t block_id) {
    for (size_t i = 0; i < REGISTRY_SIZE; i++) {
        if (s_registry[i].manifest.block_id == block_id)
            return &s_registry[i];
    }
    return NULL;
}

const pp_block_manifest_t *pp_block_get_manifest(uint8_t block_id) {
    const pp_block_entry_t *e = find_entry(block_id);
    return e ? &e->manifest : NULL;
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
    const pp_block_entry_t *e = find_entry(block_id);
    if (!e) {
        pp_block_result_t r = { .status = PP_ERR };
        return r;
    }
    return e->exec(inputs, num_inputs, params, params_len,
                   state, outputs, num_outputs);
}
```

- [ ] Create `firmware/app/src/pp_block_representation.c`:

```c
#include "pp_block.h"

pp_block_result_t pp_select_axis_exec(
    const pp_packet_t *in, uint8_t nin,
    const uint8_t *params, uint16_t plen,
    uint8_t *state, pp_packet_t *out, uint8_t nout)
{
    (void)nin; (void)state; (void)nout;
    pp_block_result_t r = { .status = PP_ERR };

    if (plen < 1 || !in || !out) return r;

    uint8_t axis = params[0];  /* PP_AXIS_X, Y, or Z */
    if (axis > PP_AXIS_Z) return r;

    uint16_t num_samples = in->length / 3;
    const int16_t *src = in->data;
    int16_t *dst = out->data;

    for (uint16_t i = 0; i < num_samples; i++) {
        dst[i] = src[i * 3 + axis];
    }

    out->length = num_samples;
    out->kind = PP_KIND_SERIES;
    out->axis = axis;
    out->sample_rate_hz = in->sample_rate_hz;

    r.status = PP_OK;
    return r;
}

/* Integer square root (Cortex-M0+ friendly, no FPU) */
static int16_t isqrt32(int32_t val) {
    if (val <= 0) return 0;
    int32_t result = 0;
    int32_t bit = 1L << 30;
    while (bit > val) bit >>= 2;
    while (bit != 0) {
        if (val >= result + bit) {
            val -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (int16_t)result;
}

pp_block_result_t pp_vector_mag_exec(
    const pp_packet_t *in, uint8_t nin,
    const uint8_t *params, uint16_t plen,
    uint8_t *state, pp_packet_t *out, uint8_t nout)
{
    (void)nin; (void)params; (void)plen; (void)state; (void)nout;
    pp_block_result_t r = { .status = PP_ERR };

    if (!in || !out) return r;

    uint16_t num_samples = in->length / 3;
    const int16_t *src = in->data;
    int16_t *dst = out->data;

    for (uint16_t i = 0; i < num_samples; i++) {
        int32_t x = src[i * 3 + 0];
        int32_t y = src[i * 3 + 1];
        int32_t z = src[i * 3 + 2];
        dst[i] = isqrt32(x * x + y * y + z * z);
    }

    out->length = num_samples;
    out->kind = PP_KIND_SERIES;
    out->axis = PP_AXIS_MAG;
    out->sample_rate_hz = in->sample_rate_hz;

    r.status = PP_OK;
    return r;
}
```

### Step 4: Build and run tests

- [ ] Run: `cd tests && mingw32-make test_pp_block`
- [ ] Expected: all 3 tests PASS

### Step 5: Commit

- [ ] ```bash
git add firmware/app/include/pp_block.h firmware/app/src/pp_block.c \
      firmware/app/src/pp_block_representation.c tests/test_pp_block.c tests/Makefile
git commit -m "feat: add block interface, registry, and representation blocks"
```

---

## Task 2: Pretraitement Blocks

**Files:**
- Create: `firmware/app/src/pp_block_pretraitement.c`
- Modify: `firmware/app/src/pp_block.c` (add registry entries)
- Modify: `tests/test_pp_block.c` (add tests)

### Step 1: Add tests for hpf_gravity and lowpass

- [ ] Append to `tests/test_pp_block.c`:

```c
/* Test: hpf_gravity removes DC offset from series */
static void test_hpf_gravity(void) {
    /* DC offset of 1000 + small signal */
    int16_t series[] = {1005, 1010, 1005, 1000, 995, 990, 995, 1000};
    pp_packet_t input = {
        .data = series, .length = 8,
        .kind = PP_KIND_SERIES, .axis = PP_AXIS_Z,
        .sample_rate_hz = 100
    };

    /* params: cutoff_hz=1 (as uint8), order=2 */
    uint8_t params[] = {1, 2};
    int16_t out_buf[8];
    pp_packet_t output = { .data = out_buf, .length = 8 };
    uint8_t state[64] = {0};

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_HPF_GRAVITY, &input, 1, params, 2, state, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_SERIES);
    /* After HPF, DC component should be largely removed */
    /* Exact values depend on filter implementation */
    printf("  PASS: test_hpf_gravity\n");
}

/* Test: lowpass with precomputed coefficients */
static void test_lowpass(void) {
    /* Step function input */
    int16_t series[] = {0, 0, 0, 0, 1000, 1000, 1000, 1000};
    pp_packet_t input = {
        .data = series, .length = 8,
        .kind = PP_KIND_SERIES, .axis = PP_AXIS_Z,
        .sample_rate_hz = 100
    };

    /* params: cutoff_hz=10, order=2 */
    uint8_t params[] = {10, 2};
    int16_t out_buf[8];
    pp_packet_t output = { .data = out_buf, .length = 8 };
    uint8_t state[64] = {0};

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_LOWPASS, &input, 1, params, 2, state, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_SERIES);
    /* Output should be smoothed — first samples near 0, later rising toward 1000 */
    assert(out_buf[0] < 500);  /* early samples attenuated */
    printf("  PASS: test_lowpass\n");
}
```

- [ ] Run: `cd tests && mingw32-make test_pp_block`
- [ ] Expected: FAIL (undefined references to hpf/lowpass)

### Step 2: Implement pp_block_pretraitement.c

- [ ] Create `firmware/app/src/pp_block_pretraitement.c` with:
  - `pp_hpf_gravity_exec`: First-order IIR high-pass (single-pole, integer coefficients). State holds previous input and output sample. Params: `[cutoff_hz, order]`.
  - `pp_lowpass_exec`: First-order IIR low-pass. When a data blob with precomputed coefficients is attached, use those; otherwise compute simple RC coefficients from cutoff_hz. State holds filter memory. Params: `[cutoff_hz, order]`.

### Step 3: Add registry entries in pp_block.c

- [ ] Add extern declarations and registry entries for `PP_BLOCK_HPF_GRAVITY` and `PP_BLOCK_LOWPASS`.

### Step 4: Build and run tests

- [ ] Run: `cd tests && mingw32-make test_pp_block`
- [ ] Expected: all tests PASS

### Step 5: Commit

- [ ] ```bash
git add firmware/app/src/pp_block_pretraitement.c firmware/app/src/pp_block.c tests/test_pp_block.c
git commit -m "feat: add pretraitement blocks — hpf_gravity, lowpass"
```

---

## Task 3: Estimation Blocks

**Files:**
- Create: `firmware/app/src/pp_block_estimation.c`
- Modify: `firmware/app/src/pp_block.c`
- Modify: `tests/test_pp_block.c`

### Step 1: Add tests for autocorrelation and fft_dominant

- [ ] Append tests to `tests/test_pp_block.c`:

```c
/* Test: autocorrelation finds dominant period in sinusoidal signal */
static void test_autocorrelation(void) {
    /* Generate 512-sample 1 Hz sine at 100 Hz sample rate → period = 100 samples */
    int16_t series[512];
    for (int i = 0; i < 512; i++) {
        /* Approximate sine: triangle wave is close enough for autocorrelation */
        int phase = i % 100;
        series[i] = (phase < 50) ? (phase * 200 - 5000) : ((100 - phase) * 200 - 5000);
    }
    pp_packet_t input = {
        .data = series, .length = 512,
        .kind = PP_KIND_SERIES, .axis = PP_AXIS_Z,
        .sample_rate_hz = 100
    };

    /* params: min_lag=50 (0.5Hz min), max_lag=200 (3.3Hz max), confidence_min=30, harmonic_pct=80 */
    uint8_t params[] = {50, 0, 200, 0, 30, 80};  /* uint16 min_lag LE, uint16 max_lag LE, u8, u8 */
    int16_t out_buf[2];  /* [0]=spm_q8, [1]=confidence */
    pp_packet_t output = { .data = out_buf, .length = 2 };

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_AUTOCORRELATION, &input, 1, params, 6, NULL, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_CANDIDATE);
    /* 1 Hz = 60 SPM. out_buf[0] should be near 60 */
    int16_t spm = out_buf[0];
    assert(spm >= 55 && spm <= 65);
    printf("  PASS: test_autocorrelation (spm=%d)\n", spm);
}

/* Test: fft_dominant finds dominant frequency */
static void test_fft_dominant(void) {
    /* Same signal — FFT should also find ~1 Hz dominant */
    int16_t series[256];
    for (int i = 0; i < 256; i++) {
        int phase = i % 100;
        series[i] = (phase < 50) ? (phase * 200 - 5000) : ((100 - phase) * 200 - 5000);
    }
    pp_packet_t input = {
        .data = series, .length = 256,
        .kind = PP_KIND_SERIES, .axis = PP_AXIS_Z,
        .sample_rate_hz = 100
    };

    /* params: min_hz=0 (as uint8, 0.5Hz), max_hz=5, window_type=0 (rectangular) */
    uint8_t params[] = {0, 5, 0};
    int16_t out_buf[2];
    pp_packet_t output = { .data = out_buf, .length = 2 };

    pp_block_result_t result = pp_block_exec(
        PP_BLOCK_FFT_DOMINANT, &input, 1, params, 3, NULL, &output, 1
    );

    assert(result.status == PP_OK);
    assert(output.kind == PP_KIND_CANDIDATE);
    printf("  PASS: test_fft_dominant (spm=%d)\n", out_buf[0]);
}
```

- [ ] Run test, expected: FAIL

### Step 2: Implement pp_block_estimation.c

- [ ] Create `firmware/app/src/pp_block_estimation.c` with:
  - `pp_autocorrelation_exec`: Port existing `paddling_pulse_stroke_rate.c` autocorrelation logic. Params: min_lag (uint16 LE), max_lag (uint16 LE), confidence_min (uint8), harmonic_pct (uint8). Output: candidate packet with [spm, confidence].
  - `pp_fft_dominant_exec`: Radix-2 DIT FFT on power-of-2 window. Find peak bin in [min_hz, max_hz] range. Convert bin to SPM. Params: min_hz, max_hz, window_type.

### Step 3: Add registry entries, build, test

- [ ] Add entries in `pp_block.c` for both blocks.
- [ ] Run: `cd tests && mingw32-make test_pp_block`
- [ ] Expected: all tests PASS

### Step 4: Commit

- [ ] ```bash
git add firmware/app/src/pp_block_estimation.c firmware/app/src/pp_block.c tests/test_pp_block.c
git commit -m "feat: add estimation blocks — autocorrelation, fft_dominant"
```

---

## Task 4: Detection Blocks

**Files:**
- Create: `firmware/app/src/pp_block_detection.c`
- Modify: `firmware/app/src/pp_block.c`
- Modify: `tests/test_pp_block.c`

### Step 1: Add tests for adaptive_peak_detect and zero_crossing_detect

- [ ] Append tests: `adaptive_peak_detect` finds peaks in a signal with known peak locations. `zero_crossing_detect` counts zero crossings in a sinusoidal signal and produces interval-based SPM candidate.

- [ ] Run test, expected: FAIL

### Step 2: Implement pp_block_detection.c

- [ ] Create `firmware/app/src/pp_block_detection.c`:
  - `pp_adaptive_peak_exec`: Adaptive threshold = running mean × threshold_factor. Detect peaks above threshold with min_distance between them. State: running mean, last peak index. Params: threshold_factor (uint8 Q4.4), min_distance (uint16 LE), decay_rate (uint8 Q0.8).
  - `pp_zero_crossing_exec`: Count upward zero crossings with hysteresis band. Convert crossing rate to SPM. Params: hysteresis (int16 LE), min_interval (uint16 LE samples).

### Step 3: Add registry entries, build, test

- [ ] Run: `cd tests && mingw32-make test_pp_block`
- [ ] Expected: all tests PASS

### Step 4: Commit

- [ ] ```bash
git add firmware/app/src/pp_block_detection.c firmware/app/src/pp_block.c tests/test_pp_block.c
git commit -m "feat: add detection blocks — adaptive_peak_detect, zero_crossing_detect"
```

---

## Task 5: Validation and Suivi Blocks

**Files:**
- Create: `firmware/app/src/pp_block_validation.c`
- Create: `firmware/app/src/pp_block_suivi.c`
- Modify: `firmware/app/src/pp_block.c`
- Modify: `tests/test_pp_block.c`

### Step 1: Add tests

- [ ] Tests for:
  - `spm_range_gate`: passes candidate within [min_spm, max_spm], returns PP_SKIP outside
  - `peak_selector`: selects most prominent peak from candidate + series inputs
  - `confidence_gate`: routes to out0 if confidence >= threshold, out1 otherwise
  - `kalman_2d`: filters noisy SPM stream, state persists across calls
  - `confirmation_filter`: requires N consistent readings before accepting

- [ ] Run test, expected: FAIL

### Step 2: Implement validation blocks

- [ ] Create `firmware/app/src/pp_block_validation.c`:
  - `pp_spm_range_gate_exec`: Params: min_spm (uint8), max_spm (uint8). Pass or PP_SKIP.
  - `pp_peak_selector_exec`: Takes 2 inputs (candidate, series). Params: min_prominence (int16 LE), min_distance (uint16 LE). Selects best peak.
  - `pp_confidence_gate_exec`: Params: min_confidence (uint8), fallback_value (int16 LE). Two output ports.

### Step 3: Implement suivi blocks

- [ ] Create `firmware/app/src/pp_block_suivi.c`:
  - `pp_kalman_2d_exec`: Port existing Kalman filter from `paddling_pulse_stroke_rate.c`. Q16.16 fixed-point. State: x_hat, p, last_valid. Params: q (uint16 Q8.8), r (uint16 Q8.8), p_max (uint16 Q8.8), max_jump (uint8 SPM).
  - `pp_confirmation_exec`: State: consecutive_count, last_value. Params: required_count (uint8), tolerance_pct (uint8).

### Step 4: Add all registry entries, build, test

- [ ] Add registry entries for all 5 blocks in `pp_block.c`.
- [ ] Update `tests/Makefile` to include new source files.
- [ ] Run: `cd tests && mingw32-make test_pp_block`
- [ ] Expected: all tests PASS

### Step 5: Commit

- [ ] ```bash
git add firmware/app/src/pp_block_validation.c firmware/app/src/pp_block_suivi.c \
      firmware/app/src/pp_block.c tests/test_pp_block.c tests/Makefile
git commit -m "feat: add validation blocks (spm_range_gate, peak_selector, confidence_gate) and suivi blocks (kalman_2d, confirmation_filter)"
```

---

## Task 6: Binary Protocol Encoder/Decoder

**Files:**
- Create: `firmware/app/include/pp_protocol.h`
- Create: `firmware/app/src/pp_protocol.c`
- Create: `tests/test_pp_protocol.c`
- Modify: `tests/Makefile`

### Step 1: Write protocol tests

- [ ] Create `tests/test_pp_protocol.c`:

```c
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "pp_protocol.h"

/* Test: validate a well-formed header */
static void test_header_valid(void) {
    uint8_t buf[] = {
        0x50, 0x50,  /* magic */
        0x01,        /* version */
        0x02,        /* block_count */
        0x01,        /* edge_count */
        0x00,        /* flags */
        0x00, 0x00,  /* body_length (filled after) */
        0x00, 0x00,  /* crc (filled after) */
        0x00, 0x00   /* reserved */
    };

    pp_protocol_header_t hdr;
    assert(pp_protocol_parse_header(buf, sizeof(buf), &hdr) == PP_PROTO_OK);
    assert(hdr.version == 1);
    assert(hdr.block_count == 2);
    assert(hdr.edge_count == 1);
    printf("  PASS: test_header_valid\n");
}

/* Test: reject bad magic */
static void test_header_bad_magic(void) {
    uint8_t buf[12] = {0xFF, 0xFF};
    pp_protocol_header_t hdr;
    assert(pp_protocol_parse_header(buf, 12, &hdr) == PP_PROTO_ERR_MAGIC);
    printf("  PASS: test_header_bad_magic\n");
}

/* Test: parse a TLV block record */
static void test_tlv_block_record(void) {
    /* tag=0x01, length=4, value: block_id=0x04, node_index=0, param_len=1, param=0x02 */
    uint8_t tlv[] = {0x01, 0x04, 0x04, 0x00, 0x01, 0x02};
    pp_tlv_record_t rec;
    uint16_t consumed = 0;
    assert(pp_protocol_parse_tlv(tlv, sizeof(tlv), &rec, &consumed) == PP_PROTO_OK);
    assert(rec.tag == 0x01);
    assert(rec.length == 4);
    assert(rec.value[0] == 0x04); /* block_id */
    assert(rec.value[1] == 0x00); /* node_index */
    assert(consumed == 6);
    printf("  PASS: test_tlv_block_record\n");
}

/* Test: parse a TLV edge record */
static void test_tlv_edge_record(void) {
    /* tag=0x02, length=4, src_node=0, src_port=0, dst_node=1, dst_port=0 */
    uint8_t tlv[] = {0x02, 0x04, 0x00, 0x00, 0x01, 0x00};
    pp_tlv_record_t rec;
    uint16_t consumed = 0;
    assert(pp_protocol_parse_tlv(tlv, sizeof(tlv), &rec, &consumed) == PP_PROTO_OK);
    assert(rec.tag == 0x02);
    assert(rec.length == 4);
    printf("  PASS: test_tlv_edge_record\n");
}

/* Test: CRC-16 round-trip */
static void test_crc16(void) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t crc = pp_protocol_crc16(data, sizeof(data));
    assert(crc != 0);
    /* Same input must produce same CRC */
    assert(pp_protocol_crc16(data, sizeof(data)) == crc);
    /* Different input must produce different CRC */
    data[0] = 0xFF;
    assert(pp_protocol_crc16(data, sizeof(data)) != crc);
    printf("  PASS: test_crc16\n");
}

int main(void) {
    printf("test_pp_protocol:\n");
    test_header_valid();
    test_header_bad_magic();
    test_tlv_block_record();
    test_tlv_edge_record();
    test_crc16();
    printf("All tests passed.\n");
    return 0;
}
```

- [ ] Run: `cd tests && mingw32-make test_pp_protocol`
- [ ] Expected: FAIL (pp_protocol.h not found)

### Step 2: Implement pp_protocol.h and pp_protocol.c

- [ ] Create `firmware/app/include/pp_protocol.h`:
  - `pp_protocol_header_t` struct matching spec Section 3.2
  - `pp_tlv_record_t` struct: tag, length, value pointer
  - Status codes: PP_PROTO_OK, PP_PROTO_ERR_MAGIC, PP_PROTO_ERR_CRC, PP_PROTO_ERR_TRUNCATED
  - Functions: `pp_protocol_parse_header()`, `pp_protocol_parse_tlv()`, `pp_protocol_crc16()`, `pp_protocol_validate()`

- [ ] Create `firmware/app/src/pp_protocol.c`:
  - Header parsing: check magic 0x5050, extract fields, validate body_length
  - TLV parsing: read tag+length, bounds check, return pointer to value
  - CRC-16/CCITT implementation (small, no lookup table — saves flash)
  - Full validate: parse header, compute CRC over body, compare

### Step 3: Build and run tests

- [ ] Run: `cd tests && mingw32-make test_pp_protocol`
- [ ] Expected: all tests PASS

### Step 4: Commit

- [ ] ```bash
git add firmware/app/include/pp_protocol.h firmware/app/src/pp_protocol.c \
      tests/test_pp_protocol.c tests/Makefile
git commit -m "feat: add binary pipeline protocol parser with CRC-16"
```

---

## Task 7: Graph Executor

**Files:**
- Create: `firmware/app/include/pp_graph.h`
- Create: `firmware/app/src/pp_graph.c`
- Create: `tests/test_pp_graph.c`
- Modify: `tests/Makefile`

### Step 1: Write graph tests

- [ ] Create `tests/test_pp_graph.c`:

```c
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "pp_graph.h"
#include "pp_block.h"

/* Test: topological sort of a linear 3-node graph */
static void test_topo_sort_linear(void) {
    pp_graph_t g = {0};
    g.node_count = 3;
    g.nodes[0].block_id = PP_BLOCK_SELECT_AXIS;
    g.nodes[1].block_id = PP_BLOCK_HPF_GRAVITY;
    g.nodes[2].block_id = PP_BLOCK_AUTOCORRELATION;

    g.edge_count = 2;
    g.edges[0] = (pp_edge_t){0, 0, 1, 0};
    g.edges[1] = (pp_edge_t){1, 0, 2, 0};

    assert(pp_graph_topo_sort(&g) == PP_OK);
    assert(g.exec_order[0] == 0);
    assert(g.exec_order[1] == 1);
    assert(g.exec_order[2] == 2);
    printf("  PASS: test_topo_sort_linear\n");
}

/* Test: detect cycle */
static void test_topo_sort_cycle(void) {
    pp_graph_t g = {0};
    g.node_count = 2;
    g.nodes[0].block_id = PP_BLOCK_HPF_GRAVITY;
    g.nodes[1].block_id = PP_BLOCK_LOWPASS;

    g.edge_count = 2;
    g.edges[0] = (pp_edge_t){0, 0, 1, 0};
    g.edges[1] = (pp_edge_t){1, 0, 0, 0};  /* cycle! */

    assert(pp_graph_topo_sort(&g) == PP_ERR);
    printf("  PASS: test_topo_sort_cycle\n");
}

/* Test: build graph from protocol binary */
static void test_graph_from_binary(void) {
    /* Minimal pipeline: select_axis(Z) → spm_range_gate(30-200) */
    uint8_t payload[] = {
        /* Header */
        0x50, 0x50, 0x01, 0x02, 0x01, 0x00,
        0x00, 0x00, /* body_length — filled below */
        0x00, 0x00, /* crc — filled below */
        0x00, 0x00,
        /* Block 0: select_axis, node 0, 1 param byte (Z=2) */
        0x01, 0x04, 0x04, 0x00, 0x01, 0x02,
        /* Block 1: spm_range_gate, node 1, 2 param bytes (30, 200) */
        0x01, 0x05, 0x0C, 0x01, 0x02, 0x1E, 0xC8,
        /* Edge: 0:0 → 1:0 */
        0x02, 0x04, 0x00, 0x00, 0x01, 0x00
    };

    /* Fill body_length (everything after header = 19 bytes) */
    uint16_t body_len = sizeof(payload) - 12;
    payload[6] = body_len & 0xFF;
    payload[7] = body_len >> 8;

    /* Compute and fill CRC */
    extern uint16_t pp_protocol_crc16(const uint8_t *, uint16_t);
    uint16_t crc = pp_protocol_crc16(payload + 12, body_len);
    payload[8] = crc & 0xFF;
    payload[9] = crc >> 8;

    pp_graph_t g = {0};
    assert(pp_graph_build_from_binary(payload, sizeof(payload), &g) == PP_OK);
    assert(g.node_count == 2);
    assert(g.edge_count == 1);
    assert(g.nodes[0].block_id == PP_BLOCK_SELECT_AXIS);
    assert(g.nodes[1].block_id == PP_BLOCK_SPM_RANGE_GATE);
    printf("  PASS: test_graph_from_binary\n");
}

/* Test: validate port kind matching */
static void test_port_validation(void) {
    pp_graph_t g = {0};
    g.node_count = 2;
    g.nodes[0].block_id = PP_BLOCK_SELECT_AXIS;      /* output: series */
    g.nodes[1].block_id = PP_BLOCK_AUTOCORRELATION;   /* input: series — OK */

    g.edge_count = 1;
    g.edges[0] = (pp_edge_t){0, 0, 1, 0};

    assert(pp_graph_validate_ports(&g) == PP_OK);

    /* Now try invalid: series output → raw_window input */
    g.nodes[1].block_id = PP_BLOCK_SELECT_AXIS;  /* input: raw_window — MISMATCH */
    assert(pp_graph_validate_ports(&g) == PP_ERR);
    printf("  PASS: test_port_validation\n");
}

int main(void) {
    printf("test_pp_graph:\n");
    test_topo_sort_linear();
    test_topo_sort_cycle();
    test_graph_from_binary();
    test_port_validation();
    printf("All tests passed.\n");
    return 0;
}
```

- [ ] Run: `cd tests && mingw32-make test_pp_graph`
- [ ] Expected: FAIL

### Step 2: Implement pp_graph.h and pp_graph.c

- [ ] Create `firmware/app/include/pp_graph.h`:
  - `pp_graph_t` struct as defined in spec Section 5.1
  - Functions: `pp_graph_topo_sort()`, `pp_graph_build_from_binary()`, `pp_graph_validate_ports()`, `pp_graph_execute()`

- [ ] Create `firmware/app/src/pp_graph.c`:
  - `pp_graph_build_from_binary()`: parse header via pp_protocol, iterate TLV records, populate nodes and edges
  - `pp_graph_topo_sort()`: DFS-based topological sort on max 16 nodes. Mark visited/in-stack for cycle detection. Result stored in `exec_order[]`.
  - `pp_graph_validate_ports()`: for each edge, look up source block's output_kind and dest block's input_kind from registry. Reject mismatches.
  - `pp_graph_execute()`: walk exec_order, for each node gather input packets by following edges, call `pp_block_exec()`, store output. Uses double-buffered scratch (2 × 512 bytes).

### Step 3: Build and run tests

- [ ] Run: `cd tests && mingw32-make test_pp_graph`
- [ ] Expected: all tests PASS

### Step 4: Commit

- [ ] ```bash
git add firmware/app/include/pp_graph.h firmware/app/src/pp_graph.c \
      tests/test_pp_graph.c tests/Makefile
git commit -m "feat: add graph executor with topological sort and port validation"
```

---

## Task 8: Storage Backend (Retained RAM)

**Files:**
- Create: `firmware/app/include/pp_storage.h`
- Create: `firmware/app/src/pp_storage.c`
- Modify: `firmware/config/da14531_config_basic.h`

### Step 1: Write pp_storage.h

- [ ] Create `firmware/app/include/pp_storage.h`:

```c
#ifndef PP_STORAGE_H
#define PP_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#define PP_PIPELINE_MAX_BYTES  512

typedef struct {
    bool (*read)(uint16_t addr, uint8_t *buf, uint16_t len);
    bool (*write)(uint16_t addr, const uint8_t *buf, uint16_t len);
    uint16_t capacity;
} pp_storage_t;

/* Get the active storage backend (retained RAM for now) */
const pp_storage_t *pp_storage_get(void);

/* High-level API */
bool pp_storage_save_pipeline(const uint8_t *data, uint16_t len);
bool pp_storage_load_pipeline(uint8_t *buf, uint16_t buf_size, uint16_t *out_len);
bool pp_storage_has_valid_pipeline(void);
void pp_storage_clear(void);

#endif /* PP_STORAGE_H */
```

### Step 2: Implement pp_storage.c

- [ ] Create `firmware/app/src/pp_storage.c`:
  - Retained RAM buffer: `__attribute__((section("retention_mem_area0")))` on ARM, regular static on test target
  - `pp_storage_save_pipeline()`: copy data, store length, validate magic+CRC before saving
  - `pp_storage_load_pipeline()`: read from retained buffer, validate magic+CRC
  - `pp_storage_has_valid_pipeline()`: quick check magic bytes

### Step 3: Increase retained data size

- [ ] Modify `firmware/config/da14531_config_advanced.h`: increase `CFG_RET_DATA_SIZE` from 1860 to 2400 (accommodates 512-byte pipeline buffer + existing data).

### Step 4: Commit

- [ ] ```bash
git add firmware/app/include/pp_storage.h firmware/app/src/pp_storage.c \
      firmware/config/da14531_config_advanced.h
git commit -m "feat: add retained RAM storage backend for pipeline configs"
```

---

## Task 9: Source Blocks (Platform-Dependent)

**Files:**
- Create: `firmware/app/src/pp_block_source.c`
- Modify: `firmware/app/src/pp_block.c`

### Step 1: Implement source blocks

- [ ] Create `firmware/app/src/pp_block_source.c`:
  - `pp_lis3dh_source_exec`: Wraps existing `paddling_pulse_imu_lis3dh.c` driver. Reads from sample_store circular buffer. Params: sample_rate_hz (uint16 LE), resolution (uint8). Configures LIS3DH registers on first call (state tracks initialized flag).
  - `pp_mpu6050_source_exec`: Same pattern, wraps `paddling_pulse_imu_mpu6050.c`.
  - `pp_polar_source_exec`: Same pattern, wraps `paddling_pulse_imu_polar.c`. Params: axis_mask (uint8, bitfield for X/Y/Z).
  - Source capability structs (`pp_source_caps_t`) as defined in spec Section 2.4.

- [ ] Platform guard: `#ifdef PP_TARGET_ARM` for real hardware calls, `#ifdef PP_TARGET_WASM` or `PP_TARGET_TEST` for stubs.

### Step 2: Add registry entries

- [ ] Add entries in `pp_block.c` for PP_BLOCK_LIS3DH_SOURCE, PP_BLOCK_MPU6050_SOURCE, PP_BLOCK_POLAR_SOURCE.

### Step 3: Commit

- [ ] ```bash
git add firmware/app/src/pp_block_source.c firmware/app/src/pp_block.c
git commit -m "feat: add source blocks — lis3dh, mpu6050, polar with capability declarations"
```

---

## Task 10: BLE Pipeline Service

**Files:**
- Create: `firmware/app/include/pp_pipeline_service.h`
- Create: `firmware/app/src/pp_pipeline_service.c`
- Modify: `firmware/config/user_profiles_config.h`

### Step 1: Define the service

- [ ] Create `firmware/app/include/pp_pipeline_service.h`:

```c
#ifndef PP_PIPELINE_SERVICE_H
#define PP_PIPELINE_SERVICE_H

#include <stdint.h>

/* Status codes */
#define PP_SVC_STATUS_IDLE           0x00
#define PP_SVC_STATUS_RECEIVING      0x01
#define PP_SVC_STATUS_VALID_RESET    0x02
#define PP_SVC_STATUS_ERR_CRC        0x80
#define PP_SVC_STATUS_ERR_BLOCK_ID   0x81
#define PP_SVC_STATUS_ERR_GRAPH      0x82
#define PP_SVC_STATUS_ERR_TOO_LARGE  0x83
#define PP_SVC_STATUS_ERR_SOURCE_CFG 0x84

/* Chunk frame flags */
#define PP_CHUNK_FLAG_FIRST  0x01
#define PP_CHUNK_FLAG_LAST   0x02
#define PP_CHUNK_FLAG_ABORT  0x04

/* Initialize the pipeline GATT service */
void pp_pipeline_service_init(void);

/* Called from GATT write handler when data arrives on the control point */
void pp_pipeline_service_on_write(const uint8_t *data, uint16_t len);

/* Get current status byte (for read characteristic) */
uint8_t pp_pipeline_service_status(void);

#endif /* PP_PIPELINE_SERVICE_H */
```

### Step 2: Implement the service

- [ ] Create `firmware/app/src/pp_pipeline_service.c`:
  - Reassembly buffer: reuses the 512-byte pipeline storage area
  - `pp_pipeline_service_on_write()`: parse chunk frame (seq, flags, payload). On first chunk, reset buffer. Append payload. On last chunk, validate full binary (pp_protocol_validate), build graph (pp_graph_build_from_binary + validate_ports), save to storage, set status, trigger `platform_reset()`.
  - 10-second inactivity timer: if no chunk arrives within 10s of a partial transfer, clear buffer and reset status to IDLE.
  - GATT service registration using DA14531 SDK custom service API.

### Step 3: Wire into main app

- [ ] Modify `firmware/app/src/paddling_pulse_app.c`:
  - In `user_app_init()`: call `pp_pipeline_service_init()`
  - In `user_app_on_set_dev_config_complete()`: register custom service in attribute database

### Step 4: Commit

- [ ] ```bash
git add firmware/app/include/pp_pipeline_service.h firmware/app/src/pp_pipeline_service.c \
      firmware/app/src/paddling_pulse_app.c firmware/config/user_profiles_config.h
git commit -m "feat: add BLE pipeline service with chunked write and status notifications"
```

---

## Task 11: Firmware Integration — Boot Pipeline

**Files:**
- Modify: `firmware/app/src/paddling_pulse_app.c`
- Modify: `firmware/app/include/paddling_pulse_app.h`
- Modify: `Makefile`

### Step 1: Add new source files to Makefile

- [ ] Add all new `pp_*.c` files to the `SOURCES` list in `Makefile`.

### Step 2: Modify boot sequence

- [ ] In `paddling_pulse_app.c`, modify the initialization to:
  1. Check `pp_storage_has_valid_pipeline()`
  2. If valid: load binary, call `pp_graph_build_from_binary()`, `pp_graph_validate_ports()`, `pp_graph_topo_sort()`. Store graph in a static `pp_graph_t`.
  3. If invalid or any step fails: build default graph programmatically (same as current hardcoded pipeline: select_axis(Z) → hpf_gravity → autocorrelation → kalman_2d).

### Step 3: Replace stroke rate update with graph execution

- [ ] In the existing timer callback that calls `paddling_pulse_stroke_rate_update()`, replace with `pp_graph_execute(&g_pipeline)`. The graph's final node output feeds into the CSCP notification path (same `app_cscps_ntf_send()` call).

### Step 4: Build firmware

- [ ] Run: `mingw32-make clean && mingw32-make`
- [ ] Expected: builds successfully, produces `build/PaddlingPulse.hex`

### Step 5: Commit

- [ ] ```bash
git add Makefile firmware/app/src/paddling_pulse_app.c firmware/app/include/paddling_pulse_app.h
git commit -m "feat: integrate graph executor into boot sequence, replace hardcoded pipeline"
```

---

## Task 12: Website — Flow Compiler

**Files:**
- Create: `C:\work\PaddlePulse\eddydq.github.io\flow-compiler.js`
- Create: `C:\work\PaddlePulse\eddydq.github.io\flow-compiler.test.js`

### Step 1: Write compiler tests

- [ ] Create `flow-compiler.test.js`:

```javascript
import { compileGraph, PP_MAGIC, PP_VERSION } from './flow-compiler.js';

// Test: compile a minimal 2-block pipeline
function test_minimal_pipeline() {
    const graph = {
        nodes: [
            { id: 0, blockId: 'select_axis', params: { axis: 'z' } },
            { id: 1, blockId: 'spm_range_gate', params: { min_spm: 30, max_spm: 200 } }
        ],
        edges: [
            { src: 0, srcPort: 0, dst: 1, dstPort: 0 }
        ]
    };

    const binary = compileGraph(graph);
    const view = new DataView(binary.buffer);

    // Check header
    console.assert(view.getUint16(0, true) === PP_MAGIC, 'magic');
    console.assert(view.getUint8(2) === PP_VERSION, 'version');
    console.assert(view.getUint8(3) === 2, 'block_count');
    console.assert(view.getUint8(4) === 1, 'edge_count');
    console.log('  PASS: test_minimal_pipeline');
}

// Test: CRC validation
function test_crc_integrity() {
    const graph = {
        nodes: [{ id: 0, blockId: 'select_axis', params: { axis: 'x' } }],
        edges: []
    };
    const binary = compileGraph(graph);
    // Corrupt one byte
    const corrupted = new Uint8Array(binary);
    corrupted[12] ^= 0xFF;
    // CRC should no longer match header
    const view = new DataView(corrupted.buffer);
    const storedCrc = view.getUint16(8, true);
    // Recompute CRC on body
    const bodyLen = view.getUint16(6, true);
    const computedCrc = crc16(corrupted.slice(12, 12 + bodyLen));
    console.assert(storedCrc !== computedCrc, 'corruption detected');
    console.log('  PASS: test_crc_integrity');
}

test_minimal_pipeline();
test_crc_integrity();
console.log('All compiler tests passed.');
```

### Step 2: Implement flow-compiler.js

- [ ] Create `flow-compiler.js`:
  - Block name → ID mapping (matching pp_block.h enum)
  - Parameter encoders per block (e.g., axis string → uint8, float cutoff_hz → integer)
  - `compileGraph(graphJson)`: returns `Uint8Array` with full binary payload
  - CRC-16/CCITT in JavaScript (matching firmware implementation)
  - TLV encoder: block records, edge records, data blobs

### Step 3: Run tests

- [ ] Run: `node flow-compiler.test.js`
- [ ] Expected: all tests PASS

### Step 4: Commit

- [ ] ```bash
cd C:\work\PaddlePulse\eddydq.github.io
git add flow-compiler.js flow-compiler.test.js
git commit -m "feat: add flow graph to binary pipeline compiler"
```

---

## Task 13: Website — Web Bluetooth Upload

**Files:**
- Create: `C:\work\PaddlePulse\eddydq.github.io\flow-ble-upload.js`
- Modify: `C:\work\PaddlePulse\eddydq.github.io\flow.js`
- Modify: `C:\work\PaddlePulse\eddydq.github.io\flow.html`

### Step 1: Implement flow-ble-upload.js

- [ ] Create `flow-ble-upload.js`:

```javascript
const PP_SERVICE_UUID = '...' ;  // 128-bit UUID (define and match firmware)
const PP_CP_UUID = '...';        // Control Point characteristic UUID
const PP_STATUS_UUID = '...';    // Status characteristic UUID

export async function uploadPipeline(binaryPayload, onProgress) {
    const device = await navigator.bluetooth.requestDevice({
        filters: [{ services: [PP_SERVICE_UUID] }]
    });
    const server = await device.gatt.connect();
    const service = await server.getPrimaryService(PP_SERVICE_UUID);
    const cp = await service.getCharacteristic(PP_CP_UUID);
    const status = await service.getCharacteristic(PP_STATUS_UUID);

    // Subscribe to status notifications
    await status.startNotifications();
    status.addEventListener('characteristicvaluechanged', (e) => {
        const val = e.target.value.getUint8(0);
        if (val >= 0x80) throw new Error(`Device error: 0x${val.toString(16)}`);
    });

    // Chunk and send
    const mtu = 244;  // conservative default, could negotiate
    const frameOverhead = 2; // seq + flags
    const chunkSize = mtu - frameOverhead;
    const totalChunks = Math.ceil(binaryPayload.length / chunkSize);

    for (let i = 0; i < totalChunks; i++) {
        const offset = i * chunkSize;
        const chunk = binaryPayload.slice(offset, offset + chunkSize);
        const flags = (i === 0 ? 0x01 : 0x00) | (i === totalChunks - 1 ? 0x02 : 0x00);
        const frame = new Uint8Array(2 + chunk.length);
        frame[0] = i;       // seq
        frame[1] = flags;
        frame.set(chunk, 2);
        await cp.writeValueWithResponse(frame);
        onProgress?.((i + 1) / totalChunks);
    }

    // Read final status
    const finalStatus = await status.readValue();
    return finalStatus.getUint8(0);
}
```

### Step 2: Add upload UI to flow.html and flow.js

- [ ] Add "Upload to DA14531" button in `flow.html`
- [ ] In `flow.js`, wire the button: compile graph → call `uploadPipeline()` → show progress bar and status

### Step 3: Commit

- [ ] ```bash
cd C:\work\PaddlePulse\eddydq.github.io
git add flow-ble-upload.js flow.js flow.html
git commit -m "feat: add Web Bluetooth pipeline upload with chunked writes"
```

---

## Task 14: WASM Dual-Target Build

**Files:**
- Create: `analysis/wasm/pp_wasm_entry.c`
- Create: `analysis/wasm/pp_wasm_source_stub.c`
- Create: `analysis/wasm/Makefile`

### Step 1: Create WASM entry points

- [ ] Create `analysis/wasm/pp_wasm_entry.c`:

```c
#include <emscripten.h>
#include <string.h>
#include <stdlib.h>
#include "pp_block.h"
#include "pp_graph.h"
#include "pp_protocol.h"

static char s_result_buf[4096];

EMSCRIPTEN_KEEPALIVE
const char *pp_wasm_catalog_json(void) {
    /* Build JSON array of block manifests */
    char *p = s_result_buf;
    p += sprintf(p, "[");
    for (uint8_t id = 1; id <= PP_BLOCK_COUNT; id++) {
        const pp_block_manifest_t *m = pp_block_get_manifest(id);
        if (!m) continue;
        if (id > 1) p += sprintf(p, ",");
        p += sprintf(p,
            "{\"id\":%d,\"group\":%d,\"inputs\":%d,\"outputs\":%d,"
            "\"input_kinds\":[%d,%d,%d],\"output_kinds\":[%d,%d,%d],"
            "\"state_size\":%d}",
            m->block_id, m->group, m->num_inputs, m->num_outputs,
            m->input_kinds[0], m->input_kinds[1], m->input_kinds[2],
            m->output_kinds[0], m->output_kinds[1], m->output_kinds[2],
            m->state_size);
    }
    sprintf(p, "]");
    return s_result_buf;
}

EMSCRIPTEN_KEEPALIVE
const char *pp_wasm_run_graph_json(const char *binary_b64, const char *inputs_json) {
    /* Decode base64 binary → parse → build graph → execute → return result as JSON */
    /* Implementation: decode binary, call pp_graph_build_from_binary,
       set up source stub with inputs_json data, pp_graph_execute, format output */
    (void)binary_b64; (void)inputs_json;
    sprintf(s_result_buf, "{\"status\":\"ok\",\"spm\":0}");
    return s_result_buf;
}
```

### Step 2: Create source stubs for WASM

- [ ] Create `analysis/wasm/pp_wasm_source_stub.c`:
  - Implements `pp_lis3dh_source_exec`, `pp_mpu6050_source_exec`, `pp_polar_source_exec` for WASM target
  - Reads from a global buffer set by JavaScript before graph execution
  - `void pp_wasm_set_source_data(const int16_t *data, uint16_t len, uint16_t sample_rate_hz)`

### Step 3: Create WASM Makefile

- [ ] Create `analysis/wasm/Makefile`:

```makefile
EMCC = emcc
BLOCK_SRCS = $(wildcard ../../firmware/app/src/pp_block*.c)
CORE_SRCS = ../../firmware/app/src/pp_protocol.c ../../firmware/app/src/pp_graph.c ../../firmware/app/src/pp_block.c
WASM_SRCS = pp_wasm_entry.c pp_wasm_source_stub.c
CFLAGS = -O2 -I../../firmware/app/include -DPP_TARGET_WASM
LDFLAGS = -s EXPORTED_FUNCTIONS='["_pp_wasm_catalog_json","_pp_wasm_run_graph_json","_pp_wasm_set_source_data","_malloc","_free"]' \
          -s EXPORTED_RUNTIME_METHODS='["UTF8ToString","stringToUTF8","lengthBytesUTF8"]'

all: flow-runtime.wasm

flow-runtime.wasm: $(WASM_SRCS) $(BLOCK_SRCS) $(CORE_SRCS)
	$(EMCC) $(CFLAGS) $(LDFLAGS) -o flow-runtime.js $^

clean:
	rm -f flow-runtime.js flow-runtime.wasm
```

### Step 4: Build WASM

- [ ] Run: `cd analysis/wasm && make`
- [ ] Expected: produces `flow-runtime.wasm` and `flow-runtime.js`

### Step 5: Commit

- [ ] ```bash
git add analysis/wasm/
git commit -m "feat: add WASM build for dual-target C blocks"
```

---

## Task 15: End-to-End Integration Test

**Files:**
- Modify: `tests/test_pp_graph.c`

### Step 1: Write a full pipeline round-trip test

- [ ] Add to `tests/test_pp_graph.c`:

```c
/* Test: full pipeline — select_axis → hpf_gravity → autocorrelation → kalman_2d */
static void test_full_pipeline_roundtrip(void) {
    /* 1. Build binary payload for this pipeline */
    /* 2. Parse it with pp_graph_build_from_binary */
    /* 3. Validate ports */
    /* 4. Topological sort */
    /* 5. Execute with synthetic raw_window input (sinusoidal at known frequency) */
    /* 6. Verify final estimate is within expected SPM range */
    /* ... full test code ... */
    printf("  PASS: test_full_pipeline_roundtrip\n");
}
```

### Step 2: Run all tests

- [ ] Run: `cd tests && mingw32-make all`
- [ ] Expected: all test suites PASS

### Step 3: Build firmware

- [ ] Run: `mingw32-make clean && mingw32-make`
- [ ] Expected: builds successfully

### Step 4: Commit

- [ ] ```bash
git add tests/
git commit -m "test: add full pipeline round-trip integration test"
```

---

## Dependency Graph

```
Task 1 (block interface + representation)
  ├─→ Task 2 (pretraitement)
  ├─→ Task 3 (estimation)
  ├─→ Task 4 (detection)
  └─→ Task 5 (validation + suivi)
        │
Task 6 (protocol) ──────────────┐
        │                        │
Task 7 (graph executor) ←───────┘
        │
Task 8 (storage) ───────────────┐
        │                        │
Task 9 (source blocks) ─────────┤
        │                        │
Task 10 (BLE service) ──────────┤
        │                        │
Task 11 (firmware integration) ←┘
        │
Task 12 (flow compiler) ← Task 6
        │
Task 13 (BLE upload) ← Task 12
        │
Task 14 (WASM build) ← Task 1-5
        │
Task 15 (end-to-end test) ← all
```

**Parallelizable:** Tasks 2, 3, 4 can run in parallel after Task 1. Tasks 12, 14 can run in parallel with Tasks 10, 11.
