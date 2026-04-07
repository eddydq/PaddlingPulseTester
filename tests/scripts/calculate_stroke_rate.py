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
REFERENCE_ALGORITHM = "consensus_music_magnitude"

PLOT_GROUPS = [
    {
        "title": "Lightweight",
        "algorithms": [
            "peak_adaptive_magnitude", "peak_adaptive_y",
            "zero_crossing_magnitude", "zero_crossing_y",
            "peak_hysteresis_magnitude", "peak_hysteresis_y",
        ],
    },
    {
        "title": "FFT + Spectral",
        "algorithms": [
            "fft_dominant_magnitude", "fft_dominant_y",
        ],
    },
    {
        "title": "Autocorrelation Variants",
        "algorithms": [
            "autocorrelation_hpf_magnitude", "autocorrelation_hpf_y",
            "autocorrelation_bandpass_magnitude", "autocorrelation_bandpass_y",
            "autocorrelation_magnitude", "autocorrelation_x",
            "autocorrelation_y", "autocorrelation_z",
        ],
    },
    {
        "title": "Heavy + Smoothed",
        "algorithms": [
            "hilbert_instantaneous_magnitude", "wavelet_dwt_magnitude",
            "consensus_music_magnitude",
            "kalman2d_peak_hysteresis_magnitude", "kalman2d_fft_dominant_magnitude",
            "firmware_exact_z",
        ],
    },
]

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


@dataclass(frozen=True)
class CAlgorithm:
    name: str
    executable_path: Path
    protocol: str


def _detect_c_protocol(source_path: Path) -> str:
    try:
        text = source_path.read_text(encoding="utf-8")
    except OSError:
        return "single_axis"
    if "PROTOCOL PROTOCOL_MAGNITUDE" in text:
        return "magnitude"
    return "single_axis"


def _needs_filters_link(source_path: Path) -> bool:
    try:
        text = source_path.read_text(encoding="utf-8")
    except OSError:
        return False
    return '#include "filters.h"' in text


def discover_c_algorithms(
    c_dir: Path = C_ALGORITHMS_DIR,
) -> list[CAlgorithm]:
    c_dir = Path(c_dir)
    compiler = shutil.which("gcc")
    if compiler is None:
        return []

    filters_source = c_dir / "filters.c"
    discovered: list[CAlgorithm] = []

    for source_path in sorted(c_dir.glob("*.c")):
        if source_path.name.startswith("_"):
            continue
        if source_path.name == "filters.c":
            continue
        if source_path.name == "stroke_rate_firmware_exact.c":
            continue

        ext = ".exe" if os.name == "nt" else ""
        executable_path = c_dir / (source_path.stem + ext)

        needs_build = not executable_path.exists()
        if not needs_build:
            needs_build = executable_path.stat().st_mtime < source_path.stat().st_mtime
            if not needs_build and filters_source.exists():
                needs_build = executable_path.stat().st_mtime < filters_source.stat().st_mtime

        if needs_build:
            cmd = [compiler, "-std=c99", "-O2", str(source_path)]
            if _needs_filters_link(source_path) and filters_source.exists():
                cmd.append(str(filters_source))
            cmd.extend(["-o", str(executable_path), "-lm"])
            result = subprocess.run(cmd, capture_output=True, text=True, check=False)
            if result.returncode != 0:
                print(
                    f"Warning: failed to build {source_path.name}: "
                    f"{result.stderr.strip() or result.stdout.strip()}",
                    file=sys.stderr,
                )
                continue

        protocol = _detect_c_protocol(source_path)
        discovered.append(
            CAlgorithm(
                name=f"c_{source_path.stem}",
                executable_path=executable_path,
                protocol=protocol,
            )
        )

    return discovered


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


def _output_fieldnames(
    algorithms: list[PythonAlgorithm],
    c_algorithms: list[CAlgorithm] | None = None,
    firmware_enabled: bool = False,
) -> list[str]:
    names = ["timestamp", "row_index", "count"]
    names += [algorithm.name for algorithm in algorithms]
    if firmware_enabled:
        names.append("firmware_exact_z")
    if c_algorithms:
        names += [c_alg.name for c_alg in c_algorithms]
    return names


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


