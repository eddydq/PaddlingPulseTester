import csv
import importlib.util
import tempfile
import unittest
from pathlib import Path


def _load_module(module_path: Path, module_name: str):
    spec = importlib.util.spec_from_file_location(module_name, module_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def _load_calculator_module():
    return _load_module(
        Path("tests/scripts/calculate_stroke_rate.py"),
        "calculate_stroke_rate",
    )


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


if __name__ == "__main__":
    unittest.main()
