# Analysis Block Platform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement all 33 Python analysis blocks across 6 groups, add executor timing + state propagation, and update existing blocks with params_schema.

**Architecture:** Each block is a standalone `.py` file with a class exposing `manifest` + `run()` + module-level `BLOCK` singleton. Blocks are thin scipy/numpy wrappers. The `PipelineExecutor` gets timing instrumentation and stateful block support.

**Tech Stack:** Python, numpy, scipy, pywt (wavelet only)

**Spec:** `docs/superpowers/specs/2026-04-07-analysis-block-platform-design.md`

**Test command:** `python -m pytest analysis/tests/ -v`

---

## File Map

### Modified files
- `analysis/scripts/blocks.py` — executor timing + state propagation
- `analysis/algorithms/representation/py/select_axis.py` — add params_schema
- `analysis/algorithms/estimation/py/autocorrelation.py` — add params_schema, remove old import
- `analysis/algorithms/validation/py/spm_range_gate.py` — add params_schema
- `analysis/tests/analysis_pipeline_executor_test.py` — update for new return type
- `analysis/tests/analysis_python_blocks_test.py` — add tests for new blocks

### Renamed files
- `analysis/algorithms/pretraitement/py/highpass.py` → `analysis/algorithms/pretraitement/py/hpf_gravity.py`

### New block files (25 files)
- `analysis/algorithms/representation/py/select_x.py`
- `analysis/algorithms/representation/py/select_y.py`
- `analysis/algorithms/representation/py/select_z.py`
- `analysis/algorithms/representation/py/vector_magnitude.py`
- `analysis/algorithms/pretraitement/py/lowpass.py`
- `analysis/algorithms/pretraitement/py/bandpass.py`
- `analysis/algorithms/pretraitement/py/zero_phase_bandpass.py`
- `analysis/algorithms/pretraitement/py/wavelet_isolation.py`
- `analysis/algorithms/pretraitement/py/window_trim.py`
- `analysis/algorithms/estimation/py/fft_dominant.py`
- `analysis/algorithms/estimation/py/yin_period.py`
- `analysis/algorithms/estimation/py/cepstrum_period.py`
- `analysis/algorithms/estimation/py/music_refine.py`
- `analysis/algorithms/estimation/py/hilbert_freq.py`
- `analysis/algorithms/estimation/py/interval_to_spm.py`
- `analysis/algorithms/estimation/py/crossings_to_spm.py`
- `analysis/algorithms/detection/py/adaptive_envelope.py`
- `analysis/algorithms/detection/py/peak_selector.py`
- `analysis/algorithms/validation/py/interval_gate.py`
- `analysis/algorithms/validation/py/consensus_band.py`
- `analysis/algorithms/validation/py/harmonic_reject.py`
- `analysis/algorithms/validation/py/confidence_gate.py`
- `analysis/algorithms/validation/py/fallback_selector.py`
- `analysis/algorithms/suivi/py/confirmation_filter.py`
- `analysis/algorithms/suivi/py/invalid_streak_reset.py`

### Replaced stubs (4 files — already exist but empty)
- `analysis/algorithms/detection/py/adaptive_peak_detect.py`
- `analysis/algorithms/detection/py/schmitt_trigger.py`
- `analysis/algorithms/detection/py/zero_crossing_detect.py`
- `analysis/algorithms/suivi/py/kalman_2d.py`

### New test file
- `analysis/tests/analysis_all_blocks_test.py` — contract + smoke tests for all 33 blocks

---

## Task 1: Executor — Timing + State Propagation

**Files:**
- Modify: `analysis/scripts/blocks.py:64-92`
- Modify: `analysis/tests/analysis_pipeline_executor_test.py`

- [ ] **Step 1: Write failing test for executor timing**

In `analysis/tests/analysis_pipeline_executor_test.py`, add:

```python
def test_executor_returns_timing_diagnostics():
    graph = {
        "nodes": [{"node_id": "n1", "block_id": "representation.inline", "params": {}}],
        "inputs": {"n1.source": "input.raw"},
        "outputs": {"final": "n1.primary"},
    }
    packet = Packet(kind="raw_window", data={"values": [1, 2, 3]})
    result, diagnostics = PipelineExecutor({"representation.inline": _InlineBlock()}).run(graph, {"input.raw": [packet]})
    assert result["final"][0].kind == "series"
    assert "n1" in diagnostics["node_timings"]
    assert diagnostics["node_timings"]["n1"]["elapsed_ms"] >= 0
    assert diagnostics["total_elapsed_ms"] >= 0
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m pytest analysis/tests/analysis_pipeline_executor_test.py::test_executor_returns_timing_diagnostics -v`
Expected: FAIL — `run()` returns dict, not tuple

- [ ] **Step 3: Write failing test for state propagation**

```python
class _StatefulCounter:
    manifest = BlockManifest(
        block_id="estimation.counter",
        group="estimation",
        language="py",
        entrypoint="inline:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "candidate"},
        stateful=True,
    )

    def run(self, input_packets, params, state):
        count = state.get("count", 0) + 1
        return BlockResult(
            outputs={"primary": [Packet(kind="candidate", data={"count": count})]},
            state={"count": count},
        )


def test_executor_propagates_state_across_runs():
    blocks = {"estimation.counter": _StatefulCounter()}
    executor = PipelineExecutor(blocks)
    graph = {
        "nodes": [{"node_id": "n1", "block_id": "estimation.counter", "params": {}}],
        "inputs": {"n1.source": "input.data"},
        "outputs": {"final": "n1.primary"},
    }
    packet = Packet(kind="series", data={"values": [1.0]})
    r1, _ = executor.run(graph, {"input.data": [packet]})
    r2, _ = executor.run(graph, {"input.data": [packet]})
    assert r1["final"][0].data["count"] == 1
    assert r2["final"][0].data["count"] == 2
```

- [ ] **Step 4: Run test to verify it fails**

Run: `python -m pytest analysis/tests/analysis_pipeline_executor_test.py::test_executor_propagates_state_across_runs -v`
Expected: FAIL

- [ ] **Step 5: Implement timing + state in PipelineExecutor**

In `analysis/scripts/blocks.py`, update `PipelineExecutor`:

