# Y Consensus MUSIC Stroke-Rate Design

## Summary

Add a new Python stroke-rate estimator that operates on the `y` accelerometer axis and uses a staged DSP pipeline:

1. zero-phase Butterworth filtering with `scipy.signal.filtfilt`
2. YIN period estimation on the filtered 512-sample window
3. cepstral period estimation on the same filtered window
4. consensus-band selection from the YIN and cepstrum candidates
5. MUSIC-based high-resolution frequency refinement inside the selected band

The estimator is Python-only and is intended for offline analysis in the existing `tests/scripts/calculate_stroke_rate.py` workflow.

## Goals

- Preserve the current Python algorithm plugin contract: `ALGORITHM_NAME` plus `calculate(snapshot) -> float`.
- Keep the estimator aligned with the current snapshot format: one 512-sample window at 52 Hz.
- Use zero-phase filtering so the preprocessing step does not shift stroke timing.
- Use YIN and cepstrum together to define a narrower and more defensible search region before MUSIC refinement.
- Return a floating-point stroke-rate estimate on the same scale used by the existing Python algorithms.

## Non-Goals

- Replacing the firmware autocorrelation estimator.
- Reworking the CSV pipeline contract or snapshot schema.
- Claiming physical certainty from five displayed decimal places. The algorithm may compute high-resolution floating-point results, but the reported precision remains a formatting decision.
- Supporting axes other than `y` in this first implementation.

## Existing Context

The current Python estimator set in `tests/algorithms/python_stroke_rate` is a collection of small modules that call shared helpers from `common.py`. The batch pipeline in `tests/scripts/calculate_stroke_rate.py` discovers every Python algorithm module, feeds each one a `snapshot` with `series["x"]`, `series["y"]`, and `series["z"]`, and writes one output column per algorithm.

The new estimator should fit that same shape. Internally, the DSP stages should be placed in shared helpers inside `tests/algorithms/python_stroke_rate/common.py`, while the new algorithm module remains a thin wrapper over the `y` series.

## Proposed Files

- `tests/algorithms/python_stroke_rate/common.py`
- `tests/algorithms/python_stroke_rate/consensus_music_y.py`
- `tests/calculate_stroke_rate_test.py`

## Algorithm Contract

The new module will expose:

- `ALGORITHM_NAME = "consensus_music_y"`
- `calculate(snapshot: dict[str, object]) -> float`

`calculate` will read `snapshot["series"]["y"]`, reject invalid windows, and return `0.0` if a valid estimate cannot be produced.

## Signal Processing Pipeline

### 1. Window Validation

The estimator expects exactly 512 samples, matching the project snapshot capacity. If the input is missing, empty, shorter than the required length, or the sample rate is invalid, the estimator returns `0.0`.

### 2. Detrending And Zero-Phase Filtering

The `y` window is mean-centered before filtering. A Butterworth band-pass filter is then applied with `scipy.signal.butter` and `scipy.signal.filtfilt`.

The passband is derived from the current stroke-rate limits in `common.py`, converted to Hz:

- low cutoff near `MIN_STROKE_RATE_SPM / 60`
- high cutoff near `MAX_STROKE_RATE_SPM / 60`

A small margin should be applied to avoid over-trimming the true fundamental close to the configured limits, while keeping the passband safely below the Nyquist frequency.

This stage must remain deterministic and must not introduce phase delay.

### 3. YIN Candidate Extraction

YIN runs on the filtered window using only lags implied by the valid stroke-rate range. The implementation computes:

- the difference function over the valid lag interval
- the cumulative mean normalized difference function
- the first strong local minimum below a configurable threshold

If no such minimum is found, the implementation may fall back to the global minimum inside the valid lag range, provided it remains plausibly in-range. The result is a candidate period in samples.

### 4. Cepstral Candidate Extraction

Cepstral analysis runs on the same filtered window:

- compute the FFT
- take the log magnitude spectrum with numerical guarding against zero
- compute the real cepstrum
- search for the dominant quefrency peak inside the valid period interval

The result is a second candidate period in samples.

### 5. Consensus-Band Selection

