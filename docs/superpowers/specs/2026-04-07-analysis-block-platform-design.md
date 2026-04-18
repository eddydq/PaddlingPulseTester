# Analysis Block Platform — Design Spec

**Date:** 2026-04-07
**Scope:** 33 Python analysis blocks across 6 groups, pipeline executor timing, params_schema for JS flow builder

## Overview

Implement all 33 analysis blocks as Python-only `.py` files following the existing `BlockManifest`/`BlockResult`/`Packet` contract. Each block is a thin scipy/numpy wrapper — simple, self-contained, readable. Blocks will be displayed on the portfolio flow builder at `eddydq.github.io` so clarity is paramount.

C implementations will follow later. All block logic must remain C-portable: no closures, generators, or class hierarchies — just arrays in, values out.

## Packet Kind Flow

```
raw_window → [representation] → series → [pretraitement] → series
  → [estimation] → candidate (data.spm)
  → [detection]  → candidate (data.intervals, data.crossings)
    → [interval_to_spm / crossings_to_spm] → candidate (data.spm)
  → [validation] → candidate → [suivi] → estimate
```

Note: `adaptive_envelope` (detection group) outputs `series`, not `candidate` — it is an envelope extractor meant to be chained before other detection blocks.

### Candidate Packet Data Fields

Detection and estimation blocks produce candidates with different `data` structures:

- **SPM candidates** (from estimation blocks): `data = {"spm": float}`
- **Interval candidates** (from peak/crossing detection): `data = {"intervals": list[float]}` — intervals in seconds between detected events
- **Crossing candidates** (from zero_crossing_detect): `data = {"crossings": int, "window_seconds": float}`

The converter blocks `interval_to_spm` and `crossings_to_spm` normalize these into SPM candidates before validation.

### sample_rate_hz Propagation

All blocks must propagate `sample_rate_hz` from their input packet to their output packet. This value is required by filter blocks (coefficient design), estimation blocks (lag-to-frequency conversion), and detection blocks (sample index to time conversion).

### Multi-Input Semantics

Blocks marked "(multi)" (`consensus_band`, `fallback_selector`) receive multiple packets on a single `"source"` port: `input_packets["source"]` is a list with N packets. The pipeline graph routes multiple node outputs to the same input port. No special contract change needed — the existing list-of-packets mechanism handles this.

### Stateful Block Lifecycle

Stateful blocks (`kalman_2d`, `confirmation_filter`, `invalid_streak_reset`) use the `state` dict to persist across consecutive `run()` calls. The `PipelineExecutor` must be updated to:

1. Maintain a `node_states: dict[str, dict]` across calls
2. Pass the node's persisted state to `run()`
3. Save `BlockResult.state` back after each call
4. On first call, `state` is `{}` — blocks must handle initialization internally

When a stateful block has no valid output (e.g., `confirmation_filter` before the streak is met), it returns `BlockResult(outputs={"primary": []})` — an empty packet list. Downstream blocks receive nothing and are skipped by the executor.

## Block Convention

Every block follows this pattern:

```python
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class XxxBlock:
    manifest = BlockManifest(
        block_id="<group>.<name>",
        group="<group>",
        language="py",
        entrypoint="analysis.algorithms.<group>.py.<name>:BLOCK",
        input_kinds=[...],
        output_ports={...},
        stateful=<bool>,
        params_schema={...},
    )

    def run(self, input_packets, params, state):
        ...
        return BlockResult(outputs={...})

BLOCK = XxxBlock()
```

### params_schema Format

Machine-readable dict for the JS flow builder to render config panels:

```python
params_schema = {
    "cutoff_hz": {"type": "float", "default": 0.5, "description": "Cutoff frequency in Hz"},
    "order":     {"type": "int",   "default": 4,   "description": "Filter order"},
}
```

Valid types: `"float"`, `"int"`, `"str"`, `"bool"`. No nested objects. Blocks with no params use `params_schema = {}`.

Optional fields per param:
- `"min"`, `"max"` — numeric bounds (JS renders a bounded slider/spinner)
- `"enum"` — list of valid string values (JS renders a dropdown), e.g., `"enum": ["x", "y", "z"]`
- `"required"` — `true` if no default exists and the user must provide a value

## Execution Timing

The `PipelineExecutor.run()` wraps each block's `run()` with `time.perf_counter()` and returns timing alongside outputs:

```python
{
    "node_timings": {
        "n1": {"block_id": "representation.select_axis", "elapsed_ms": 0.12},
        "n2": {"block_id": "pretraitement.highpass", "elapsed_ms": 0.34},
    },
    "total_elapsed_ms": 1.87
}
```

Blocks do not measure themselves — the executor owns timing.

## Block Inventory (33 blocks)

### Representation (5 blocks)

| Block | Input Kind | Output Port(s) | Params | Logic |
|-------|-----------|-----------------|--------|-------|
| `select_axis` | `raw_window` | `primary: series` | `axis` ("y") | Extract named axis from IMU window |
| `select_x` | `raw_window` | `primary: series` | — | Extract X axis |
| `select_y` | `raw_window` | `primary: series` | — | Extract Y axis |
| `select_z` | `raw_window` | `primary: series` | — | Extract Z axis |
| `vector_magnitude` | `raw_window` | `primary: series` | — | `sqrt(x² + y² + z²)` per sample |

### Pretraitement (6 blocks)

| Block | Input Kind | Output Port(s) | Params | Logic |
|-------|-----------|-----------------|--------|-------|
| `hpf_gravity` | `series` | `primary: series` | `cutoff_hz` (0.5), `order` (4) | `scipy.signal.butter` high-pass, removes gravity. Replaces existing `highpass.py` (rename). |
| `lowpass` | `series` | `primary: series` | `cutoff_hz` (5.0), `order` (4) | `scipy.signal.butter` low-pass |
| `bandpass` | `series` | `primary: series` | `low_hz` (0.5), `high_hz` (5.0), `order` (4) | `scipy.signal.butter` band-pass |
| `zero_phase_bandpass` | `series` | `primary: series` | `low_hz` (0.5), `high_hz` (5.0), `order` (4) | `scipy.signal.sosfiltfilt` zero-phase band-pass |
| `wavelet_isolation` | `series` | `primary: series` | `wavelet` ("db4"), `level` (4) | `pywt.wavedec` → zero selected levels → `pywt.waverec` |
| `window_trim` | `series` | `primary: series` | `keep_samples` (256), `anchor` ("end") | Keep last/first N samples from series |

### Estimation (8 blocks)

| Block | Input Kind | Output Port(s) | Params | Logic |
|-------|-----------|-----------------|--------|-------|
| `autocorrelation` | `series` | `primary: candidate` | `min_lag_samples` (15), `max_lag_samples` (160) | Peak of autocorrelation → period → SPM |
| `fft_dominant` | `series` | `primary: candidate` | `min_hz` (0.33), `max_hz` (2.0) | `np.fft.rfft` → peak bin in range → SPM |
| `yin_period` | `series` | `primary: candidate` | `threshold` (0.15) | YIN difference function → sub-sample interpolation |
| `cepstrum_period` | `series` | `primary: candidate` | `min_hz` (0.33), `max_hz` (2.0) | Real cepstrum via log-spectrum → quefrency peak |
| `music_refine` | `series` | `primary: candidate` | `num_signals` (1), `min_hz` (0.33), `max_hz` (2.0) | MUSIC pseudo-spectrum from covariance eigendecomposition |
| `hilbert_freq` | `series` | `primary: candidate` | — | `scipy.signal.hilbert` → instantaneous frequency → median SPM |
| `interval_to_spm` | `candidate` | `primary: candidate` | — | Converts mean of `data["intervals"]` (list of seconds) to `data["spm"]` |
| `crossings_to_spm` | `candidate` | `primary: candidate` | — | Converts crossing count + window length to `data["spm"]` |

### Detection (5 blocks)

| Block | Input Kind | Output Port(s) | Params | Logic |
|-------|-----------|-----------------|--------|-------|
| `adaptive_envelope` | `series` | `primary: series` | `smoothing_hz` (1.0) | `abs(scipy.signal.hilbert())` → low-pass envelope |
| `adaptive_peak_detect` | `series` | `primary: candidate` | `min_distance_samples` (26), `prominence` (0.1) | `scipy.signal.find_peaks` → intervals → candidate |
| `schmitt_trigger` | `series` | `primary: candidate` | `high_thresh` (required), `low_thresh` (required) | Hysteresis crossing detection → intervals. Thresholds are signal-amplitude-dependent — no sensible default, user must configure. |
| `zero_crossing_detect` | `series` | `primary: candidate` | `dead_samples` (5) | Zero-crossing with dead zone → crossings count + window length |
| `peak_selector` | `candidate` | `primary: candidate` | `count` (3), `strategy` ("last") | Keep last/first N peaks from detected intervals |