```python
import time

class PipelineExecutor:
    def __init__(self, blocks: dict[str, object]):
        self.blocks = blocks
        self.node_states: dict[str, dict] = {}

    def run(self, graph: dict[str, object], inputs: dict[str, list[object]]):
        node_outputs: dict[str, dict[str, list[object]]] = {}
        node_timings: dict[str, dict] = {}
        t_start = time.perf_counter()

        for node in graph["nodes"]:
            block = self.blocks[node["block_id"]]
            bound_inputs = {}
            for target, source in graph.get("inputs", {}).items():
                node_id, port = target.split(".", 1)
                if node_id == node["node_id"]:
                    if source in inputs:
                        packets = inputs[source]
                    else:
                        src_node, src_port = source.split(".", 1)
                        packets = node_outputs[src_node][src_port]
                    for packet in packets:
                        if packet.kind not in block.manifest.input_kinds:
                            raise ValueError(f"packet kind mismatch for {block.manifest.block_id}")
                    bound_inputs[port] = packets

            nid = node["node_id"]
            state = self.node_states.get(nid, {})
            t0 = time.perf_counter()
            result = block.run(bound_inputs, node.get("params", {}), state)
            t1 = time.perf_counter()

            node_outputs[nid] = result.outputs
            if block.manifest.stateful:
                self.node_states[nid] = result.state
            node_timings[nid] = {
                "block_id": block.manifest.block_id,
                "elapsed_ms": round((t1 - t0) * 1000, 3),
            }

        exported = {}
        for name, source in graph["outputs"].items():
            node_id, port = source.split(".", 1)
            exported[name] = node_outputs[node_id][port]

        diagnostics = {
            "node_timings": node_timings,
            "total_elapsed_ms": round((time.perf_counter() - t_start) * 1000, 3),
        }
        return exported, diagnostics
```

- [ ] **Step 6: Fix existing executor tests for new return type**

Update `test_executor_routes_named_outputs_between_nodes`:
```python
result, _ = PipelineExecutor(...).run(graph, {"input.raw": [packet]})
```

Update `test_executor_rejects_kind_mismatch` — no change needed, ValueError is raised before return.

- [ ] **Step 7: Run all executor tests**

Run: `python -m pytest analysis/tests/analysis_pipeline_executor_test.py -v`
Expected: all PASS

- [ ] **Step 8: Commit**

```bash
git add analysis/scripts/blocks.py analysis/tests/analysis_pipeline_executor_test.py
git commit -m "feat: add executor timing instrumentation and state propagation"
```

---

## Task 2: Update Existing Blocks — params_schema

**Files:**
- Modify: `analysis/algorithms/representation/py/select_axis.py`
- Modify: `analysis/algorithms/estimation/py/autocorrelation.py`
- Modify: `analysis/algorithms/validation/py/spm_range_gate.py`

- [ ] **Step 1: Add params_schema to select_axis**

```python
params_schema = {
    "axis": {"type": "str", "default": "y", "enum": ["x", "y", "z"], "description": "IMU axis to extract"},
}
```

- [ ] **Step 2: Add params_schema to autocorrelation and rewrite with numpy**

Remove the `sys.path` hack and `from common import ...`. Rewrite `run()` to use numpy directly:

```python
from __future__ import annotations
import numpy as np
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class AutocorrelationBlock:
    manifest = BlockManifest(
        block_id="estimation.autocorrelation",
        group="estimation",
        language="py",
        entrypoint="analysis.algorithms.estimation.py.autocorrelation:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "candidate"},
        stateful=False,
        params_schema={
            "min_lag_samples": {"type": "int", "default": 15, "min": 1, "description": "Minimum lag in samples"},
            "max_lag_samples": {"type": "int", "default": 160, "min": 2, "description": "Maximum lag in samples"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        min_lag = int(params.get("min_lag_samples", 15))
        max_lag = int(params.get("max_lag_samples", min(160, len(values) // 2)))
        values = values - values.mean()
        full = np.correlate(values, values, mode="full")
        acf = full[len(values) - 1:]
        if acf[0] != 0:
            acf = acf / acf[0]
        segment = acf[min_lag:max_lag + 1]
        if len(segment) == 0:
            return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": 0.0}, sample_rate_hz=sr)]})
        best_lag = min_lag + int(np.argmax(segment))
        spm = 60.0 * sr / best_lag
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, axis=source.axis, sample_rate_hz=sr)]})

BLOCK = AutocorrelationBlock()
```

- [ ] **Step 3: Add params_schema to spm_range_gate**

```python
params_schema = {
    "min_spm": {"type": "float", "default": 20.0, "min": 0, "description": "Minimum valid SPM"},
    "max_spm": {"type": "float", "default": 120.0, "min": 1, "description": "Maximum valid SPM"},
}
```

- [ ] **Step 4: Run existing block tests**

Run: `python -m pytest analysis/tests/analysis_python_blocks_test.py -v`
Expected: all PASS

- [ ] **Step 5: Commit**

```bash
git add analysis/algorithms/representation/py/select_axis.py analysis/algorithms/estimation/py/autocorrelation.py analysis/algorithms/validation/py/spm_range_gate.py
git commit -m "feat: add params_schema to existing blocks"
```

---

## Task 3: Rename highpass → hpf_gravity

**Files:**
- Delete: `analysis/algorithms/pretraitement/py/highpass.py`
- Create: `analysis/algorithms/pretraitement/py/hpf_gravity.py`

- [ ] **Step 1: Create hpf_gravity.py with scipy implementation**

```python
from __future__ import annotations
import numpy as np
from scipy.signal import butter, sosfilt
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class HpfGravityBlock:
    manifest = BlockManifest(
        block_id="pretraitement.hpf_gravity",
        group="pretraitement",
        language="py",
        entrypoint="analysis.algorithms.pretraitement.py.hpf_gravity:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "series"},
        stateful=False,
        params_schema={
            "cutoff_hz": {"type": "float", "default": 0.5, "min": 0.01, "description": "High-pass cutoff frequency"},
            "order": {"type": "int", "default": 4, "min": 1, "max": 10, "description": "Filter order"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        cutoff = float(params.get("cutoff_hz", 0.5))
        order = int(params.get("order", 4))
        sos = butter(order, cutoff, btype="high", fs=sr, output="sos")
        filtered = sosfilt(sos, values).tolist()
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": filtered}, axis=source.axis, sample_rate_hz=sr)]})

BLOCK = HpfGravityBlock()
```

- [ ] **Step 2: Delete old highpass.py**

```bash
git rm analysis/algorithms/pretraitement/py/highpass.py
```

- [ ] **Step 3: Run tests**

