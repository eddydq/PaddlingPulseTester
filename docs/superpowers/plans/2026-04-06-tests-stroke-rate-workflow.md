# Tests Stroke Rate Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a self-contained `tests/...` stroke-rate workflow that copies the existing host-side logger and algorithm tools, writes one calculated CSV per raw log, and renders one PNG per calculated CSV.

**Architecture:** Track source files under `tests/scripts` and `tests/algorithms`, but keep generated artifacts ignored under `tests/logs` and `tests/results/png`. `tests/scripts/calculate_stroke_rate.py` owns the batch pipeline, dynamically runs the Python algorithms, manages the firmware-exact C subprocess, writes one output CSV per raw log, and then plots that CSV into one PNG.

**Tech Stack:** Python 3 standard library, `matplotlib`, host `gcc`, portable C99, git ignore rules

---

### File Structure

- Modify: `.gitignore`
- Create: `tests/calculate_stroke_rate_test.py`
- Create: `tests/scripts/calculate_stroke_rate.py`
- Create: `tests/scripts/polar_logger.py`
- Create: `tests/algorithms/python_stroke_rate/common.py`
- Create: `tests/algorithms/python_stroke_rate/autocorrelation_x.py`
- Create: `tests/algorithms/python_stroke_rate/autocorrelation_y.py`
- Create: `tests/algorithms/python_stroke_rate/autocorrelation_z.py`
- Create: `tests/algorithms/python_stroke_rate/autocorrelation_magnitude.py`
- Create: `tests/algorithms/c_stroke_rate/stroke_rate_firmware_exact.c`
- Create: `tests/algorithms/c_stroke_rate/.gitignore`
- Create: `tests/logs/raw_logs/.gitkeep`
- Create: `tests/logs/stroke_rate_logs/.gitkeep`
- Create: `tests/results/png/.gitkeep`

### Task 1: Track the New `tests/...` Workflow and Lock the Batch Contract

**Files:**
- Modify: `.gitignore`
- Create: `tests/calculate_stroke_rate_test.py`
- Create: `tests/logs/raw_logs/.gitkeep`
- Create: `tests/logs/stroke_rate_logs/.gitkeep`
- Create: `tests/results/png/.gitkeep`
- Create: `tests/algorithms/c_stroke_rate/.gitignore`

- [ ] **Step 1: Write the failing batch-workflow tests**

```python
import csv
import importlib.util
import tempfile
import unittest
from pathlib import Path


def _load_calculator_module():
    return _load_module(Path("tests/scripts/calculate_stroke_rate.py"), "calculate_stroke_rate")


def _load_module(module_path: Path, module_name: str):
    spec = importlib.util.spec_from_file_location("calculate_stroke_rate", module_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class CalculateStrokeRateWorkflowTest(unittest.TestCase):
    def test_process_all_logs_writes_one_csv_and_one_png_per_raw_log(self):
        module = _load_calculator_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            raw_logs_dir = root / "logs" / "raw_logs"
            stroke_rate_logs_dir = root / "logs" / "stroke_rate_logs"
            png_dir = root / "results" / "png"
            raw_logs_dir.mkdir(parents=True)

            with (raw_logs_dir / "polar_log_001.csv").open("w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                writer.writerow(["timestamp", "count", "x_000", "y_000", "z_000"])
                writer.writerow(["2026-04-06T12:00:00", "1", "1", "2", "3"])

            generated = module.process_all_logs(
                raw_logs_dir=raw_logs_dir,
                stroke_rate_logs_dir=stroke_rate_logs_dir,
                png_dir=png_dir,
            )

            self.assertIn("polar_log_001", generated)
            self.assertTrue((stroke_rate_logs_dir / "polar_log_001.csv").exists())
            self.assertTrue((png_dir / "polar_log_001.png").exists())
```

- [ ] **Step 2: Run the test to verify the current repo fails for the right reason**

Run: `python -m unittest tests.calculate_stroke_rate_test -v`

Expected: FAIL with `FileNotFoundError` or import failure because `tests/scripts/calculate_stroke_rate.py` does not exist yet.

- [ ] **Step 3: Update ignore rules so source files under `tests/...` are tracked but generated outputs stay ignored**

```gitignore
build/
config/local.mk
config/local/
.claude/
.worktrees/
__pycache__/
/tests/logs/raw_logs/*
!/tests/logs/raw_logs/.gitkeep
/tests/logs/stroke_rate_logs/*
!/tests/logs/stroke_rate_logs/.gitkeep
/tests/results/png/*
!/tests/results/png/.gitkeep
/tests/algorithms/c_stroke_rate/*.exe
```

- [ ] **Step 4: Add the kept-empty directories and C build ignore file**

```gitignore
*.exe
```

- [ ] **Step 5: Re-run the test and confirm it still fails only because the implementation does not exist yet**

Run: `python -m unittest tests.calculate_stroke_rate_test -v`

