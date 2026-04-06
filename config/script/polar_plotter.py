"""Render Polar logger CSV snapshots into per-row PNG plots."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

from matplotlib import pyplot as plt

SAMPLE_STORE_CAPACITY = 512
LOGS_DIR = Path(__file__).resolve().parent.parent / "logs"
PLOTS_DIR = Path(__file__).resolve().parent.parent / "plots"
AXES = (
    ("x", "#d1495b"),
    ("y", "#2b59c3"),
    ("z", "#2a9d8f"),
)


@dataclass(frozen=True)
class SnapshotRow:
    row_index: int
    timestamp: str
    count: int
    series: dict[str, list[int]]


def _parse_int(value: str | None, default: int = 0) -> int:
    if value in (None, ""):
        return default
    try:
        return int(value)
    except ValueError:
        return default


def parse_snapshot_row(row: dict[str, str], row_index: int) -> SnapshotRow:
    count = max(0, min(SAMPLE_STORE_CAPACITY, _parse_int(row.get("count"))))
    series: dict[str, list[int]] = {}

    for axis_name, _color in AXES:
        axis_series = []
        for sample_index in range(count):
            axis_series.append(_parse_int(row.get(f"{axis_name}_{sample_index:03d}")))
        series[axis_name] = axis_series

    return SnapshotRow(
        row_index=row_index,
        timestamp=row.get("timestamp", ""),
        count=count,
        series=series,
    )


def render_snapshot_plot(snapshot: SnapshotRow, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)

    figure, subplots = plt.subplots(
        len(AXES),
        1,
        figsize=(12, 8),
        sharex=True,
        constrained_layout=True,
    )
    sample_indices = list(range(snapshot.count))

    for subplot, (axis_name, color) in zip(subplots, AXES):
        values = snapshot.series[axis_name]
        subplot.set_ylabel(axis_name.upper())
        subplot.grid(True, alpha=0.3)
        subplot.set_xlim(0, max(snapshot.count - 1, 1))

        if values:
            subplot.plot(sample_indices, values, color=color, linewidth=1.2)
        else:
            subplot.text(
                0.5,
                0.5,
                "No samples captured",
                ha="center",
                va="center",
                transform=subplot.transAxes,
            )

    subplots[-1].set_xlabel("Sample Index")
    figure.suptitle(
        f"{snapshot.timestamp} | row {snapshot.row_index:04d} | samples {snapshot.count}"
    )
    figure.savefig(output_path, dpi=150)
    plt.close(figure)


def iter_snapshot_rows(csv_path: Path):
    with csv_path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row_index, row in enumerate(reader, start=1):
            yield parse_snapshot_row(row, row_index=row_index)


def plot_log_file(csv_path: Path, plots_dir: Path = PLOTS_DIR) -> int:
    output_dir = plots_dir / csv_path.stem
    output_dir.mkdir(parents=True, exist_ok=True)

    for existing_plot in output_dir.glob("snapshot_*.png"):
        existing_plot.unlink()

    generated = 0
    for snapshot in iter_snapshot_rows(csv_path):
        render_snapshot_plot(snapshot, output_dir / f"snapshot_{snapshot.row_index:04d}.png")
        generated += 1

    return generated


def plot_all_logs(logs_dir: Path = LOGS_DIR, plots_dir: Path = PLOTS_DIR) -> dict[str, int]:
    logs_dir = Path(logs_dir)
    plots_dir = Path(plots_dir)
    plots_dir.mkdir(parents=True, exist_ok=True)

    generated: dict[str, int] = {}
    for csv_path in sorted(path for path in logs_dir.glob("*.csv") if path.is_file()):
        generated[csv_path.stem] = plot_log_file(csv_path, plots_dir=plots_dir)
    return generated


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Plot every Polar logger CSV row into per-snapshot PNG files."
    )
    parser.add_argument("--logs-dir", type=Path, default=LOGS_DIR)
    parser.add_argument("--plots-dir", type=Path, default=PLOTS_DIR)
    args = parser.parse_args()

    generated = plot_all_logs(logs_dir=args.logs_dir, plots_dir=args.plots_dir)
    if not generated:
        print(f"No CSV logs found in {args.logs_dir}")
        return 0

    for log_name, plot_count in generated.items():
        print(f"{log_name}: wrote {plot_count} plot(s) to {args.plots_dir / log_name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