Run: `python -m pytest analysis/tests/ -v`
Expected: PASS (harness test discovers hpf_gravity instead of highpass)

- [ ] **Step 4: Commit**

```bash
git add analysis/algorithms/pretraitement/py/hpf_gravity.py
git commit -m "feat: rename highpass to hpf_gravity with scipy implementation"
```

---

## Task 4: Representation Blocks

**Files:**
- Create: `analysis/algorithms/representation/py/select_x.py`
- Create: `analysis/algorithms/representation/py/select_y.py`
- Create: `analysis/algorithms/representation/py/select_z.py`
- Create: `analysis/algorithms/representation/py/vector_magnitude.py`

- [ ] **Step 1: Write tests in analysis_all_blocks_test.py**

Create `analysis/tests/analysis_all_blocks_test.py`:

```python
import numpy as np
from analysis.scripts.blocks import Packet


RAW_WINDOW = Packet(
    kind="raw_window",
    data={"series": {"x": [1.0, 2.0, 3.0], "y": [4.0, 5.0, 6.0], "z": [7.0, 8.0, 9.0]}},
    sample_rate_hz=52.0,
)

SERIES_PACKET = Packet(kind="series", data={"values": list(np.sin(np.linspace(0, 4 * np.pi, 256)))}, sample_rate_hz=52.0)

# Clean 1 Hz sine (60 SPM) for estimation tests
_SR = 52.0
_T = np.arange(512) / _SR
_SINE_1HZ = Packet(kind="series", data={"values": np.sin(2 * np.pi * 1.0 * _T).tolist()}, sample_rate_hz=_SR)


def test_select_x():
    from analysis.algorithms.representation.py.select_x import BLOCK
    result = BLOCK.run({"source": [RAW_WINDOW]}, {}, {})
    assert result.outputs["primary"][0].data["values"] == [1.0, 2.0, 3.0]
    assert result.outputs["primary"][0].kind == "series"


def test_select_y():
    from analysis.algorithms.representation.py.select_y import BLOCK
    result = BLOCK.run({"source": [RAW_WINDOW]}, {}, {})
    assert result.outputs["primary"][0].data["values"] == [4.0, 5.0, 6.0]


def test_select_z():
    from analysis.algorithms.representation.py.select_z import BLOCK
    result = BLOCK.run({"source": [RAW_WINDOW]}, {}, {})
    assert result.outputs["primary"][0].data["values"] == [7.0, 8.0, 9.0]


def test_vector_magnitude():
    from analysis.algorithms.representation.py.vector_magnitude import BLOCK
    result = BLOCK.run({"source": [RAW_WINDOW]}, {}, {})
    values = result.outputs["primary"][0].data["values"]
    expected_0 = (1.0**2 + 4.0**2 + 7.0**2) ** 0.5
    assert abs(values[0] - expected_0) < 1e-9
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py::test_select_x -v`
Expected: FAIL — ImportError

- [ ] **Step 3: Implement select_x.py**

```python
from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class SelectXBlock:
    manifest = BlockManifest(
        block_id="representation.select_x",
        group="representation",
        language="py",
        entrypoint="analysis.algorithms.representation.py.select_x:BLOCK",
        input_kinds=["raw_window"],
        output_ports={"primary": "series"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = list(source.data["series"]["x"])
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": values}, axis="x", sample_rate_hz=source.sample_rate_hz)]})

BLOCK = SelectXBlock()
```

- [ ] **Step 4: Implement select_y.py** (same pattern, axis="y")

- [ ] **Step 5: Implement select_z.py** (same pattern, axis="z")

- [ ] **Step 6: Implement vector_magnitude.py**

```python
from __future__ import annotations
import numpy as np
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class VectorMagnitudeBlock:
    manifest = BlockManifest(
        block_id="representation.vector_magnitude",
        group="representation",
        language="py",
        entrypoint="analysis.algorithms.representation.py.vector_magnitude:BLOCK",
        input_kinds=["raw_window"],
        output_ports={"primary": "series"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        s = source.data["series"]
        x, y, z = np.array(s["x"]), np.array(s["y"]), np.array(s["z"])
        mag = np.sqrt(x**2 + y**2 + z**2).tolist()
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": mag}, axis="magnitude", sample_rate_hz=source.sample_rate_hz)]})

BLOCK = VectorMagnitudeBlock()
```

- [ ] **Step 7: Run tests**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py -k "select_x or select_y or select_z or vector_magnitude" -v`
Expected: all PASS

- [ ] **Step 8: Commit**

```bash
git add analysis/algorithms/representation/py/select_x.py analysis/algorithms/representation/py/select_y.py analysis/algorithms/representation/py/select_z.py analysis/algorithms/representation/py/vector_magnitude.py analysis/tests/analysis_all_blocks_test.py
git commit -m "feat: add representation blocks — select_x, select_y, select_z, vector_magnitude"
```

---

## Task 5: Pretraitement Blocks

**Files:**
- Create: `analysis/algorithms/pretraitement/py/lowpass.py`
- Create: `analysis/algorithms/pretraitement/py/bandpass.py`
- Create: `analysis/algorithms/pretraitement/py/zero_phase_bandpass.py`
- Create: `analysis/algorithms/pretraitement/py/wavelet_isolation.py`
- Create: `analysis/algorithms/pretraitement/py/window_trim.py`

- [ ] **Step 1: Write tests**

Append to `analysis/tests/analysis_all_blocks_test.py`:

```python
def test_lowpass():
    from analysis.algorithms.pretraitement.py.lowpass import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {"cutoff_hz": 5.0}, {})
    assert result.outputs["primary"][0].kind == "series"
    assert len(result.outputs["primary"][0].data["values"]) == 256


def test_bandpass():
    from analysis.algorithms.pretraitement.py.bandpass import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {}, {})
    assert result.outputs["primary"][0].kind == "series"


def test_zero_phase_bandpass():
    from analysis.algorithms.pretraitement.py.zero_phase_bandpass import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {}, {})
    assert result.outputs["primary"][0].kind == "series"


def test_wavelet_isolation():
    from analysis.algorithms.pretraitement.py.wavelet_isolation import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {}, {})
    assert result.outputs["primary"][0].kind == "series"


def test_window_trim_end():
    from analysis.algorithms.pretraitement.py.window_trim import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {"keep_samples": 64, "anchor": "end"}, {})
    assert len(result.outputs["primary"][0].data["values"]) == 64