def _send_to_c_estimator(
    estimator: FirmwareExactEstimator,
    c_alg: CAlgorithm,
    snapshot: SnapshotRow,
) -> int:
    if c_alg.protocol == "magnitude":
        values = (
            [int(v) for v in snapshot.series["x"]]
            + [int(v) for v in snapshot.series["y"]]
            + [int(v) for v in snapshot.series["z"]]
        )
        line = " ".join(str(v) for v in values)
        estimator.process.stdin.write(line + "\n")
        estimator.process.stdin.flush()
        output = estimator.process.stdout.readline()
        if not output:
            return 0
        return int(output.strip())
    else:
        return estimator.estimate(snapshot.series["y"])


def process_log_file(
    csv_path: Path,
    *,
    output_csv: Path,
    python_algorithms: list[PythonAlgorithm] | None = None,
    c_algorithms: list[CAlgorithm] | None = None,
    firmware_exact_source: Path | None = None,
    firmware_exact_executable: Path | None = None,
) -> int:
    algorithms = python_algorithms or discover_python_algorithms()
    c_algs = c_algorithms if c_algorithms is not None else discover_c_algorithms()
    firmware_enabled = firmware_exact_source is not None or DEFAULT_FIRMWARE_EXACT_SOURCE.exists()
    resolved_firmware_executable: Path | None = None
    if firmware_enabled:
        resolved_firmware_executable = _ensure_firmware_exact_executable(
            source_path=firmware_exact_source,
            executable_path=firmware_exact_executable,
        )
    output_csv.parent.mkdir(parents=True, exist_ok=True)

    # Reset stateful algorithms (Kalman overlays)
    for algorithm in algorithms:
        module = sys.modules.get(f"stroke_rate_algorithm_{algorithm.name}")
        if module and hasattr(module, "reset"):
            module.reset()

    generated = 0
    fieldnames = _output_fieldnames(algorithms, c_algs, firmware_enabled)

    with output_csv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()

        # Open firmware-exact estimator
        firmware_estimator_ctx = (
            FirmwareExactEstimator(resolved_firmware_executable)
            if resolved_firmware_executable is not None
            else None
        )

        # Open all C algorithm estimators
        c_estimators: list[tuple[CAlgorithm, FirmwareExactEstimator]] = []
        for c_alg in c_algs:
            c_estimators.append((c_alg, FirmwareExactEstimator(c_alg.executable_path)))

        try:
            if firmware_estimator_ctx is not None:
                firmware_estimator_ctx.__enter__()
            for _c_alg, est in c_estimators:
                est.__enter__()

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
                if firmware_estimator_ctx is not None:
                    row["firmware_exact_z"] = str(
                        firmware_estimator_ctx.estimate(snapshot.series["z"])
                    )
                for c_alg, est in c_estimators:
                    try:
                        row[c_alg.name] = str(
                            _send_to_c_estimator(est, c_alg, snapshot)
                        )
                    except Exception:
                        row[c_alg.name] = "0"
                writer.writerow(row)
                generated += 1
        finally:
            if firmware_estimator_ctx is not None:
                firmware_estimator_ctx.close()
            for _c_alg, est in c_estimators:
                est.close()

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


def _compute_algorithm_stats(
    algorithm_values: list[float],
    reference_values: list[float],
) -> dict[str, str]:
    non_zero = [v for v in algorithm_values if v > 0.0]
    mean_spm = sum(non_zero) / len(non_zero) if non_zero else 0.0

    if len(non_zero) >= 2:
        variance = sum((v - mean_spm) ** 2 for v in non_zero) / (len(non_zero) - 1)
        std_spm = math.sqrt(variance)
    else:
        std_spm = 0.0

    zero_pct = (
        100.0 * sum(1 for v in algorithm_values if v == 0.0) / len(algorithm_values)
        if algorithm_values
        else 0.0
    )

    paired = [
        (a, r)
        for a, r in zip(algorithm_values, reference_values)
        if a > 0.0 and r > 0.0
    ]

    if len(paired) >= 2:
        a_vals = [p[0] for p in paired]
        r_vals = [p[1] for p in paired]
        mae = sum(abs(a - r) for a, r in paired) / len(paired)
        a_mean = sum(a_vals) / len(a_vals)
        r_mean = sum(r_vals) / len(r_vals)
        num = sum((a - a_mean) * (r - r_mean) for a, r in zip(a_vals, r_vals))
        den_a = math.sqrt(sum((a - a_mean) ** 2 for a in a_vals))
        den_r = math.sqrt(sum((r - r_mean) ** 2 for r in r_vals))
        pearson_r = num / (den_a * den_r) if den_a > 0 and den_r > 0 else 0.0
    else:
        mae = float("nan")
        pearson_r = float("nan")

    return {
        "mean": f"{mean_spm:.1f}",
        "std": f"{std_spm:.1f}",
        "zero_pct": f"{zero_pct:.1f}%",
        "mae": f"{mae:.1f}" if not math.isnan(mae) else "---",
        "r": f"{pearson_r:.2f}" if not math.isnan(pearson_r) else "---",
    }