Expected: FAIL with missing-module or missing-function errors, not because gitignored files are absent from the worktree.

- [ ] **Step 6: Commit the test-and-layout scaffolding**

```bash
git add .gitignore tests/calculate_stroke_rate_test.py tests/logs/raw_logs/.gitkeep tests/logs/stroke_rate_logs/.gitkeep tests/results/png/.gitkeep tests/algorithms/c_stroke_rate/.gitignore
git commit -m "test: define tests stroke-rate workflow contract"
```

### Task 2: Implement the Python Batch Pipeline and Per-Algorithm Modules

**Files:**
- Modify: `tests/calculate_stroke_rate_test.py`
- Create: `tests/scripts/calculate_stroke_rate.py`
- Create: `tests/algorithms/python_stroke_rate/common.py`
- Create: `tests/algorithms/python_stroke_rate/autocorrelation_x.py`
- Create: `tests/algorithms/python_stroke_rate/autocorrelation_y.py`
- Create: `tests/algorithms/python_stroke_rate/autocorrelation_z.py`
- Create: `tests/algorithms/python_stroke_rate/autocorrelation_magnitude.py`

- [ ] **Step 1: Extend the tests to lock the output CSV schema and algorithm discovery behavior**

```python
    def test_generated_csv_contains_metadata_and_algorithm_columns(self):
        module = _load_calculator_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            raw_logs_dir = root / "logs" / "raw_logs"
            stroke_rate_logs_dir = root / "logs" / "stroke_rate_logs"
            png_dir = root / "results" / "png"
            raw_logs_dir.mkdir(parents=True)

            with (raw_logs_dir / "polar_log_001.csv").open("w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                header = ["timestamp", "count"]
                header.extend(f"{axis}_{index:03d}" for axis in ("x", "y", "z") for index in range(512))
                writer.writerow(header)
                row = ["2026-04-06T12:00:00", "512"]
                row.extend("1" for _index in range(1536))
                writer.writerow(row)

            module.process_all_logs(
                raw_logs_dir=raw_logs_dir,
                stroke_rate_logs_dir=stroke_rate_logs_dir,
                png_dir=png_dir,
            )

            with (stroke_rate_logs_dir / "polar_log_001.csv").open("r", newline="", encoding="utf-8") as handle:
                reader = csv.DictReader(handle)
                self.assertEqual(
                    reader.fieldnames[:3],
                    ["timestamp", "row_index", "count"],
                )
                self.assertIn("autocorrelation_x", reader.fieldnames)
                self.assertIn("autocorrelation_y", reader.fieldnames)
                self.assertIn("autocorrelation_z", reader.fieldnames)
                self.assertIn("autocorrelation_magnitude", reader.fieldnames)
```

- [ ] **Step 2: Run the tests to verify the richer contract fails**

Run: `python -m unittest tests.calculate_stroke_rate_test -v`

Expected: FAIL because `process_all_logs` and the algorithm modules do not exist yet.

- [ ] **Step 3: Implement shared autocorrelation helpers**

```python
SAMPLE_STORE_CAPACITY = 512
SAMPLE_RATE_HZ = 52.0
MIN_STROKE_RATE_SPM = 20.0
MAX_STROKE_RATE_SPM = 120.0
PEAK_SCORE_FRACTION = 0.8


def estimate_autocorrelation_stroke_rate(values: list[float]) -> float:
    if len(values) < 2:
        return 0.0
    lag_min = max(1, math.ceil((SAMPLE_RATE_HZ * 60.0) / MAX_STROKE_RATE_SPM))
    lag_max = min(len(values) - 1, math.floor((SAMPLE_RATE_HZ * 60.0) / MIN_STROKE_RATE_SPM))
    if lag_min > lag_max:
        return 0.0
    scores = [(lag, _pearson_autocorrelation(values, lag)) for lag in range(lag_min, lag_max + 1)]
    best_lag = _select_peak_lag(scores)
    if best_lag == 0:
        return 0.0
    return (SAMPLE_RATE_HZ * 60.0) / best_lag
```

- [ ] **Step 4: Implement one Python file per algorithm**

```python
ALGORITHM_NAME = "autocorrelation_x"


def calculate(snapshot: dict[str, object]) -> float:
    series = snapshot["series"]
    return estimate_autocorrelation_stroke_rate(series["x"])
```

- [ ] **Step 5: Implement `tests/scripts/calculate_stroke_rate.py` with snapshot parsing, Python algorithm discovery, per-log CSV writing, and PNG rendering**

```python
def process_all_logs(raw_logs_dir: Path, stroke_rate_logs_dir: Path, png_dir: Path) -> dict[str, int]:
    generated: dict[str, int] = {}
    for csv_path in sorted(raw_logs_dir.glob("*.csv")):
        output_csv = stroke_rate_logs_dir / f"{csv_path.stem}.csv"
        row_count = process_log_file(csv_path, output_csv=output_csv)
        if row_count > 0:
            render_plot_csv(output_csv, png_dir / f"{csv_path.stem}.png")
        generated[csv_path.stem] = row_count
    return generated
```