def test_window_trim_start():
    from analysis.algorithms.pretraitement.py.window_trim import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {"keep_samples": 64, "anchor": "start"}, {})
    assert len(result.outputs["primary"][0].data["values"]) == 64
```

- [ ] **Step 2: Run to verify failures**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py -k "lowpass or bandpass or zero_phase or wavelet or window_trim" -v`
Expected: all FAIL

- [ ] **Step 3: Implement lowpass.py**

```python
from __future__ import annotations
import numpy as np
from scipy.signal import butter, sosfilt
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class LowpassBlock:
    manifest = BlockManifest(
        block_id="pretraitement.lowpass",
        group="pretraitement",
        language="py",
        entrypoint="analysis.algorithms.pretraitement.py.lowpass:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "series"},
        stateful=False,
        params_schema={
            "cutoff_hz": {"type": "float", "default": 5.0, "min": 0.01, "description": "Low-pass cutoff frequency"},
            "order": {"type": "int", "default": 4, "min": 1, "max": 10, "description": "Filter order"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        cutoff = float(params.get("cutoff_hz", 5.0))
        order = int(params.get("order", 4))
        sos = butter(order, cutoff, btype="low", fs=sr, output="sos")
        filtered = sosfilt(sos, values).tolist()
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": filtered}, axis=source.axis, sample_rate_hz=sr)]})

BLOCK = LowpassBlock()
```

- [ ] **Step 4: Implement bandpass.py**

Same pattern as lowpass, `btype="band"`, `Wn=[low_hz, high_hz]`. Params: `low_hz` (0.5), `high_hz` (5.0), `order` (4).

- [ ] **Step 5: Implement zero_phase_bandpass.py**

Same as bandpass but use `sosfiltfilt` instead of `sosfilt`. Params: `low_hz` (0.5), `high_hz` (5.0), `order` (4).

- [ ] **Step 6: Implement wavelet_isolation.py**

```python
from __future__ import annotations
import numpy as np
import pywt
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class WaveletIsolationBlock:
    manifest = BlockManifest(
        block_id="pretraitement.wavelet_isolation",
        group="pretraitement",
        language="py",
        entrypoint="analysis.algorithms.pretraitement.py.wavelet_isolation:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "series"},
        stateful=False,
        params_schema={
            "wavelet": {"type": "str", "default": "db4", "description": "Wavelet family"},
            "level": {"type": "int", "default": 4, "min": 1, "max": 10, "description": "Decomposition level"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        wavelet = params.get("wavelet", "db4")
        level = int(params.get("level", 4))
        coeffs = pywt.wavedec(values, wavelet, level=level)
        coeffs[0] = np.zeros_like(coeffs[0])
        reconstructed = pywt.waverec(coeffs, wavelet)[:len(values)].tolist()
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": reconstructed}, axis=source.axis, sample_rate_hz=source.sample_rate_hz)]})

BLOCK = WaveletIsolationBlock()
```

- [ ] **Step 7: Implement window_trim.py**

```python
from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class WindowTrimBlock:
    manifest = BlockManifest(
        block_id="pretraitement.window_trim",
        group="pretraitement",
        language="py",
        entrypoint="analysis.algorithms.pretraitement.py.window_trim:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "series"},
        stateful=False,
        params_schema={
            "keep_samples": {"type": "int", "default": 256, "min": 1, "description": "Number of samples to keep"},
            "anchor": {"type": "str", "default": "end", "enum": ["start", "end"], "description": "Keep from start or end"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = list(source.data["values"])
        n = int(params.get("keep_samples", 256))
        anchor = params.get("anchor", "end")
        if anchor == "end":
            trimmed = values[-n:]
        else:
            trimmed = values[:n]
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": trimmed}, axis=source.axis, sample_rate_hz=source.sample_rate_hz)]})

BLOCK = WindowTrimBlock()
```

- [ ] **Step 8: Run tests**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py -k "lowpass or bandpass or zero_phase or wavelet or window_trim" -v`
Expected: all PASS

- [ ] **Step 9: Commit**

```bash
git add analysis/algorithms/pretraitement/py/lowpass.py analysis/algorithms/pretraitement/py/bandpass.py analysis/algorithms/pretraitement/py/zero_phase_bandpass.py analysis/algorithms/pretraitement/py/wavelet_isolation.py analysis/algorithms/pretraitement/py/window_trim.py analysis/tests/analysis_all_blocks_test.py
git commit -m "feat: add pretraitement blocks — lowpass, bandpass, zero_phase_bandpass, wavelet_isolation, window_trim"
```

---

## Task 6: Estimation Blocks

**Files:**
- Create: `analysis/algorithms/estimation/py/fft_dominant.py`
- Create: `analysis/algorithms/estimation/py/yin_period.py`
- Create: `analysis/algorithms/estimation/py/cepstrum_period.py`
- Create: `analysis/algorithms/estimation/py/music_refine.py`
- Create: `analysis/algorithms/estimation/py/hilbert_freq.py`
- Create: `analysis/algorithms/estimation/py/interval_to_spm.py`
- Create: `analysis/algorithms/estimation/py/crossings_to_spm.py`

- [ ] **Step 1: Write tests**

Append to `analysis/tests/analysis_all_blocks_test.py`:

```python
def test_fft_dominant():
    from analysis.algorithms.estimation.py.fft_dominant import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    spm = result.outputs["primary"][0].data["spm"]
    assert 55 < spm < 65  # ~60 SPM


def test_yin_period():
    from analysis.algorithms.estimation.py.yin_period import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    spm = result.outputs["primary"][0].data["spm"]
    assert 55 < spm < 65


def test_cepstrum_period():
    from analysis.algorithms.estimation.py.cepstrum_period import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    spm = result.outputs["primary"][0].data["spm"]
    assert 55 < spm < 65


def test_music_refine():
    from analysis.algorithms.estimation.py.music_refine import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    spm = result.outputs["primary"][0].data["spm"]
    assert 55 < spm < 65


def test_hilbert_freq():
    from analysis.algorithms.estimation.py.hilbert_freq import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    spm = result.outputs["primary"][0].data["spm"]
    assert 55 < spm < 65


def test_interval_to_spm():
    from analysis.algorithms.estimation.py.interval_to_spm import BLOCK
    pkt = Packet(kind="candidate", data={"intervals": [1.0, 1.0, 1.0]}, sample_rate_hz=52.0)
    result = BLOCK.run({"source": [pkt]}, {}, {})
    assert abs(result.outputs["primary"][0].data["spm"] - 60.0) < 0.1


