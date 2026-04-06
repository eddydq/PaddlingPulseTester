# Tests Stroke Rate Workflow Design

**Goal**

Reorganize the host-side stroke-rate workflow into a self-contained `tests/...` layout that copies the existing Python and C tools, keeps the old locations untouched for now, and makes one batch script responsible for calculating stroke-rate CSVs and plotting PNG results.

**Scope**

- Copy the relevant host-side workflow into `tests/...`; do not delete or move the existing `config/...` or `scripts/...` files yet.
- Keep only two top-level Python workflow scripts under `tests/scripts`: `polar_logger.py` and `calculate_stroke_rate.py`.
- Store raw logger output in `tests/logs/raw_logs`.
- Store calculated stroke-rate CSV files in `tests/logs/stroke_rate_logs`.
- Store plot outputs only in `tests/results/png`.
- Split the current `config/script/stroke_rate_reference.py` logic into multiple algorithm files under `tests/algorithms`.
- Copy the host-side C firmware-exact estimator into `tests/algorithms/c_stroke_rate`.
- Ignore `config/script/polar_plotter.py` and `config/script/requirements.txt` in this change.

**Directory Layout**

```text
tests/
|-- scripts/
|   |-- polar_logger.py
|   `-- calculate_stroke_rate.py
|-- logs/
|   |-- raw_logs/
|   `-- stroke_rate_logs/
|-- results/
|   `-- png/
`-- algorithms/
    |-- python_stroke_rate/
    |   |-- autocorrelation_x.py
    |   |-- autocorrelation_y.py
    |   |-- autocorrelation_z.py
    |   `-- autocorrelation_magnitude.py
    `-- c_stroke_rate/
        `-- stroke_rate_firmware_exact.c
```

The current on-disk `tests/` directory is empty, so this layout can be introduced cleanly as the new default host-side workflow root.

**Architecture**

1. `tests/scripts/polar_logger.py`
   A copy of the existing Polar logger workflow with its default output path changed to `tests/logs/raw_logs`. Its BLE parsing and CSV schema stay aligned with the current logger so the downstream batch script can consume the same snapshot format.

2. `tests/scripts/calculate_stroke_rate.py`
   The new workflow entrypoint. It scans `tests/logs/raw_logs` for logger CSV files, loads the available algorithms from `tests/algorithms`, computes stroke-rate values row by row, writes one stroke-rate CSV per raw log into `tests/logs/stroke_rate_logs`, and then renders one PNG per generated stroke-rate CSV into `tests/results/png`.

3. `tests/algorithms/python_stroke_rate/*.py`
   Each Python estimator lives in its own file. The initial split mirrors the existing reference logic with separate modules for `autocorrelation_x`, `autocorrelation_y`, `autocorrelation_z`, and `autocorrelation_magnitude`. Each module exposes one stable algorithm name and one calculation entrypoint so `calculate_stroke_rate.py` can discover and run them consistently.

4. `tests/algorithms/c_stroke_rate/stroke_rate_firmware_exact.c`
   A copied C implementation of the firmware-exact estimator. `calculate_stroke_rate.py` builds or reuses its host executable from the `tests/algorithms/c_stroke_rate` location, starts it as a subprocess, streams one full sample window per input row over `stdin`, reads one returned stroke-rate value per row from `stdout`, and records that value as another algorithm column in the generated CSVs.

**Data Flow**

- `polar_logger.py` writes raw snapshot CSV files to `tests/logs/raw_logs`.
- `calculate_stroke_rate.py` scans `tests/logs/raw_logs` for input CSV files.
- For each raw log:
  - Parse each row using the existing snapshot schema (`timestamp`, `count`, and axis sample columns).
  - Skip rows that are not full `512`-sample windows, matching the current reference workflow.
  - Run every enabled algorithm under `tests/algorithms`.
  - Build one output row containing `timestamp`, `row_index`, `count`, and one column per algorithm.
  - Append that row to `tests/logs/stroke_rate_logs/<raw-log-stem>.csv`.
- For the firmware-exact C estimator:
  - Check whether a host executable already exists beside the copied C source.
  - Rebuild it when the executable is missing or older than the source file.
  - Launch one subprocess per raw log so state can persist across successive rows from the same file.
  - Send one ordered sample window to the subprocess over `stdin` and read one stroke-rate result back from `stdout`.
- After the stroke-rate CSV is complete for that raw log:
  - Parse the generated stroke-rate CSV.
  - Plot every algorithm column against `timestamp`.
  - Write `tests/results/png/<raw-log-stem>.png`.

**Output Shape**

Each raw log produces exactly one calculated CSV and one PNG.

For example, `tests/logs/raw_logs/polar_log_001.csv` produces:

- `tests/logs/stroke_rate_logs/polar_log_001.csv`
- `tests/results/png/polar_log_001.png`

The stroke-rate CSV schema is:

- `timestamp`
- `row_index`
- `count`
- one column per discovered algorithm, such as `autocorrelation_x`, `autocorrelation_y`, `autocorrelation_z`, `autocorrelation_magnitude`, and `firmware_exact_z`

**Algorithm Discovery**

- `calculate_stroke_rate.py` owns the batch orchestration and the output file format.
- Python algorithms are discovered from `tests/algorithms/python_stroke_rate`.
- The firmware-exact C estimator is treated as another algorithm source and contributes one column to the same per-log CSV.
- Algorithm names must be stable and explicit so the generated CSV headers and plot legends remain predictable across runs.

**Plotting Behavior**

- Plot from the generated CSV in `tests/logs/stroke_rate_logs`, not directly from the raw logger CSV.
- Generate one combined PNG per calculated CSV, not one PNG per algorithm.
- Use `timestamp` for the x-axis.
- Treat `row_index` and `count` as metadata, not plotted series.
- Plot every algorithm column as a separate line with the column name used as the legend label.
- If a generated CSV has no algorithm columns, skip the PNG rather than creating an empty chart.

**Error Handling**

- If no raw logs are present, print a clear message and exit successfully.
- If an input row is malformed or incomplete, skip that row or raise a clear parsing error depending on whether the problem is a normal partial window or a broken CSV structure.
- If the firmware-exact C executable is missing and cannot be built, raise a clear runtime error instead of emitting incomplete output silently.
- If the C subprocess returns malformed output or exits unexpectedly, stop processing that log and raise a clear runtime error.
- Each raw log is processed independently so one bad file does not corrupt the outputs of other logs.

**Testing**

- Add tests for the new `tests/scripts/calculate_stroke_rate.py` batch flow.
- Verify that one stroke-rate CSV is created per raw log and that the CSV contains metadata plus one column per algorithm.
- Verify that one PNG is created per calculated CSV and that metadata columns are excluded from plotting.
- Verify that the copied logger writes to `tests/logs/raw_logs`.
- Verify that the C firmware-exact estimator is executed through the new `tests/algorithms/c_stroke_rate` location and contributes a column to the generated CSV.

**Non-Goals**

- Deleting or renaming the original `config/...` or `scripts/...` workflow yet.
- Reworking the raw logger CSV schema.
- Adding `polar_plotter.py` or `requirements.txt` to the new `tests/...` workflow in this change.
- Producing additional CSV artifacts under `tests/results`.
