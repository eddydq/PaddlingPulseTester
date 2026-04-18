from __future__ import annotations

import argparse
import csv
import importlib.util
import sys
from pathlib import Path
from datetime import datetime

import matplotlib
matplotlib.use("Agg")
from matplotlib import dates as mdates
from matplotlib import pyplot as plt

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from analysis.scripts.blocks import Packet, PipelineExecutor

SAMPLE_STORE_CAPACITY = 512
AXES = ("x", "y", "z")

ANALYSIS_ROOT = PROJECT_ROOT / "analysis"
RAW_LOGS_DIR = ANALYSIS_ROOT / "logs" / "raw_logs"
STROKE_RATE_LOGS_DIR = ANALYSIS_ROOT / "logs" / "stroke_rate_logs"
PNG_DIR = ANALYSIS_ROOT / "results" / "png"
PIPELINES_DIR = ANALYSIS_ROOT / "pipelines"

def load_pipeline_modules():
    pipelines = []
    if not PIPELINES_DIR.exists():
        return pipelines
    for module_path in sorted(PIPELINES_DIR.glob("*.py")):
        if module_path.name == "__init__.py" or module_path.name.startswith("_"):
            continue
        spec = importlib.util.spec_from_file_location(module_path.stem, module_path)
        if spec and spec.loader:
            module = importlib.util.module_from_spec(spec)
            sys.modules[module_path.stem] = module
            spec.loader.exec_module(module)
            pipelines.append(module)
    return pipelines

def parse_snapshot_row(row: dict[str, str], row_index: int) -> Packet:
    count_str = row.get("count", "0")
    count = max(0, min(SAMPLE_STORE_CAPACITY, int(count_str) if count_str else 0))
    series: dict[str, list[float]] = {"x": [], "y": [], "z": []}

    for axis_name in AXES:
        for sample_index in range(count):
            val = row.get(f"{axis_name}_{sample_index:03d}")
            series[axis_name].append(float(val) if val else 0.0)

    return Packet(
        kind="raw_window",
        data={"series": series},
        timestamp=row.get("timestamp", ""),
        sample_rate_hz=52.0
    )

def _format_rate(value: float) -> str:
    return f"{value:.3f}"

def process_log_file(csv_path: Path, output_csv: Path, pipelines: list) -> int:
    output_csv.parent.mkdir(parents=True, exist_ok=True)

    generated = 0
    fieldnames = ["timestamp", "row_index", "count"]
    for p in pipelines:
        fieldnames.append(p.OUTPUT_SERIES_NAME)

    executors = []
    for p in pipelines:
        executors.append((p.OUTPUT_SERIES_NAME, PipelineExecutor(p.get_blocks()), p.get_graph()))

    with csv_path.open("r", newline="", encoding="utf-8") as handle, \
         output_csv.open("w", newline="", encoding="utf-8") as out_handle:
        
        reader = csv.DictReader(handle)
        writer = csv.DictWriter(out_handle, fieldnames=fieldnames)
        writer.writeheader()

        for row_index, row in enumerate(reader, start=1):
            packet = parse_snapshot_row(row, row_index)
            if packet.data["series"]["y"] and len(packet.data["series"]["y"]) == SAMPLE_STORE_CAPACITY:
                
                out_row = {
                    "timestamp": packet.timestamp,
                    "row_index": str(row_index),
                    "count": str(len(packet.data["series"]["y"]))
                }

                for series_name, executor, graph in executors:
                    outputs, _ = executor.run(graph, {"input.raw": [packet]})
                    accepted = outputs.get("accepted", [])
                    spm_val = 0.0
                    if accepted:
                        spm_val = float(accepted[0].data.get("spm", 0.0))
                    out_row[series_name] = _format_rate(spm_val)

                writer.writerow(out_row)
                generated += 1

    return generated

def render_plot_csv(csv_path: Path, output_png: Path, series_names: list[str]) -> int:
    timestamps: list[datetime] = []
    series_data = {name: [] for name in series_names}

    with csv_path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            ts_str = row.get("timestamp", "")
            try:
                timestamps.append(datetime.fromisoformat(ts_str))
            except ValueError:
                continue
            
            for name in series_names:
                spm_str = row.get(name, "0.0")
                try:
                    series_data[name].append(float(spm_str))
                except ValueError:
                    series_data[name].append(0.0)

    if not timestamps:
        return 0

    output_png.parent.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(1, 1, figsize=(14, 4), constrained_layout=True)
    
    for name in series_names:
        ax.plot(timestamps, series_data[name], label=name, linewidth=1.5)

    ax.set_title(f"{csv_path.stem} — Stroke Rates (Pipelines)")
    ax.set_ylabel("SPM")
    ax.grid(True, alpha=0.3)
    ax.legend(loc="upper left", fontsize=10)
    ax.xaxis.set_major_locator(mdates.AutoDateLocator())
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))

    fig.autofmt_xdate()
    fig.savefig(output_png, dpi=150)
    plt.close(fig)
    return 1

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Calculate stroke-rate CSVs using all defined pipelines and render PNG plots."
    )
    parser.add_argument("--raw-logs-dir", type=Path, default=RAW_LOGS_DIR)
    parser.add_argument("--stroke-rate-logs-dir", type=Path, default=STROKE_RATE_LOGS_DIR)
    parser.add_argument("--png-dir", type=Path, default=PNG_DIR)
    args = parser.parse_args()

    pipelines = load_pipeline_modules()
    if not pipelines:
        print("No pipelines found in analysis/pipelines/")
        return 0

    if not args.raw_logs_dir.exists():
        print(f"No raw CSV logs directory found at {args.raw_logs_dir}")
        return 0

    generated_files = 0
    series_names = [p.OUTPUT_SERIES_NAME for p in pipelines]

    for csv_path in sorted(path for path in args.raw_logs_dir.glob("*.csv") if path.is_file()):
        output_csv = args.stroke_rate_logs_dir / f"{csv_path.stem}.csv"
        
        row_count = process_log_file(csv_path, output_csv, pipelines)
        if row_count > 0:
            render_plot_csv(output_csv, args.png_dir / f"{csv_path.stem}.png", series_names)
            print(f"{csv_path.stem}: wrote {row_count} row(s) to {output_csv}")
            generated_files += 1

    if generated_files == 0:
        print(f"No raw CSV logs found in {args.raw_logs_dir}")

    return 0

if __name__ == "__main__":
    raise SystemExit(main())