def test_crossings_to_spm():
    from analysis.algorithms.estimation.py.crossings_to_spm import BLOCK
    pkt = Packet(kind="candidate", data={"crossings": 10, "window_seconds": 5.0}, sample_rate_hz=52.0)
    result = BLOCK.run({"source": [pkt]}, {}, {})
    spm = result.outputs["primary"][0].data["spm"]
    assert abs(spm - 60.0) < 0.1  # 10 crossings / 5s = 2 Hz half-cycles -> 1 Hz -> 60 SPM
```

- [ ] **Step 2: Run to verify failures**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py -k "fft_dominant or yin or cepstrum or music or hilbert or interval_to or crossings_to" -v`
Expected: all FAIL

- [ ] **Step 3: Implement fft_dominant.py**

```python
from __future__ import annotations
import numpy as np
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class FftDominantBlock:
    manifest = BlockManifest(
        block_id="estimation.fft_dominant",
        group="estimation",
        language="py",
        entrypoint="analysis.algorithms.estimation.py.fft_dominant:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "candidate"},
        stateful=False,
        params_schema={
            "min_hz": {"type": "float", "default": 0.33, "description": "Minimum frequency"},
            "max_hz": {"type": "float", "default": 2.0, "description": "Maximum frequency"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        min_hz = float(params.get("min_hz", 0.33))
        max_hz = float(params.get("max_hz", 2.0))
        spectrum = np.abs(np.fft.rfft(values - values.mean()))
        freqs = np.fft.rfftfreq(len(values), d=1.0 / sr)
        mask = (freqs >= min_hz) & (freqs <= max_hz)
        if not mask.any():
            return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": 0.0}, sample_rate_hz=sr)]})
        peak_idx = np.argmax(spectrum[mask])
        freq = freqs[mask][peak_idx]
        spm = freq * 60.0
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, axis=source.axis, sample_rate_hz=sr)]})

BLOCK = FftDominantBlock()
```

- [ ] **Step 4: Implement yin_period.py**

YIN difference function: for each lag tau, compute `d(tau) = sum((x[t] - x[t+tau])^2)`. Cumulative mean normalization. Find first tau below threshold. Convert to SPM via `60 * sr / tau`.

Params: `threshold` (0.15), `min_hz` (0.33), `max_hz` (2.0). Convert hz bounds to lag bounds: `min_lag = int(sr / max_hz)`, `max_lag = int(sr / min_hz)`.

- [ ] **Step 5: Implement cepstrum_period.py**

Real cepstrum: `np.fft.irfft(np.log(np.abs(np.fft.rfft(x)) + 1e-12))`. Find peak in quefrency range `[int(sr/max_hz), int(sr/min_hz)]`. Convert quefrency to SPM: `60 * sr / peak_quefrency`.

Params: `min_hz` (0.33), `max_hz` (2.0).

- [ ] **Step 6: Implement music_refine.py**

Build `(subspace_dim x subspace_dim)` autocorrelation matrix from signal using Toeplitz structure. Eigendecompose with `np.linalg.eigh`. Noise subspace = eigenvectors for smallest eigenvalues. Scan frequencies in `[min_hz, max_hz]`, compute MUSIC pseudo-spectrum `1 / (a^H * En * En^H * a)`. Find peak frequency, convert to SPM.

Params: `num_signals` (1), `min_hz` (0.33), `max_hz` (2.0), `subspace_dim` (16).

- [ ] **Step 7: Implement hilbert_freq.py**

```python
from __future__ import annotations
import numpy as np
from scipy.signal import hilbert
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class HilbertFreqBlock:
    manifest = BlockManifest(
        block_id="estimation.hilbert_freq",
        group="estimation",
        language="py",
        entrypoint="analysis.algorithms.estimation.py.hilbert_freq:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "candidate"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        analytic = hilbert(values)
        phase = np.unwrap(np.angle(analytic))
        inst_freq = np.diff(phase) / (2 * np.pi) * sr
        median_freq = float(np.median(inst_freq[inst_freq > 0])) if np.any(inst_freq > 0) else 0.0
        spm = median_freq * 60.0
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, axis=source.axis, sample_rate_hz=sr)]})

BLOCK = HilbertFreqBlock()
```

- [ ] **Step 8: Implement interval_to_spm.py**

```python
from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class IntervalToSpmBlock:
    manifest = BlockManifest(
        block_id="estimation.interval_to_spm",
        group="estimation",
        language="py",
        entrypoint="analysis.algorithms.estimation.py.interval_to_spm:BLOCK",
        input_kinds=["candidate"],
        output_ports={"primary": "candidate"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        intervals = source.data.get("intervals", [])
        if not intervals:
            return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": 0.0}, sample_rate_hz=source.sample_rate_hz)]})
        mean_interval = sum(intervals) / len(intervals)
        spm = 60.0 / mean_interval if mean_interval > 0 else 0.0
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, sample_rate_hz=source.sample_rate_hz)]})

BLOCK = IntervalToSpmBlock()
```

- [ ] **Step 9: Implement crossings_to_spm.py**

```python
from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class CrossingsToSpmBlock:
    manifest = BlockManifest(
        block_id="estimation.crossings_to_spm",
        group="estimation",
        language="py",
        entrypoint="analysis.algorithms.estimation.py.crossings_to_spm:BLOCK",
        input_kinds=["candidate"],
        output_ports={"primary": "candidate"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        crossings = source.data.get("crossings", 0)
        window_s = source.data.get("window_seconds", 1.0)
        half_cycles_per_s = crossings / window_s if window_s > 0 else 0
        spm = half_cycles_per_s * 30.0  # 2 crossings per cycle -> /2 * 60
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, sample_rate_hz=source.sample_rate_hz)]})

BLOCK = CrossingsToSpmBlock()
```

