import csv
import importlib.util
import shutil
import sys
import unittest
from contextlib import contextmanager
from pathlib import Path

import numpy as np


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


def _load_logger_module():
    return _load_module(
        Path("tests/scripts/polar_logger.py"),
        "polar_logger",
    )


def _load_common_module():
    return _load_module(
        Path("tests/algorithms/python_stroke_rate/common.py"),
        "stroke_rate_common",
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


def _sinusoid_window(
    stroke_rate_spm: float,
    *,
    amplitude: float = 1.0,
    harmonic: float = 0.15,
    phase: float = 0.0,
) -> list[float]:
    sample_rate_hz = 52.0
    sample_count = 512
    time_axis = np.arange(sample_count, dtype=float) / sample_rate_hz
    fundamental_hz = stroke_rate_spm / 60.0

    values = amplitude * np.sin((2.0 * np.pi * fundamental_hz * time_axis) + phase)
    if harmonic:
        values += harmonic * amplitude * np.sin(
            (4.0 * np.pi * fundamental_hz * time_axis) + (phase / 2.0)
        )
    return values.tolist()


def _tone_window(
    frequency_hz: float,
    *,
    amplitude: float = 1.0,
    phase: float = 0.0,
    sample_rate_hz: float = 52.0,
    sample_count: int = 512,
) -> list[float]:
    time_axis = np.arange(sample_count, dtype=float) / sample_rate_hz
    return (
        amplitude * np.sin((2.0 * np.pi * frequency_hz * time_axis) + phase)
    ).tolist()


def _tone_amplitude(
    values: list[float],
    frequency_hz: float,
    *,
    sample_rate_hz: float = 52.0,
) -> float:
    time_axis = np.arange(len(values), dtype=float) / sample_rate_hz
    centered = np.asarray(values, dtype=float) - np.mean(values)
    sine = np.sin(2.0 * np.pi * frequency_hz * time_axis)
    cosine = np.cos(2.0 * np.pi * frequency_hz * time_axis)
    return float(
        2.0
        * np.hypot(np.dot(centered, sine), np.dot(centered, cosine))
        / len(values)
    )


def _zero_lag(filtered: list[float], reference: list[float]) -> int:
    filtered_centered = np.asarray(filtered) - np.mean(filtered)
    reference_centered = np.asarray(reference) - np.mean(reference)
    return int(
        np.argmax(np.correlate(filtered_centered, reference_centered, mode="full"))
        - (len(reference_centered) - 1)
    )


class CalculateStrokeRateWorkflowTest(unittest.TestCase):
    def test_logger_defaults_to_tests_raw_logs(self):
        logger_module = _load_logger_module()
        self.assertEqual(logger_module.LOGS_DIR, Path("tests/logs/raw_logs").resolve())

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

    def test_generated_csv_includes_firmware_exact_column(self):
        module = _load_calculator_module()
        with _temporary_root("firmware_exact") as root:
            raw_logs_dir = root / "logs" / "raw_logs"
            stroke_rate_logs_dir = root / "logs" / "stroke_rate_logs"
            png_dir = root / "results" / "png"
            raw_logs_dir.mkdir(parents=True)

            _write_full_window(raw_logs_dir / "polar_log_001.csv", sample_value=7)

            module.process_all_logs(
                raw_logs_dir=raw_logs_dir,
                stroke_rate_logs_dir=stroke_rate_logs_dir,
                png_dir=png_dir,
                firmware_exact_source=Path("tests/algorithms/c_stroke_rate/stroke_rate_firmware_exact.c"),
            )

            with (stroke_rate_logs_dir / "polar_log_001.csv").open(
                "r",
                newline="",
                encoding="utf-8",
            ) as handle:
                reader = csv.DictReader(handle)
                self.assertIn("firmware_exact_z", reader.fieldnames)


class ConsensusMusicHelpersTest(unittest.TestCase):
    def test_zero_phase_bandpass_keeps_zero_lag_and_attenuates_out_of_band_tone(self):
        common = _load_common_module()
        in_band = _tone_window(1.0)
        out_of_band = _tone_window(8.0, amplitude=0.35, phase=np.pi / 3.0)
        source = (np.asarray(in_band) + np.asarray(out_of_band)).tolist()

        filtered = common._zero_phase_bandpass(source)

        self.assertEqual(_zero_lag(filtered, in_band), 0)
        self.assertGreater(_tone_amplitude(filtered, 1.0), _tone_amplitude(source, 1.0) * 0.7)
        self.assertLess(_tone_amplitude(filtered, 8.0), _tone_amplitude(source, 8.0) * 0.2)

    def test_zero_phase_bandpass_returns_empty_for_short_inputs(self):
        common = _load_common_module()

        self.assertEqual(common._zero_phase_bandpass([1.0] * 26), [])

    def test_yin_period_candidate_tracks_known_rate(self):
        common = _load_common_module()
        filtered = common._zero_phase_bandpass(_sinusoid_window(60.0, harmonic=0.20))

        period = common._yin_period_candidate(filtered)

        self.assertIsNotNone(period)
        self.assertAlmostEqual(period, 52, delta=2)

    def test_cepstrum_period_candidate_tracks_known_rate(self):
        common = _load_common_module()
        filtered = common._zero_phase_bandpass(_sinusoid_window(78.0, harmonic=0.35))

        period = common._cepstrum_period_candidate(filtered)

        self.assertIsNotNone(period)
        self.assertAlmostEqual(period, 40, delta=2)

    def test_period_candidates_return_none_for_flat_window(self):
        common = _load_common_module()
        flat_window = [0.0] * 512

        self.assertIsNone(common._yin_period_candidate(flat_window))
        self.assertIsNone(common._cepstrum_period_candidate(flat_window))

    def test_period_candidates_return_none_for_undersized_window(self):
        common = _load_common_module()
        undersized_window = [0.0] * 26

        self.assertIsNone(common._yin_period_candidate(undersized_window))
        self.assertIsNone(common._cepstrum_period_candidate(undersized_window))


if __name__ == "__main__":
    unittest.main()
