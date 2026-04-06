from __future__ import annotations

import argparse
import csv
import importlib.util
import math
import os
import shutil
import subprocess
import sys
from contextlib import nullcontext
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

from matplotlib import dates as mdates
from matplotlib import pyplot as plt

SAMPLE_STORE_CAPACITY = 512
AXES = ("x", "y", "z")
METADATA_COLUMNS = {"timestamp", "row_index", "count"}

TESTS_ROOT = Path(__file__).resolve().parent.parent
RAW_LOGS_DIR = TESTS_ROOT / "logs" / "raw_logs"
STROKE_RATE_LOGS_DIR = TESTS_ROOT / "logs" / "stroke_rate_logs"
PNG_DIR = TESTS_ROOT / "results" / "png"
PYTHON_ALGORITHMS_DIR = TESTS_ROOT / "algorithms" / "python_stroke_rate"
C_ALGORITHMS_DIR = TESTS_ROOT / "algorithms" / "c_stroke_rate"
DEFAULT_FIRMWARE_EXACT_SOURCE = C_ALGORITHMS_DIR / "stroke_rate_firmware_exact.c"
DEFAULT_FIRMWARE_EXACT_EXECUTABLE = C_ALGORITHMS_DIR / (
    "stroke_rate_firmware_exact.exe" if os.name == "nt" else "stroke_rate_firmware_exact"
)


@dataclass(frozen=True)
class SnapshotRow:
    row_index: int
    timestamp: str
    count: int
    series: dict[str, list[float]]


@dataclass(frozen=True)
class PythonAlgorithm:
    name: str
    calculate: object