- [ ] **Step 10: Run tests**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py -k "fft_dominant or yin or cepstrum or music or hilbert or interval_to or crossings_to" -v`
Expected: all PASS

- [ ] **Step 11: Commit**

```bash
git add analysis/algorithms/estimation/py/fft_dominant.py analysis/algorithms/estimation/py/yin_period.py analysis/algorithms/estimation/py/cepstrum_period.py analysis/algorithms/estimation/py/music_refine.py analysis/algorithms/estimation/py/hilbert_freq.py analysis/algorithms/estimation/py/interval_to_spm.py analysis/algorithms/estimation/py/crossings_to_spm.py analysis/tests/analysis_all_blocks_test.py
git commit -m "feat: add estimation blocks — fft, yin, cepstrum, music, hilbert, converters"
```

---

## Task 7: Detection Blocks

**Files:**
- Create: `analysis/algorithms/detection/py/adaptive_envelope.py`
- Replace: `analysis/algorithms/detection/py/adaptive_peak_detect.py`
- Replace: `analysis/algorithms/detection/py/schmitt_trigger.py`
- Replace: `analysis/algorithms/detection/py/zero_crossing_detect.py`
- Create: `analysis/algorithms/detection/py/peak_selector.py`

- [ ] **Step 1: Write tests**

Append to `analysis/tests/analysis_all_blocks_test.py`:

```python
def test_adaptive_envelope():
    from analysis.algorithms.detection.py.adaptive_envelope import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {}, {})
    assert result.outputs["primary"][0].kind == "series"
    values = result.outputs["primary"][0].data["values"]
    assert all(v >= 0 for v in values)


def test_adaptive_peak_detect():
    from analysis.algorithms.detection.py.adaptive_peak_detect import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {"min_distance_samples": 26, "prominence": 0.1}, {})
    pkt = result.outputs["primary"][0]
    assert pkt.kind == "candidate"
    assert "intervals" in pkt.data


def test_schmitt_trigger():
    from analysis.algorithms.detection.py.schmitt_trigger import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {"high_thresh": 0.3, "low_thresh": -0.3}, {})
    pkt = result.outputs["primary"][0]
    assert pkt.kind == "candidate"
    assert "intervals" in pkt.data


def test_zero_crossing_detect():
    from analysis.algorithms.detection.py.zero_crossing_detect import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    pkt = result.outputs["primary"][0]
    assert pkt.kind == "candidate"
    assert "crossings" in pkt.data


def test_peak_selector_last():
    from analysis.algorithms.detection.py.peak_selector import BLOCK
    pkt = Packet(kind="candidate", data={"intervals": [1.0, 1.1, 0.9, 1.0, 1.05]}, sample_rate_hz=52.0)
    result = BLOCK.run({"source": [pkt]}, {"count": 3, "strategy": "last"}, {})
    assert len(result.outputs["primary"][0].data["intervals"]) == 3
```

- [ ] **Step 2: Run to verify failures**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py -k "adaptive_envelope or adaptive_peak or schmitt or zero_crossing or peak_selector" -v`
Expected: all FAIL

- [ ] **Step 3: Implement adaptive_envelope.py**

`np.abs(scipy.signal.hilbert(values))` then low-pass at `smoothing_hz`. Output kind is `series`.

Params: `smoothing_hz` (1.0), `order` (4).

- [ ] **Step 4: Implement adaptive_peak_detect.py**

`scipy.signal.find_peaks` with `distance=min_distance_samples` and `prominence`. Compute intervals between peaks in seconds: `np.diff(peaks) / sr`. Output `data={"intervals": intervals.tolist()}`.

Params: `min_distance_samples` (26), `prominence` (0.1).

- [ ] **Step 5: Implement schmitt_trigger.py**

Walk through signal. Start armed high. On crossing above `high_thresh`, record sample index, switch to armed low. On crossing below `low_thresh`, switch to armed high. Compute intervals between triggers in seconds.

Params: `high_thresh` (required), `low_thresh` (required).

- [ ] **Step 6: Implement zero_crossing_detect.py**

Count positive-going zero crossings (signal goes from negative to non-negative). After each crossing, skip `dead_samples` before looking for next. Output `data={"crossings": count, "window_seconds": len(values) / sr}`.

Params: `dead_samples` (5).

- [ ] **Step 7: Implement peak_selector.py**

```python
from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class PeakSelectorBlock:
    manifest = BlockManifest(
        block_id="detection.peak_selector",
        group="detection",
        language="py",
        entrypoint="analysis.algorithms.detection.py.peak_selector:BLOCK",
        input_kinds=["candidate"],
        output_ports={"primary": "candidate"},
        stateful=False,
        params_schema={
            "count": {"type": "int", "default": 3, "min": 1, "description": "Number of intervals to keep"},
            "strategy": {"type": "str", "default": "last", "enum": ["last", "first"], "description": "Which intervals to keep"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        intervals = list(source.data.get("intervals", []))
        n = int(params.get("count", 3))
        strategy = params.get("strategy", "last")
        if strategy == "last":
            trimmed = intervals[-n:]
        else:
            trimmed = intervals[:n]
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"intervals": trimmed}, sample_rate_hz=source.sample_rate_hz)]})

BLOCK = PeakSelectorBlock()
```

- [ ] **Step 8: Run tests**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py -k "adaptive_envelope or adaptive_peak or schmitt or zero_crossing or peak_selector" -v`
Expected: all PASS

- [ ] **Step 9: Commit**

```bash
git add analysis/algorithms/detection/py/adaptive_envelope.py analysis/algorithms/detection/py/adaptive_peak_detect.py analysis/algorithms/detection/py/schmitt_trigger.py analysis/algorithms/detection/py/zero_crossing_detect.py analysis/algorithms/detection/py/peak_selector.py analysis/tests/analysis_all_blocks_test.py
git commit -m "feat: add detection blocks — envelope, peak_detect, schmitt, zero_crossing, peak_selector"
```

---

## Task 8: Validation Blocks

**Files:**
- Create: `analysis/algorithms/validation/py/interval_gate.py`
- Create: `analysis/algorithms/validation/py/consensus_band.py`
- Create: `analysis/algorithms/validation/py/harmonic_reject.py`
- Create: `analysis/algorithms/validation/py/confidence_gate.py`
- Create: `analysis/algorithms/validation/py/fallback_selector.py`

- [ ] **Step 1: Write tests**

Append to `analysis/tests/analysis_all_blocks_test.py`:

```python
def test_interval_gate_accepts():
    from analysis.algorithms.validation.py.interval_gate import BLOCK
    pkt = Packet(kind="candidate", data={"intervals": [1.0]})
    result = BLOCK.run({"source": [pkt]}, {"min_s": 0.5, "max_s": 3.0}, {})
    assert len(result.outputs["accepted"]) == 1


def test_interval_gate_rejects():
    from analysis.algorithms.validation.py.interval_gate import BLOCK
    pkt = Packet(kind="candidate", data={"intervals": [0.1]})
    result = BLOCK.run({"source": [pkt]}, {"min_s": 0.5, "max_s": 3.0}, {})
    assert len(result.outputs["rejected"]) == 1