### Validation (6 blocks)

| Block | Input Kind | Output Port(s) | Params | Logic |
|-------|-----------|-----------------|--------|-------|
| `spm_range_gate` | `candidate` | `accepted/rejected: candidate` | `min_spm` (20), `max_spm` (120) | Pass if SPM in range |
| `interval_gate` | `candidate` | `accepted/rejected: candidate` | `min_s` (0.5), `max_s` (3.0) | Pass if interval in range |
| `consensus_band` | `candidate` (multi) | `accepted/rejected: candidate` | `tolerance_spm` (5.0) | Pass if SPM within ± tolerance of median |
| `harmonic_reject` | `candidate` | `accepted/rejected: candidate` | `fundamental_spm` (required), `tolerance_spm` (5.0) | Reject if SPM near 2× or 0.5× of `fundamental_spm`. This is a static param — the user's expected typical paddle rate (e.g., 60 SPM). Not derived from other blocks. |
| `confidence_gate` | `candidate` | `accepted/rejected: candidate` | `min_confidence` (0.5) | Pass if confidence >= threshold |
| `fallback_selector` | `candidate` (multi) | `selected: candidate` | — | Pick candidate with highest confidence |

### Suivi (3 blocks)

| Block | Input Kind | Output Port(s) | Params | Logic |
|-------|-----------|-----------------|--------|-------|
| `kalman_2d` | `candidate` | `primary: estimate` | `process_noise` (1.0), `measurement_noise` (10.0) | 2D Kalman (SPM + derivative), stateful |
| `confirmation_filter` | `estimate` | `primary: estimate` | `required_streak` (3) | Emit only after N consecutive valid estimates, stateful |
| `invalid_streak_reset` | `estimate` | `primary: estimate` | `max_invalid` (5) | Reset tracking state after N consecutive invalids, stateful |

## Dependencies

- `numpy` — already used
- `scipy` — `signal`, `interpolate`, `linalg`
- `pywt` — only for `wavelet_isolation`

## Changes to Existing Files

- **`blocks.py`** — Two changes:
  1. Add timing instrumentation to `PipelineExecutor.run()`. Return `(exported, pipeline_diagnostics)` tuple. **Breaking change** — existing callers that do `result = executor.run(...)` must change to `result, diagnostics = executor.run(...)`. Tests in `analysis_pipeline_executor_test.py` need updating.
  2. Add state propagation: executor maintains `node_states` dict, passes persisted state to each `run()`, saves returned state back.
- **`highpass.py`** — Renamed to `hpf_gravity.py`, block_id changed from `pretraitement.highpass` to `pretraitement.hpf_gravity`. Reimplemented with `scipy.signal.butter` instead of the old `_filters.remove_gravity_hpf` import. Add `params_schema`.
- **Existing blocks** (`select_axis.py`, `autocorrelation.py`, `spm_range_gate.py`) — Add `params_schema` to manifests. Logic unchanged.
- **Empty stubs** (`kalman_2d.py`, `adaptive_peak_detect.py`, `schmitt_trigger.py`, `zero_crossing_detect.py`) — Replace with real implementations.
- **`select_x.py`, `select_y.py`, `select_z.py`** — Standalone implementations (not delegating to `select_axis`). Each is ~5 lines. Simpler for C porting.

## File Layout

```
analysis/algorithms/
  representation/py/  select_axis.py, select_x.py, select_y.py, select_z.py, vector_magnitude.py
  pretraitement/py/   hpf_gravity.py, lowpass.py, bandpass.py, zero_phase_bandpass.py,
                      wavelet_isolation.py, window_trim.py
  estimation/py/      autocorrelation.py, fft_dominant.py, yin_period.py, cepstrum_period.py,
                      music_refine.py, hilbert_freq.py, interval_to_spm.py, crossings_to_spm.py
  detection/py/       adaptive_envelope.py, adaptive_peak_detect.py, schmitt_trigger.py,
                      zero_crossing_detect.py, peak_selector.py
  validation/py/      spm_range_gate.py, interval_gate.py, consensus_band.py, harmonic_reject.py,
                      confidence_gate.py, fallback_selector.py
  suivi/py/           kalman_2d.py, confirmation_filter.py, invalid_streak_reset.py
```