class FirmwareExactEstimator:
    def __init__(self, executable_path: Path):
        self.executable_path = Path(executable_path)
        self.process: subprocess.Popen[str] | None = None

    def __enter__(self) -> "FirmwareExactEstimator":
        self.process = subprocess.Popen(
            [str(self.executable_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        return self

    def __exit__(self, _exc_type, _exc, _tb) -> None:
        self.close()

    def estimate(self, values: list[float]) -> int:
        if self.process is None or self.process.stdin is None or self.process.stdout is None:
            raise RuntimeError("FirmwareExactEstimator is not running")
        if len(values) != SAMPLE_STORE_CAPACITY:
            raise ValueError(f"Expected {SAMPLE_STORE_CAPACITY} samples, got {len(values)}")

        line = " ".join(str(int(value)) for value in values)
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

        output = self.process.stdout.readline()
        if not output:
            stderr = ""
            if self.process.stderr is not None:
                stderr = self.process.stderr.read().strip()
            raise RuntimeError(
                "Firmware-exact estimator exited unexpectedly"
                + (f": {stderr}" if stderr else "")
            )

        try:
            return int(output.strip())
        except ValueError as exc:
            raise RuntimeError(
                f"Malformed firmware-exact estimator output: {output.strip()!r}"
            ) from exc

    def close(self) -> None:
        if self.process is None:
            return
        if self.process.stdin is not None and not self.process.stdin.closed:
            self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
        if self.process.stdout is not None and not self.process.stdout.closed:
            self.process.stdout.close()
        if self.process.stderr is not None and not self.process.stderr.closed:
            self.process.stderr.close()
        self.process = None


def _load_module(module_path: Path, module_name: str):
    spec = importlib.util.spec_from_file_location(module_name, module_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


def _parse_int(value: str | None, default: int = 0) -> int:
    if value in (None, ""):
        return default
    try:
        return int(value)
    except ValueError:
        return default


def _parse_float(value: str | None) -> float:
    if value in (None, ""):
        return math.nan
    try:
        return float(value)
    except ValueError:
        return math.nan


def parse_snapshot_row(row: dict[str, str], row_index: int) -> SnapshotRow:
    count = max(0, min(SAMPLE_STORE_CAPACITY, _parse_int(row.get("count"))))
    series: dict[str, list[float]] = {}

    for axis_name in AXES:
        axis_series = []
        for sample_index in range(count):
            axis_series.append(float(_parse_int(row.get(f"{axis_name}_{sample_index:03d}"))))
        series[axis_name] = axis_series

    return SnapshotRow(
        row_index=row_index,
        timestamp=row.get("timestamp", ""),
        count=count,
        series=series,
    )


def iter_snapshot_rows(csv_path: Path):
    with csv_path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row_index, row in enumerate(reader, start=1):
            yield parse_snapshot_row(row, row_index=row_index)


def discover_python_algorithms(
    algorithms_dir: Path = PYTHON_ALGORITHMS_DIR,
) -> list[PythonAlgorithm]:
    algorithms_dir = Path(algorithms_dir)
    if str(algorithms_dir) not in sys.path:
        sys.path.insert(0, str(algorithms_dir))

    discovered: list[PythonAlgorithm] = []
    for module_path in sorted(algorithms_dir.glob("*.py")):
        if module_path.name == "common.py" or module_path.name.startswith("_"):
            continue

        module = _load_module(module_path, f"stroke_rate_algorithm_{module_path.stem}")
        algorithm_name = getattr(module, "ALGORITHM_NAME")
        algorithm_function = getattr(module, "calculate")
        discovered.append(
            PythonAlgorithm(
                name=str(algorithm_name),
                calculate=algorithm_function,
            )
        )

    return discovered


def _output_fieldnames(algorithms: list[PythonAlgorithm]) -> list[str]:
    return ["timestamp", "row_index", "count"] + [algorithm.name for algorithm in algorithms]


def _append_firmware_fieldname(fieldnames: list[str], enabled: bool) -> list[str]:
    if enabled:
        return fieldnames + ["firmware_exact_z"]
    return fieldnames


def _format_rate(value: float) -> str:
    return f"{value:.3f}"


def _ensure_firmware_exact_executable(
    source_path: Path | None = None,
    executable_path: Path | None = None,
) -> Path:
    source_path = DEFAULT_FIRMWARE_EXACT_SOURCE if source_path is None else Path(source_path)
    executable_path = (
        DEFAULT_FIRMWARE_EXACT_EXECUTABLE if executable_path is None else Path(executable_path)
    )

    if not source_path.exists():
        raise RuntimeError(f"Missing firmware-exact source: {source_path}")

    needs_build = not executable_path.exists()
    if not needs_build:
        needs_build = executable_path.stat().st_mtime < source_path.stat().st_mtime

    if not needs_build:
        return executable_path

    compiler = shutil.which("gcc")
    if compiler is None:
        raise RuntimeError("gcc is required to build the firmware-exact C estimator")

    executable_path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [compiler, "-std=c99", "-O2", str(source_path), "-o", str(executable_path)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        stderr = result.stderr.strip()
        raise RuntimeError(
            f"Failed to build firmware-exact estimator: {stderr or result.stdout.strip()}"
        )

    return executable_path


def process_log_file(
    csv_path: Path,
    *,
    output_csv: Path,
    python_algorithms: list[PythonAlgorithm] | None = None,
    firmware_exact_source: Path | None = None,
    firmware_exact_executable: Path | None = None,
) -> int:
    algorithms = python_algorithms or discover_python_algorithms()
    firmware_enabled = firmware_exact_source is not None or DEFAULT_FIRMWARE_EXACT_SOURCE.exists()
    resolved_firmware_executable: Path | None = None
    if firmware_enabled:
        resolved_firmware_executable = _ensure_firmware_exact_executable(
            source_path=firmware_exact_source,
            executable_path=firmware_exact_executable,
        )
    output_csv.parent.mkdir(parents=True, exist_ok=True)

    generated = 0
    with output_csv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=_append_firmware_fieldname(
                _output_fieldnames(algorithms),
                firmware_enabled,
            ),
        )
        writer.writeheader()

        estimator_context = (
            FirmwareExactEstimator(resolved_firmware_executable)
            if resolved_firmware_executable is not None
            else None
        )

        if estimator_context is None:
            estimator_cm = nullcontext()
        else:
            estimator_cm = estimator_context

        with estimator_cm as firmware_estimator:
            for snapshot in iter_snapshot_rows(Path(csv_path)):
                if snapshot.count != SAMPLE_STORE_CAPACITY:
                    continue

                row = {
                    "timestamp": snapshot.timestamp,
                    "row_index": str(snapshot.row_index),
                    "count": str(snapshot.count),
                }
                for algorithm in algorithms:
                    row[algorithm.name] = _format_rate(
                        algorithm.calculate({"series": snapshot.series})
                    )
                if firmware_estimator is not None:
                    row["firmware_exact_z"] = str(
                        firmware_estimator.estimate(snapshot.series["z"])
                    )
                writer.writerow(row)
                generated += 1

    return generated


def parse_plot_csv(csv_path: Path):
    with csv_path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        fieldnames = reader.fieldnames or []
        series_columns = [
            fieldname
            for fieldname in fieldnames
            if fieldname not in METADATA_COLUMNS
        ]

        timestamps: list[datetime] = []
        series = {column_name: [] for column_name in series_columns}

        for row_index, row in enumerate(reader, start=1):
            timestamp_text = row.get("timestamp", "")
            try:
                timestamps.append(datetime.fromisoformat(timestamp_text))
            except ValueError as exc:
                raise ValueError(
                    f"Invalid timestamp '{timestamp_text}' in {csv_path} row {row_index}"
                ) from exc

            for column_name in series_columns:
                series[column_name].append(_parse_float(row.get(column_name)))

    return timestamps, series


def render_plot_csv(csv_path: Path, output_png: Path) -> int:
    timestamps, series = parse_plot_csv(csv_path)
    if not series:
        return 0

    output_png.parent.mkdir(parents=True, exist_ok=True)
    figure, axis = plt.subplots(figsize=(12, 6), constrained_layout=True)

    for column_name, values in series.items():
        axis.plot(timestamps, values, label=column_name, linewidth=1.8)

    axis.set_title(Path(csv_path).stem)
    axis.set_xlabel("Timestamp")
    axis.set_ylabel("Estimated Stroke Rate")
    axis.grid(True, alpha=0.3)
    axis.legend()
    axis.xaxis.set_major_locator(mdates.AutoDateLocator())
    axis.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))
    figure.autofmt_xdate()
    figure.savefig(output_png, dpi=150)
    plt.close(figure)
    return 1


def process_all_logs(
    *,
    raw_logs_dir: Path = RAW_LOGS_DIR,
    stroke_rate_logs_dir: Path = STROKE_RATE_LOGS_DIR,
    png_dir: Path = PNG_DIR,
    firmware_exact_source: Path | None = DEFAULT_FIRMWARE_EXACT_SOURCE,
    firmware_exact_executable: Path | None = None,
) -> dict[str, int]:
    raw_logs_dir = Path(raw_logs_dir)
    stroke_rate_logs_dir = Path(stroke_rate_logs_dir)
    png_dir = Path(png_dir)
    algorithms = discover_python_algorithms()

    generated: dict[str, int] = {}
    for csv_path in sorted(path for path in raw_logs_dir.glob("*.csv") if path.is_file()):
        output_csv = stroke_rate_logs_dir / f"{csv_path.stem}.csv"
        row_count = process_log_file(
            csv_path,
            output_csv=output_csv,
            python_algorithms=algorithms,
            firmware_exact_source=firmware_exact_source,
            firmware_exact_executable=firmware_exact_executable,
        )
        if row_count > 0:
            render_plot_csv(output_csv, png_dir / f"{csv_path.stem}.png")
        generated[csv_path.stem] = row_count

    return generated


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Calculate stroke-rate CSVs from raw logs and render PNG plots."
    )
    parser.add_argument("--raw-logs-dir", type=Path, default=RAW_LOGS_DIR)
    parser.add_argument("--stroke-rate-logs-dir", type=Path, default=STROKE_RATE_LOGS_DIR)
    parser.add_argument("--png-dir", type=Path, default=PNG_DIR)
    parser.add_argument("--firmware-exact-source", type=Path, default=DEFAULT_FIRMWARE_EXACT_SOURCE)
    parser.add_argument("--firmware-exact-executable", type=Path, default=None)
    args = parser.parse_args()

    generated = process_all_logs(
        raw_logs_dir=args.raw_logs_dir,
        stroke_rate_logs_dir=args.stroke_rate_logs_dir,
        png_dir=args.png_dir,
        firmware_exact_source=args.firmware_exact_source,
        firmware_exact_executable=args.firmware_exact_executable,
    )
    if not generated:
        print(f"No raw CSV logs found in {args.raw_logs_dir}")
        return 0

    for log_name, row_count in generated.items():
        print(
            f"{log_name}: wrote {row_count} row(s) to "
            f"{args.stroke_rate_logs_dir / (log_name + '.csv')}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
