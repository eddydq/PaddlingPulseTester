# OTA Pipeline Service — Design Spec

**Date:** 2026-04-08
**Status:** Draft
**Scope:** DA14531 firmware, eddydq.github.io flow builder, WASM runtime

---

## 1. Overview

The DA14531 firmware gains a runtime-configurable DSP pipeline. Instead of a
single hardcoded algorithm, the device exposes a custom BLE service that accepts
a binary-encoded graph of DSP blocks. The website flow builder lets users
visually compose pipelines, preview results via WASM (same C code), and upload
the configuration over BLE. The pipeline is saved in retained RAM (with EEPROM
migration path) so it persists across sleep cycles.

### Goals

- 16 modular C blocks with a uniform execution interface
- Full graph topology (arbitrary connections, not just linear chains)
- Same C source compiles to ARM (DA14531) and WASM (browser preview)
- Compact binary protocol (hybrid fixed header + TLV body)
- Chunked BLE transfer with validation and error reporting
- Soft reset after upload — device boots into new pipeline
- Source selection (LIS3DH, MPU6050, Polar) is a graph node, all drivers compiled in
- All block parameters configurable (lag limits, cutoffs, thresholds, Hz, axis)
- Precomputed data (filter coefficients, tables) sent from website to save M0+ flash/cycles
- Retained RAM storage now, EEPROM-ready architecture for later

### Non-Goals

- Hot-swap pipeline without reset
- Full 33-block Python analysis set on device
- OTP storage

---

## 2. Block Architecture

### 2.1 Block Interface

Every block implements a uniform C interface:

```c
typedef struct {
    const int16_t *data;
    uint16_t       length;
    uint8_t        kind;           // raw_window, series, candidate, estimate
    uint8_t        axis;           // x=0, y=1, z=2, mag=3
    uint16_t       sample_rate_hz;
} pp_packet_t;

typedef struct {
    uint8_t  block_id;
    uint8_t  group;
    uint8_t  num_inputs;
    uint8_t  num_outputs;
    uint16_t state_size;           // 0 if stateless
} pp_block_manifest_t;

typedef pp_block_result_t (*pp_block_exec_fn)(
    const pp_packet_t *inputs,
    uint8_t            num_inputs,
    const uint8_t     *params,
    uint16_t           params_len,
    uint8_t           *state
);
```

A const block registry maps `block_id` to `{manifest, exec_fn}`.

### 2.2 Block Catalog (16 blocks)

| ID   | Block                | Group          | Stateful | Key Parameters                              |
|------|----------------------|----------------|----------|---------------------------------------------|
| 0x01 | lis3dh_source        | source         | yes      | sample_rate_hz, resolution                  |
| 0x02 | mpu6050_source       | source         | yes      | sample_rate_hz, resolution, gyro_enable     |
| 0x03 | polar_source         | source         | yes      | axis_mask (X/Y/Z selection)                 |
| 0x04 | select_axis          | representation | no       | axis (x/y/z)                                |
| 0x05 | vector_magnitude     | representation | no       | —                                           |
| 0x06 | hpf_gravity          | pretraitement  | yes      | cutoff_hz, order                            |
| 0x07 | lowpass              | pretraitement  | yes      | cutoff_hz, order, coefficients*             |
| 0x08 | autocorrelation      | estimation     | no       | min_lag, max_lag, confidence_min, harmonic_pct |
| 0x09 | fft_dominant         | estimation     | no       | min_hz, max_hz, window_type                 |
| 0x0A | adaptive_peak_detect | detection      | yes      | threshold_factor, min_distance, decay_rate  |
| 0x0B | zero_crossing_detect | detection      | no       | hysteresis, min_interval                    |
| 0x0C | spm_range_gate       | validation     | no       | min_spm, max_spm                            |
| 0x0D | peak_selector        | validation     | no       | min_prominence, min_distance                |
| 0x0E | confidence_gate      | validation     | no       | min_confidence, fallback_value              |
| 0x0F | kalman_2d            | suivi          | yes      | q, r, p_max, max_jump                       |
| 0x10 | confirmation_filter  | suivi          | yes      | required_count, tolerance_pct               |

*\* coefficients sent as precomputed data blob from website*

### 2.3 Dual-Target Compilation

Each block `.c` file uses only integer math (Q15/Q16.16 fixed-point) and no
platform headers. A thin platform layer (`pp_platform.h`) provides:

- `#ifdef PP_TARGET_ARM` — real I2C/BLE calls for source blocks
- `#ifdef PP_TARGET_WASM` — source blocks read from JS-provided test buffers

Build targets:
- ARM: `armclang` → linked into firmware `.hex`
- WASM: `emcc` → `flow-runtime.wasm` for browser

### 2.4 Source Block Constraints

Each source declares its capabilities:

```c
pp_source_caps_t polar_caps = {
    .sample_rates   = {52},
    .num_rates      = 1,
    .resolutions    = {16},
    .num_res        = 1,
    .axes_available = {X, Y, Z},
    .gyro_available = false,
};

pp_source_caps_t lis3dh_caps = {
    .sample_rates   = {1, 10, 25, 50, 100, 200, 400},
    .num_rates      = 7,
    .resolutions    = {8, 10, 12},
    .num_res        = 3,
    .axes_available = {X, Y, Z},
    .gyro_available = false,
};
```

Capabilities are exported to the block catalog JSON for the website. The flow
builder UI disables invalid options per source. Firmware validates on boot as a
safety net.

---

## 3. Binary Pipeline Protocol

### 3.1 Format: Hybrid Fixed Header + TLV Body

```
┌─────────────────────────────────────────────┐
│  FIXED HEADER (12 bytes)                    │
├─────────────────────────────────────────────┤
│  TLV BODY                                   │
│  ├─ Block records  (tag 0x01)               │
│  ├─ Edge records   (tag 0x02)               │
│  └─ Data blobs     (tag 0x03)               │
└─────────────────────────────────────────────┘
```

### 3.2 Fixed Header (12 bytes)

| Offset | Size | Field         | Description                          |
|--------|------|---------------|--------------------------------------|
| 0      | 2    | magic         | `0x5050` ("PP")                      |
| 2      | 1    | version       | Protocol version (starts at 1)       |
| 3      | 1    | block_count   | Number of blocks in graph            |
| 4      | 1    | edge_count    | Number of edges                      |
| 5      | 1    | flags         | Bit 0: source type hint, 1-7: rsvd   |
| 6      | 2    | body_length   | Total TLV body size in bytes         |
| 8      | 2    | params_crc    | CRC-16 over entire TLV body          |
| 10     | 2    | reserved      | 0x0000                               |

### 3.3 TLV Records

Each record: `[tag: 1] [length: 1] [value: N]`

**Tag 0x01 — Block Record:**
```
[block_id: 1] [node_index: 1] [param_length: 1] [params: N]
```

**Tag 0x02 — Edge Record (4 bytes value):**
```
[src_node: 1] [src_port: 1] [dst_node: 1] [dst_port: 1]
```

**Tag 0x03 — Data Blob:**
```
[target_node: 1] [data_type: 1] [data: N]
```

Data types:
- `0x01` — Filter coefficients (Q15 fixed-point)
- `0x02` — Window function
- `0x03` — Lookup table

### 3.4 Example Payload

Pipeline: `lis3dh → select_axis(Z) → hpf_gravity → autocorrelation → kalman_2d`

```
Header:  50 50 01 05 04 00 xx xx xx xx 00 00   (12 bytes)
Block 0: 01 03 01 00 02                         (lis3dh, 100Hz, 12-bit)
Block 1: 01 03 04 01 02                         (select_axis, Z)
Block 2: 01 05 06 02 00 20 04                   (hpf_gravity, cutoff+order)
Block 3: 01 06 08 03 1E C8 33 00               (autocorrelation, lags+conf)
Block 4: 01 06 0F 04 xx xx xx xx               (kalman_2d, q/r/p/jump)
Edge 0:  02 04 00 00 01 00                      (0:0 → 1:0)
Edge 1:  02 04 01 00 02 00                      (1:0 → 2:0)
Edge 2:  02 04 02 00 03 00                      (2:0 → 3:0)
Edge 3:  02 04 03 00 04 00                      (3:0 → 4:0)
```

~70 bytes for a 5-block pipeline. With precomputed data blobs, ~120-400 bytes.

---

## 4. BLE Service & Transfer Protocol

### 4.1 Custom GATT Service

A new 128-bit UUID service alongside existing CSCP (0x1816) and DISS.

| Characteristic       | Properties   | Purpose                              |
|----------------------|-------------|--------------------------------------|
| Pipeline Control Point | Write      | Receives chunked pipeline data       |
| Pipeline Status        | Read, Notify | Reports state and errors           |

### 4.2 Chunked Write Protocol

Each write to the Control Point:

```
[seq: 1] [flags: 1] [payload: up to MTU-4]
```

- `seq` — chunk sequence number (0-based)
- `flags` — bit 0: first chunk, bit 1: last chunk, bit 2: abort

### 4.3 Transfer Flow

```
Website                          DA14531
   │                                │
   ├── Write chunk 0 (first) ──────>│  Buffer chunk
   ├── Write chunk 1 ──────────────>│  Buffer chunk
   ├── Write chunk N (last) ───────>│  Reassemble
   │                                │  Validate header (magic, CRC)
   │                                │  Save to retained RAM
   │<── Notify status: 0x02 ────────┤  (valid, resetting)
   │                                │  Trigger SW_RESET
   │         ... reboot ...         │
   │                                │  Read retained RAM → parse → build graph
   │                                │  Start pipeline, resume BLE advertising
   │<── Reconnect ──────────────────┤
```

### 4.4 Status Codes

