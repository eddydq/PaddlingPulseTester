# Analysis Block Platform — Design Spec

**Date:** 2026-04-07
**Scope:** 33 Python analysis blocks across 6 groups, pipeline executor timing, params_schema for JS flow builder

## Overview

Implement all 33 analysis blocks as Python-only `.py` files following the existing `BlockManifest`/`BlockResult`/`Packet` contract. Each block is a thin scipy/numpy wrapper — simple, self-contained, readable. Blocks will be displayed on the portfolio flow builder at `eddydq.github.io` so clarity is paramount.

C implementations will follow later. All block logic must remain C-portable: no closures, generators, or class hierarchies — just arrays in, values out.

## Packet Kind Flow

```
raw_window → [representation] → series → [pretraitement] → series → [detection/estimation] → candidate → [validation] → candidate → [suivi] → estimate
```

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
| `hpf_gravity` | `series` | `primary: series` | `cutoff_hz` (0.5) | `scipy.signal.butter` high-pass, removes gravity |
| `lowpass` | `series` | `primary: series` | `cutoff_hz` (5.0) | `scipy.signal.butter` low-pass |
| `bandpass` | `series` | `primary: series` | `low_hz` (0.5), `high_hz` (5.0) | `scipy.signal.butter` band-pass |
| `zero_phase_bandpass` | `series` | `primary: series` | `low_hz`, `high_hz`, `order` (4) | `scipy.signal.sosfiltfilt` zero-phase band-pass |
| `wavelet_isolation` | `series` | `primary: series` | `wavelet` ("db4"), `level` (4) | `pywt.wavedec` → zero selected levels → `pywt.waverec` |
| `window_trim` | `series` | `primary: series` | `keep_samples` (256), `anchor` ("end") | Keep last/first N samples from series |

### Estimation (8 blocks)

| Block | Input Kind | Output Port(s) | Params | Logic |
|-------|-----------|-----------------|--------|-------|
| `autocorrelation` | `series` | `primary: candidate` | `min_lag`, `max_lag` | Peak of autocorrelation → period → SPM |
| `fft_dominant` | `series` | `primary: candidate` | `min_hz` (0.33), `max_hz` (2.0) | `np.fft.rfft` → peak bin in range → SPM |
| `yin_period` | `series` | `primary: candidate` | `threshold` (0.15) | YIN difference function → sub-sample interpolation |
| `cepstrum_period` | `series` | `primary: candidate` | `min_hz`, `max_hz` | Real cepstrum via log-spectrum → quefrency peak |
| `music_refine` | `series` | `primary: candidate` | `num_signals` (1), `min_hz`, `max_hz` | MUSIC pseudo-spectrum from covariance eigendecomposition |
| `hilbert_freq` | `series` | `primary: candidate` | — | `scipy.signal.hilbert` → instantaneous frequency → median SPM |
| `interval_to_spm` | `candidate` | `primary: candidate` | — | Converts `data["interval"]` (seconds) to `data["spm"]` |
| `crossings_to_spm` | `candidate` | `primary: candidate` | — | Converts crossing count + window length to `data["spm"]` |

### Detection (5 blocks)

| Block | Input Kind | Output Port(s) | Params | Logic |
|-------|-----------|-----------------|--------|-------|
| `adaptive_envelope` | `series` | `primary: series` | `smoothing_hz` (1.0) | `abs(scipy.signal.hilbert())` → low-pass envelope |
| `adaptive_peak_detect` | `series` | `primary: candidate` | `min_distance`, `prominence` | `scipy.signal.find_peaks` → intervals → candidate |
| `schmitt_trigger` | `series` | `primary: candidate` | `high_thresh`, `low_thresh` | Hysteresis crossing detection → intervals |
| `zero_crossing_detect` | `series` | `primary: candidate` | `dead_samples` (5) | Zero-crossing with dead zone → intervals |
| `peak_selector` | `candidate` | `primary: candidate` | `count` (3), `strategy` ("last") | Keep last/first/best N peaks from detected intervals |

### Validation (6 blocks)

| Block | Input Kind | Output Port(s) | Params | Logic |
|-------|-----------|-----------------|--------|-------|
| `spm_range_gate` | `candidate` | `accepted/rejected: candidate` | `min_spm` (20), `max_spm` (120) | Pass if SPM in range |
| `interval_gate` | `candidate` | `accepted/rejected: candidate` | `min_s` (0.5), `max_s` (3.0) | Pass if interval in range |
| `consensus_band` | `candidate` (multi) | `accepted/rejected: candidate` | `tolerance_spm` (5.0) | Pass if SPM within ± tolerance of median |
| `harmonic_reject` | `candidate` | `accepted/rejected: candidate` | `fundamental_spm`, `tolerance` | Reject if SPM near 2× or 0.5× fundamental |
| `confidence_gate` | `candidate` | `accepted/rejected: candidate` | `min_confidence` (0.5) | Pass if confidence >= threshold |
| `fallback_selector` | `candidate` (multi) | `selected: candidate` | — | Pick candidate with highest confidence |

### Suivi (3 blocks)

| Block | Input Kind | Output Port(s) | Params | Logic |
|-------|-----------|-----------------|--------|-------|
| `kalman_2d` | `candidate` | `primary: estimate` | `process_noise`, `measurement_noise` | 2D Kalman (SPM + derivative), stateful |
| `confirmation_filter` | `estimate` | `primary: estimate` | `required_streak` (3) | Emit only after N consecutive valid estimates, stateful |
| `invalid_streak_reset` | `estimate` | `primary: estimate` | `max_invalid` (5) | Reset tracking state after N consecutive invalids, stateful |

## Dependencies

- `numpy` — already used
- `scipy` — `signal`, `interpolate`, `linalg`
- `pywt` — only for `wavelet_isolation`

## Changes to Existing Files

- **`blocks.py`** — Add timing instrumentation to `PipelineExecutor.run()`. Return `(exported, pipeline_diagnostics)` tuple.
- **Existing blocks** (`select_axis.py`, `highpass.py`, `autocorrelation.py`, `spm_range_gate.py`) — Add `params_schema` to manifests. Logic unchanged.
- **Empty stubs** (`kalman_2d.py`, `adaptive_peak_detect.py`, `schmitt_trigger.py`, `zero_crossing_detect.py`) — Replace with real implementations.

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
