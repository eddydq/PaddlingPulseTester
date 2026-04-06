import csv
import importlib.util
import shutil
import sys
import unittest
from contextlib import contextmanager
from pathlib import Path


def _load_module(module_path: Path, module_name: str):
    spec = importlib.util.spec_from_file_location(module_name, module_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


def _load_calculator_module():
    return _load_module(
        Path("tests/scripts/calculate_stroke_rate.py"),
        "calculate_stroke_rate",
    )


def _write_full_window(csv_path: Path, sample_value: int = 1) -> None:
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        header = ["timestamp", "count"]
        header.extend(
            f"{axis}_{index:03d}"
            for axis in ("x", "y", "z")
            for index in range(512)
        )
        writer.writerow(header)
        row = ["2026-04-06T12:00:00", "512"]
        row.extend(str(sample_value) for _index in range(1536))
        writer.writerow(row)


@contextmanager
def _temporary_root(name: str):
    artifacts_dir = Path("tests/.tmp_test_artifacts")
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    root = artifacts_dir / name
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True, exist_ok=True)
    try:
        yield root
    finally:
        if root.exists():
            shutil.rmtree(root)


class CalculateStrokeRateWorkflowTest(unittest.TestCase):
    def test_process_all_logs_writes_one_csv_and_one_png_per_raw_log(self):
        module = _load_calculator_module()
        with _temporary_root("process_all_logs") as root:
            raw_logs_dir = root / "logs" / "raw_logs"
            stroke_rate_logs_dir = root / "logs" / "stroke_rate_logs"
            png_dir = root / "results" / "png"
            raw_logs_dir.mkdir(parents=True)
            _write_full_window(raw_logs_dir / "polar_log_001.csv")

            generated = module.process_all_logs(
                raw_logs_dir=raw_logs_dir,
                stroke_rate_logs_dir=stroke_rate_logs_dir,
                png_dir=png_dir,
            )

            self.assertIn("polar_log_001", generated)
            self.assertTrue((stroke_rate_logs_dir / "polar_log_001.csv").exists())
            self.assertTrue((png_dir / "polar_log_001.png").exists())

    def test_generated_csv_contains_metadata_and_algorithm_columns(self):
        module = _load_calculator_module()
        with _temporary_root("csv_schema") as root:
            raw_logs_dir = root / "logs" / "raw_logs"
            stroke_rate_logs_dir = root / "logs" / "stroke_rate_logs"
            png_dir = root / "results" / "png"
            raw_logs_dir.mkdir(parents=True)

            _write_full_window(raw_logs_dir / "polar_log_001.csv")

            module.process_all_logs(
                raw_logs_dir=raw_logs_dir,
                stroke_rate_logs_dir=stroke_rate_logs_dir,
                png_dir=png_dir,
            )

            with (stroke_rate_logs_dir / "polar_log_001.csv").open(
                "r",
                newline="",
                encoding="utf-8",
            ) as handle:
                reader = csv.DictReader(handle)
                self.assertEqual(
                    reader.fieldnames[:3],
                    ["timestamp", "row_index", "count"],
                )
                self.assertIn("autocorrelation_x", reader.fieldnames)
                self.assertIn("autocorrelation_y", reader.fieldnames)
                self.assertIn("autocorrelation_z", reader.fieldnames)
                self.assertIn("autocorrelation_magnitude", reader.fieldnames)


if __name__ == "__main__":
    unittest.main()