| Code | Meaning                                    |
|------|--------------------------------------------|
| 0x00 | Idle (default pipeline or no pipeline)     |
| 0x01 | Receiving chunks                           |
| 0x02 | Valid pipeline saved, resetting            |
| 0x80 | Error: CRC mismatch                        |
| 0x81 | Error: unknown block ID                    |
| 0x82 | Error: graph validation failed (cycle)     |
| 0x83 | Error: payload too large for storage       |
| 0x84 | Error: invalid source configuration        |

### 4.5 Default Fallback

If retained RAM contains no valid pipeline (first boot, power loss, corruption),
firmware falls back to the current hardcoded pipeline: autocorrelation + kalman
on the configured IMU axis. The device always works out of the box.

---

## 5. Graph Executor

### 5.1 Data Structures

```c
#define PP_MAX_NODES  16
#define PP_MAX_EDGES  20

typedef struct {
    uint8_t          block_id;
    const uint8_t   *params;
    uint16_t         params_len;
    uint8_t         *state;
    pp_packet_t      output;
} pp_node_t;

typedef struct {
    uint8_t src_node;
    uint8_t src_port;
    uint8_t dst_node;
    uint8_t dst_port;
} pp_edge_t;

typedef struct {
    pp_node_t  nodes[PP_MAX_NODES];
    pp_edge_t  edges[PP_MAX_EDGES];
    uint8_t    node_count;
    uint8_t    edge_count;
    uint8_t    exec_order[PP_MAX_NODES];
} pp_graph_t;
```

### 5.2 Boot Sequence

1. Check retained RAM for valid pipeline (magic + CRC)
2. If valid → parse TLV, populate `pp_graph_t`, topological sort
3. If invalid → build default hardcoded pipeline
4. Initialize BLE (CSCP + Pipeline Service)
5. Source node begins sampling

### 5.3 Execution Loop

Called every `PP_STROKE_RATE_INTERVAL_MS` (default 1000 ms):

1. Source node produces `pp_packet_t` from sample buffer
2. Walk `exec_order[]` — for each node, gather inputs via edges, call `exec_fn`
3. Final node output = stroke rate estimate
4. Feed to CSCP notification (crank cadence)

### 5.4 Topological Sort

Simple DFS at boot on max 16 nodes. Detects cycles → reject pipeline, fall back
to default. Run once, result cached in `exec_order[]`.

### 5.5 Memory Budget

| Component           | Estimate    |
|---------------------|-------------|
| pp_graph_t struct   | ~400 bytes  |
| Node state (all)    | ~200 bytes  |
| Packet buffer reuse | 0 (in-place)|
| **Total overhead**  | **~600 bytes** |

---

## 6. Website Integration

### 6.1 Flow Compiler (`flow-compiler.js`)

Takes the flow builder graph JSON and produces the binary pipeline:

1. Map block names → block_id (0x01–0x10)
2. Encode per-block parameters (floats → Q15, Hz → integer)
3. Precompute filter coefficients via WASM runtime (exact values device would use)
4. Pack into hybrid header + TLV format
5. Return `Uint8Array`

### 6.2 Web Bluetooth Upload (`flow-ble-upload.js`)

1. `navigator.bluetooth.requestDevice()` filtering on Pipeline Service UUID
2. Connect, discover Pipeline Control Point characteristic
3. Chunk binary payload respecting negotiated MTU
4. Write chunks sequentially
5. Read Pipeline Status for confirmation or error
6. Display progress in UI

### 6.3 WASM Preview

Extends the existing WASM runtime in the `flow-native-runtime` worktree:

- All 16 C blocks compile to WASM
- Source blocks in WASM mode read from JS-provided test buffers
- Graph executor logic shared between ARM and WASM targets
- User clicks "Preview" → graph sent to WASM worker → identical output

### 6.4 Block Catalog Sync

Block catalog (IDs, param schemas, port definitions, source capabilities) is
defined once in C as a struct array. A build step exports it as JSON for the
flow builder palette. Website and firmware always agree on block definitions.

---

## 7. Storage Architecture

### 7.1 Current: Retained RAM

Pipeline binary stored in retained memory section (survives sleep, lost on
power removal). Validation via magic bytes + CRC on boot.

### 7.2 Future: External I2C EEPROM

Architecture designed so the storage backend is a swappable interface:

```c
typedef struct {
    bool (*read)(uint16_t addr, uint8_t *buf, uint16_t len);
    bool (*write)(uint16_t addr, const uint8_t *buf, uint16_t len);
    uint16_t capacity;
} pp_storage_t;
```

Retained RAM and EEPROM are two implementations of the same interface. Pipeline
format and validation logic unchanged.

---

## 8. Constraints Summary

| Source       | Hz Options               | Resolution | Axes  | Gyro |
|-------------|--------------------------|------------|-------|------|
| LIS3DH      | 1, 10, 25, 50, 100, 200, 400 | 8, 10, 12-bit | X Y Z | no  |
| MPU6050     | 4–1000 (configurable)    | 16-bit     | X Y Z | yes  |
| Polar       | 52 (fixed)               | 16-bit     | X Y Z | no   |

Constraints enforced in both the flow builder UI (disable invalid options) and
firmware validation on boot (status 0x84 on mismatch).