def _build_stats_text(
    group_algorithms: list[str],
    series: dict[str, list[float]],
    reference_values: list[float],
) -> str:
    header = f"{'Algorithm':<36s} {'Mean':>5s} {'Std':>5s} {'Zero%':>6s} {'MAE':>5s} {'r':>5s}"
    lines = [header]
    for alg_name in group_algorithms:
        if alg_name not in series:
            continue
        stats = _compute_algorithm_stats(series[alg_name], reference_values)
        is_ref = alg_name == REFERENCE_ALGORITHM
        mae_str = "---" if is_ref else stats["mae"]
        r_str = "1.00" if is_ref else stats["r"]
        lines.append(
            f"{alg_name:<36s} {stats['mean']:>5s} {stats['std']:>5s} "
            f"{stats['zero_pct']:>6s} {mae_str:>5s} {r_str:>5s}"
        )
    return "\n".join(lines)


def render_plot_csv(csv_path: Path, output_png: Path) -> int:
    timestamps, series = parse_plot_csv(csv_path)
    if not series:
        return 0

    output_png.parent.mkdir(parents=True, exist_ok=True)

    reference_values = series.get(REFERENCE_ALGORITHM, [])

    num_groups = len(PLOT_GROUPS)
    figure, axes = plt.subplots(
        num_groups, 1, figsize=(14, 4 * num_groups), constrained_layout=True,
    )
    if num_groups == 1:
        axes = [axes]

    for group_idx, group in enumerate(PLOT_GROUPS):
        ax = axes[group_idx]

        # Plot reference as dashed gray in all groups except Heavy+Smoothed
        # (where it's already a member)
        if REFERENCE_ALGORITHM in series and REFERENCE_ALGORITHM not in group["algorithms"]:
            ax.plot(
                timestamps, series[REFERENCE_ALGORITHM],
                label=REFERENCE_ALGORITHM, linewidth=2.0,
                linestyle="--", color="gray", alpha=0.7,
            )

        for alg_name in group["algorithms"]:
            if alg_name not in series:
                continue
            kwargs = {}
            if alg_name == REFERENCE_ALGORITHM:
                kwargs = {"linewidth": 2.0, "linestyle": "--", "color": "gray", "alpha": 0.7}
            else:
                kwargs = {"linewidth": 1.5}
            ax.plot(timestamps, series[alg_name], label=alg_name, **kwargs)

        ax.set_title(f"{Path(csv_path).stem} — {group['title']}")
        ax.set_ylabel("SPM")
        ax.grid(True, alpha=0.3)
        ax.legend(loc="upper left", fontsize=7, ncol=2)
        ax.xaxis.set_major_locator(mdates.AutoDateLocator())
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))

        # Stats text box
        all_algs = group["algorithms"][:]
        if REFERENCE_ALGORITHM not in all_algs and REFERENCE_ALGORITHM in series:
            all_algs.append(REFERENCE_ALGORITHM)
        stats_text = _build_stats_text(all_algs, series, reference_values)
        ax.text(
            0.99, 0.02, stats_text,
            transform=ax.transAxes,
            fontsize=5.5, fontfamily="monospace",
            verticalalignment="bottom", horizontalalignment="right",
            bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.85),
        )

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
    c_algorithms = discover_c_algorithms()

    generated: dict[str, int] = {}
    for csv_path in sorted(path for path in raw_logs_dir.glob("*.csv") if path.is_file()):
        output_csv = stroke_rate_logs_dir / f"{csv_path.stem}.csv"
        row_count = process_log_file(
            csv_path,
            output_csv=output_csv,
            python_algorithms=algorithms,
            c_algorithms=c_algorithms,
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