YIN and cepstrum are compared using the approved tolerance:

- agreement threshold = `max(1 sample, 5% of period)`

If the two candidates agree within tolerance, the estimator treats this as consensus and defines a narrow refinement band centered around the shared period. If they disagree, the estimator must still continue. In that case it defines a wider band that spans both candidates, clipped to the legal stroke-rate range.

If one candidate is invalid and the other is valid, the valid candidate defines the refinement band by itself. If both are invalid, the estimator returns `0.0`.

### 6. MUSIC Refinement

MUSIC is used only as a local high-resolution refiner inside the band defined above.

The implementation should:

- build a trajectory or covariance matrix from the filtered window
- estimate signal and noise subspaces with `numpy.linalg.eigh`
- evaluate the MUSIC pseudospectrum over a dense frequency grid inside the refinement band
- choose the peak pseudospectrum frequency as the final estimate

The resulting frequency in Hz is converted to stroke rate using:

- `stroke_rate_spm = frequency_hz * 60.0`

If the covariance decomposition or pseudospectrum evaluation becomes numerically invalid, the estimator returns `0.0`.

## Helper Structure

The shared helpers in `common.py` should be small and testable. Expected helper responsibilities:

- filter design and zero-phase filtering
- conversion between stroke-rate bounds, frequency bounds, and lag bounds
- YIN candidate extraction
- cepstral candidate extraction
- consensus-band construction
- MUSIC refinement
- top-level estimator orchestration for `y`

The thin algorithm module should only load the `y` series from the snapshot and delegate to the shared estimator helper.

## Error Handling

The estimator should fail closed and return `0.0` for:

- invalid sample counts
- invalid or degenerate filter parameters
- all-zero or numerically flat signals that provide no useful periodic structure
- absent YIN and cepstrum candidates
- linear algebra failures during MUSIC
- any refined frequency outside the allowed stroke-rate range

No exceptions should escape into the batch CSV pipeline for normal bad-input cases.

## Testing Strategy

Follow test-driven development for implementation. Add targeted tests for the DSP helpers and one integration-level algorithm test.

### Unit-Level Coverage

- zero-phase filtering preserves the index of a known transient or periodic reference point
- YIN returns the expected period for a synthetic periodic waveform in the allowed range
- cepstrum returns the expected period for a synthetic periodic waveform in the allowed range
- consensus-band selection chooses a narrow band on agreement
- consensus-band selection spans both candidates on disagreement
- MUSIC refines a known synthetic frequency more accurately than coarse lag-based estimation
- invalid inputs return `0.0` or `None` according to the helper contract being tested

### Integration-Level Coverage

- `consensus_music_y.calculate(...)` accepts the existing snapshot structure
- the new algorithm appears in `discover_python_algorithms()`
- the estimator returns a nonzero, in-range value for representative valid input

## Tradeoffs

### Why This Design

This design keeps the repo's existing algorithm-discovery model intact while introducing more advanced offline DSP stages only inside the Python test path. It also avoids letting MUSIC search the full range blindly, which would make the result more fragile and harder to reason about.

### Accepted Costs

- `numpy` and `scipy` become required for this Python estimator path
- the estimator is more computationally expensive than autocorrelation
- a five-decimal floating-point result should be interpreted as computational resolution, not measurement certainty

## Open Decisions Resolved During Brainstorming

- axis: `y`
- Python dependencies: `numpy` and `scipy` are acceptable
- YIN/cepstrum disagreement handling: continue with a widened refinement band and still run MUSIC
- agreement tolerance: `max(1 sample, 5%)`

## Implementation Notes

- Reuse the current `SAMPLE_STORE_CAPACITY`, `SAMPLE_RATE_HZ`, `MIN_STROKE_RATE_SPM`, and `MAX_STROKE_RATE_SPM` constants from `common.py`.
- Keep helper APIs explicit and numeric rather than passing entire snapshots deep into the DSP stack.
- Preserve existing behavior for all current estimator modules.
- The CSV writer currently formats rates to three decimals. If higher printed precision is later required, that should be handled as a separate output-format decision rather than inside the estimator.