def test_consensus_band():
    from analysis.algorithms.validation.py.consensus_band import BLOCK
    pkts = [
        Packet(kind="candidate", data={"spm": 60.0}),
        Packet(kind="candidate", data={"spm": 62.0}),
        Packet(kind="candidate", data={"spm": 120.0}),
    ]
    result = BLOCK.run({"source": pkts}, {"tolerance_spm": 5.0}, {})
    assert len(result.outputs["accepted"]) == 2
    assert len(result.outputs["rejected"]) == 1


def test_harmonic_reject_rejects_double():
    from analysis.algorithms.validation.py.harmonic_reject import BLOCK
    pkt = Packet(kind="candidate", data={"spm": 120.0})
    result = BLOCK.run({"source": [pkt]}, {"fundamental_spm": 60.0, "tolerance_spm": 5.0}, {})
    assert len(result.outputs["rejected"]) == 1


def test_harmonic_reject_accepts_fundamental():
    from analysis.algorithms.validation.py.harmonic_reject import BLOCK
    pkt = Packet(kind="candidate", data={"spm": 60.0})
    result = BLOCK.run({"source": [pkt]}, {"fundamental_spm": 60.0, "tolerance_spm": 5.0}, {})
    assert len(result.outputs["accepted"]) == 1


def test_confidence_gate():
    from analysis.algorithms.validation.py.confidence_gate import BLOCK
    pkt_good = Packet(kind="candidate", data={"spm": 60.0}, confidence=0.8)
    pkt_bad = Packet(kind="candidate", data={"spm": 60.0}, confidence=0.2)
    r1 = BLOCK.run({"source": [pkt_good]}, {"min_confidence": 0.5}, {})
    r2 = BLOCK.run({"source": [pkt_bad]}, {"min_confidence": 0.5}, {})
    assert len(r1.outputs["accepted"]) == 1
    assert len(r2.outputs["rejected"]) == 1


def test_fallback_selector():
    from analysis.algorithms.validation.py.fallback_selector import BLOCK
    pkts = [
        Packet(kind="candidate", data={"spm": 60.0}, confidence=0.5),
        Packet(kind="candidate", data={"spm": 65.0}, confidence=0.9),
        Packet(kind="candidate", data={"spm": 70.0}, confidence=0.3),
    ]
    result = BLOCK.run({"source": pkts}, {}, {})
    assert result.outputs["selected"][0].data["spm"] == 65.0
```

- [ ] **Step 2: Run to verify failures**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py -k "interval_gate or consensus or harmonic or confidence_gate or fallback" -v`
Expected: all FAIL

- [ ] **Step 3: Implement interval_gate.py**

Check `mean(data["intervals"])` against `[min_s, max_s]`. Route to accepted/rejected.

Params: `min_s` (0.5), `max_s` (3.0).

- [ ] **Step 4: Implement consensus_band.py**

Compute median SPM from all input packets. For each packet, if `abs(spm - median) <= tolerance_spm`, route to accepted, else rejected.

Params: `tolerance_spm` (5.0).

- [ ] **Step 5: Implement harmonic_reject.py**

Check if `spm` is near `2 * fundamental_spm` or `0.5 * fundamental_spm` (within `tolerance_spm`). If so, reject. Otherwise accept.

Params: `fundamental_spm` (required), `tolerance_spm` (5.0).

- [ ] **Step 6: Implement confidence_gate.py**

Check `packet.confidence >= min_confidence`. Route to accepted/rejected.

Params: `min_confidence` (0.5).

- [ ] **Step 7: Implement fallback_selector.py**

Pick packet with highest `confidence` from input list. Output port: `selected`.

No params.

- [ ] **Step 8: Run tests**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py -k "interval_gate or consensus or harmonic or confidence_gate or fallback" -v`
Expected: all PASS

- [ ] **Step 9: Commit**

```bash
git add analysis/algorithms/validation/py/interval_gate.py analysis/algorithms/validation/py/consensus_band.py analysis/algorithms/validation/py/harmonic_reject.py analysis/algorithms/validation/py/confidence_gate.py analysis/algorithms/validation/py/fallback_selector.py analysis/tests/analysis_all_blocks_test.py
git commit -m "feat: add validation blocks — interval_gate, consensus_band, harmonic_reject, confidence_gate, fallback_selector"
```

---

## Task 9: Suivi Blocks

**Files:**
- Replace: `analysis/algorithms/suivi/py/kalman_2d.py`
- Create: `analysis/algorithms/suivi/py/confirmation_filter.py`
- Create: `analysis/algorithms/suivi/py/invalid_streak_reset.py`

- [ ] **Step 1: Write tests**

Append to `analysis/tests/analysis_all_blocks_test.py`:

```python
def test_kalman_2d_smooths():
    from analysis.algorithms.suivi.py.kalman_2d import BLOCK
    state = {}
    results = []
    for spm in [60.0, 62.0, 58.0, 61.0]:
        pkt = Packet(kind="candidate", data={"spm": spm})
        r = BLOCK.run({"source": [pkt]}, {}, state)
        state = r.state
        results.append(r.outputs["primary"][0].data["spm"])
    assert all(isinstance(v, float) for v in results)
    assert results[-1] != 61.0  # should be smoothed, not raw


def test_confirmation_filter_requires_streak():
    from analysis.algorithms.suivi.py.confirmation_filter import BLOCK
    state = {}
    pkt = Packet(kind="estimate", data={"spm": 60.0})
    r1 = BLOCK.run({"source": [pkt]}, {"required_streak": 3}, state)
    assert r1.outputs["primary"] == []
    state = r1.state
    r2 = BLOCK.run({"source": [pkt]}, {"required_streak": 3}, state)
    assert r2.outputs["primary"] == []
    state = r2.state
    r3 = BLOCK.run({"source": [pkt]}, {"required_streak": 3}, state)
    assert len(r3.outputs["primary"]) == 1


def test_invalid_streak_reset():
    from analysis.algorithms.suivi.py.invalid_streak_reset import BLOCK
    state = {}
    empty = Packet(kind="estimate", data={"spm": 0.0})
    for _ in range(4):
        r = BLOCK.run({"source": [empty]}, {"max_invalid": 5}, state)
        state = r.state
    assert state.get("invalid_count", 0) == 4
    assert state.get("reset", False) is False
    r = BLOCK.run({"source": [empty]}, {"max_invalid": 5}, state)
    assert r.state.get("reset", False) is True