- [ ] **Step 6: Re-run the tests and confirm the Python-only workflow passes**

Run: `python -m unittest tests.calculate_stroke_rate_test -v`

Expected: PASS for the CSV schema, algorithm discovery, and PNG generation tests.

- [ ] **Step 7: Commit the Python workflow implementation**

```bash
git add tests/calculate_stroke_rate_test.py tests/scripts/calculate_stroke_rate.py tests/algorithms/python_stroke_rate/common.py tests/algorithms/python_stroke_rate/autocorrelation_x.py tests/algorithms/python_stroke_rate/autocorrelation_y.py tests/algorithms/python_stroke_rate/autocorrelation_z.py tests/algorithms/python_stroke_rate/autocorrelation_magnitude.py
git commit -m "feat: add tests stroke-rate batch pipeline"
```

### Task 3: Copy the Logger and Add Firmware-Exact C Integration

**Files:**
- Modify: `tests/calculate_stroke_rate_test.py`
- Create: `tests/scripts/polar_logger.py`
- Create: `tests/algorithms/c_stroke_rate/stroke_rate_firmware_exact.c`
- Modify: `tests/scripts/calculate_stroke_rate.py`

- [ ] **Step 1: Extend the tests to cover the copied logger path and firmware-exact column**

```python
    def test_logger_defaults_to_tests_raw_logs(self):
        logger_module = _load_module(Path("tests/scripts/polar_logger.py"), "polar_logger")
        self.assertEqual(logger_module.LOGS_DIR, Path("tests/logs/raw_logs").resolve())

    def test_generated_csv_includes_firmware_exact_column(self):
        module = _load_calculator_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            raw_logs_dir = root / "logs" / "raw_logs"
            stroke_rate_logs_dir = root / "logs" / "stroke_rate_logs"
            png_dir = root / "results" / "png"
            raw_logs_dir.mkdir(parents=True)

            with (raw_logs_dir / "polar_log_001.csv").open("w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                header = ["timestamp", "count"]
                header.extend(f"{axis}_{index:03d}" for axis in ("x", "y", "z") for index in range(512))
                writer.writerow(header)
                row = ["2026-04-06T12:00:00", "512"]
                row.extend(str((index % 23) - 11) for index in range(1536))
                writer.writerow(row)

            module.process_all_logs(
                raw_logs_dir=raw_logs_dir,
                stroke_rate_logs_dir=stroke_rate_logs_dir,
                png_dir=png_dir,
                firmware_exact_source=Path("tests/algorithms/c_stroke_rate/stroke_rate_firmware_exact.c"),
            )

            with (stroke_rate_logs_dir / "polar_log_001.csv").open("r", newline="", encoding="utf-8") as handle:
                reader = csv.DictReader(handle)

            self.assertIn("firmware_exact_z", reader.fieldnames)
```

- [ ] **Step 2: Run the tests to verify the firmware/logger expectations fail**

Run: `python -m unittest tests.calculate_stroke_rate_test -v`

Expected: FAIL because the copied logger file and firmware-exact integration do not exist yet.

- [ ] **Step 3: Copy the existing logger into `tests/scripts/polar_logger.py` and retarget its default log directory**

```python
LOGS_DIR = Path(__file__).resolve().parent.parent / "logs" / "raw_logs"
```

- [ ] **Step 4: Copy `scripts/stroke_rate_firmware_exact.c` into `tests/algorithms/c_stroke_rate/stroke_rate_firmware_exact.c` and integrate the compile-and-stream subprocess path into `calculate_stroke_rate.py`**

```python
result = subprocess.run(
    [compiler, "-std=c99", "-O2", str(source_path), "-o", str(executable_path)],
    capture_output=True,
    text=True,
    check=False,
)

process = subprocess.Popen(
    [str(executable_path)],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    encoding="utf-8",
    bufsize=1,
)
```

- [ ] **Step 5: Re-run the tests and then run the workflow end to end on a real sample log if one is available**

Run: `python -m unittest tests.calculate_stroke_rate_test -v`
Expected: PASS

Run: `python tests/scripts/calculate_stroke_rate.py`
Expected: one CSV per raw log in `tests/logs/stroke_rate_logs` and one PNG per calculated CSV in `tests/results/png`

- [ ] **Step 6: Commit the copied logger and firmware-exact integration**

```bash
git add tests/calculate_stroke_rate_test.py tests/scripts/polar_logger.py tests/scripts/calculate_stroke_rate.py tests/algorithms/c_stroke_rate/stroke_rate_firmware_exact.c
git commit -m "feat: add tests firmware-exact stroke-rate workflow"
```