```

- [ ] **Step 2: Run to verify failures**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py -k "kalman or confirmation or invalid_streak" -v`
Expected: all FAIL

- [ ] **Step 3: Implement kalman_2d.py**

```python
from __future__ import annotations
import numpy as np
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class Kalman2dBlock:
    manifest = BlockManifest(
        block_id="suivi.kalman_2d",
        group="suivi",
        language="py",
        entrypoint="analysis.algorithms.suivi.py.kalman_2d:BLOCK",
        input_kinds=["candidate"],
        output_ports={"primary": "estimate"},
        stateful=True,
        params_schema={
            "process_noise": {"type": "float", "default": 1.0, "min": 0.001, "description": "Process noise"},
            "measurement_noise": {"type": "float", "default": 10.0, "min": 0.001, "description": "Measurement noise"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        measurement = float(source.data.get("spm", 0.0))
        q = float(params.get("process_noise", 1.0))
        r = float(params.get("measurement_noise", 10.0))
        x = np.array(state.get("x", [measurement, 0.0]))
        P = np.array(state.get("P", [[1000.0, 0.0], [0.0, 1000.0]]))
        F = np.array([[1.0, 1.0], [0.0, 1.0]])
        H = np.array([[1.0, 0.0]])
        Q = np.array([[q, 0.0], [0.0, q]])
        R = np.array([[r]])
        # Predict
        x = F @ x
        P = F @ P @ F.T + Q
        # Update
        y = measurement - (H @ x)[0]
        S = (H @ P @ H.T + R)[0, 0]
        K = (P @ H.T) / S
        x = x + (K @ np.array([[y]])).flatten()
        P = P - K @ H @ P
        new_state = {"x": x.tolist(), "P": P.tolist()}
        return BlockResult(
            outputs={"primary": [Packet(kind="estimate", data={"spm": float(x[0])}, sample_rate_hz=source.sample_rate_hz)]},
            state=new_state,
        )

BLOCK = Kalman2dBlock()
```

- [ ] **Step 4: Implement confirmation_filter.py**

```python
from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class ConfirmationFilterBlock:
    manifest = BlockManifest(
        block_id="suivi.confirmation_filter",
        group="suivi",
        language="py",
        entrypoint="analysis.algorithms.suivi.py.confirmation_filter:BLOCK",
        input_kinds=["estimate"],
        output_ports={"primary": "estimate"},
        stateful=True,
        params_schema={
            "required_streak": {"type": "int", "default": 3, "min": 1, "description": "Consecutive valid estimates before emitting"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        required = int(params.get("required_streak", 3))
        streak = state.get("streak", 0) + 1
        new_state = {"streak": streak}
        if streak >= required:
            return BlockResult(outputs={"primary": [source]}, state=new_state)
        return BlockResult(outputs={"primary": []}, state=new_state)

BLOCK = ConfirmationFilterBlock()
```

- [ ] **Step 5: Implement invalid_streak_reset.py**

```python
from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class InvalidStreakResetBlock:
    manifest = BlockManifest(
        block_id="suivi.invalid_streak_reset",
        group="suivi",
        language="py",
        entrypoint="analysis.algorithms.suivi.py.invalid_streak_reset:BLOCK",
        input_kinds=["estimate"],
        output_ports={"primary": "estimate"},
        stateful=True,
        params_schema={
            "max_invalid": {"type": "int", "default": 5, "min": 1, "description": "Max consecutive invalids before reset"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        max_inv = int(params.get("max_invalid", 5))
        spm = float(source.data.get("spm", 0.0))
        invalid_count = state.get("invalid_count", 0)
        if spm == 0.0:
            invalid_count += 1
        else:
            invalid_count = 0
        reset = invalid_count >= max_inv
        new_state = {"invalid_count": invalid_count, "reset": reset}
        if reset:
            new_state["invalid_count"] = 0
            return BlockResult(outputs={"primary": []}, state=new_state)
        return BlockResult(outputs={"primary": [source]}, state=new_state)

BLOCK = InvalidStreakResetBlock()
```

- [ ] **Step 6: Run tests**

Run: `python -m pytest analysis/tests/analysis_all_blocks_test.py -k "kalman or confirmation or invalid_streak" -v`
Expected: all PASS

- [ ] **Step 7: Commit**

```bash
git add analysis/algorithms/suivi/py/kalman_2d.py analysis/algorithms/suivi/py/confirmation_filter.py analysis/algorithms/suivi/py/invalid_streak_reset.py analysis/tests/analysis_all_blocks_test.py
git commit -m "feat: add suivi blocks — kalman_2d, confirmation_filter, invalid_streak_reset"
```

---

## Task 10: Full Suite Smoke Test

- [ ] **Step 1: Run entire test suite**

Run: `python -m pytest analysis/tests/ -v`
Expected: all PASS

- [ ] **Step 2: Verify harness discovers all 33 blocks**

The `test_catalog_contains_all_python_block_groups` test should still pass since all 6 groups are populated.

- [ ] **Step 3: Fix any failing tests**

Address any import issues or test regressions.

- [ ] **Step 4: Commit if any fixes were needed**

```bash
git add -u
git commit -m "fix: resolve test suite regressions"
```

---

## Self-Review

### Spec Coverage

| Spec Requirement | Task |
|-----------------|------|
| Executor timing instrumentation | Task 1 |
| Executor state propagation | Task 1 |
| params_schema on existing blocks | Task 2 |
| highpass → hpf_gravity rename | Task 3 |
| 4 representation blocks + select_axis update | Task 2, 4 |
| 5 pretraitement blocks (new) | Task 5 |
| 7 estimation blocks (new) + autocorrelation update | Task 2, 6 |
| 5 detection blocks (4 new/replaced + 1 new) | Task 7 |
| 5 validation blocks (new) + spm_range_gate update | Task 2, 8 |
| 3 suivi blocks (1 replaced + 2 new) | Task 9 |
| Breaking return type change documented | Task 1 step 6 |
| Candidate data field variants (spm, intervals, crossings) | Tasks 6, 7 |
| Multi-input blocks (consensus_band, fallback_selector) | Task 8 |

### Placeholder Scan

No `TODO`, `TBD`, or "implement later" remain. Every task names exact files. Every verification step has an exact command and expected result.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-07-analysis-block-platform.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**
