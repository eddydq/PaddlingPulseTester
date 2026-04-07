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


def _load_algorithm_module(stem: str):
    algorithms_dir = Path("tests/algorithms/python_stroke_rate").resolve()
    if str(algorithms_dir) not in sys.path:
        sys.path.insert(0, str(algorithms_dir))
    return _load_module(
        algorithms_dir / f"{stem}.py",
        f"stroke_rate_algorithm_{stem}",
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


def _snapshot_from_y(values: list[float]) -> dict[str, object]:
    zeros = [0.0] * len(values)
    return {
        "series": {
            "x": zeros,
            "y": list(values),
            "z": zeros,
        }
    }


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


@contextmanager
def _temporarily_unload_modules(*module_names: str):
    original_modules = {
        module_name: sys.modules.pop(module_name, None) for module_name in module_names
    }
    try:
        yield
    finally:
        for module_name, module in original_modules.items():
            if module is None:
                sys.modules.pop(module_name, None)
            else:
                sys.modules[module_name] = module


@contextmanager
def _without_algorithm_import_side_effects():
    algorithms_dir = Path("tests/algorithms/python_stroke_rate")
    algorithm_paths = {
        str(algorithms_dir),
        str(algorithms_dir.resolve()),
    }
    original_sys_path = list(sys.path)
    original_modules = {
        "common": sys.modules.pop("common", None),
        "stroke_rate_algorithm_consensus_music_y": sys.modules.pop(
            "stroke_rate_algorithm_consensus_music_y", None
        ),
    }
    sys.path[:] = [entry for entry in sys.path if entry not in algorithm_paths]
    try:
        yield
    finally:
        sys.path[:] = original_sys_path
        for module_name, module in original_modules.items():
            if module is None:
                sys.modules.pop(module_name, None)
            else:
                sys.modules[module_name] = module


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


def _ramp_window(
    *,
    sample_count: int = 512,
) -> list[float]:
    return np.linspace(0.0, 1.0, sample_count, dtype=float).tolist()


def _seeded_noise_window(
    seed: int,
    *,
    sample_count: int = 512,
) -> list[float]:
    return np.random.default_rng(seed).normal(0.0, 1.0, sample_count).tolist()


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
                self.assertIn("consensus_music_y", reader.fieldnames)
                self.assertNotIn("consensus_music_magnitude", reader.fieldnames)

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

    def test_yin_period_candidate_returns_none_for_seeded_noise_window(self):
        common = _load_common_module()
        filtered = common._zero_phase_bandpass(_seeded_noise_window(0))

        self.assertIsNone(common._yin_period_candidate(filtered))

    def test_yin_period_candidate_returns_none_for_ramp_window(self):
        common = _load_common_module()
        filtered = common._zero_phase_bandpass(_ramp_window())

        self.assertIsNone(common._yin_period_candidate(filtered))

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

    def test_consensus_period_band_is_narrow_when_estimators_agree(self):
        common = _load_common_module()

        band = common._consensus_period_band(52, 53, min_lag=26, max_lag=156)

        self.assertEqual(band, (50, 55))

    def test_consensus_period_band_spans_candidates_when_estimators_disagree(self):
        common = _load_common_module()

        band = common._consensus_period_band(40, 52, min_lag=26, max_lag=156)

        self.assertEqual(band, (40, 52))

    def test_music_frequency_refines_known_rate(self):
        common = _load_common_module()
        expected_spm = 58.25
        filtered = common._zero_phase_bandpass(
            _sinusoid_window(expected_spm, harmonic=0.05)
        )
        band = common._consensus_period_band(53, 54, min_lag=26, max_lag=156)

        self.assertIsNotNone(band)

        low_frequency_hz = 52.0 / band[1]
        high_frequency_hz = 52.0 / band[0]
        refined_frequency_hz = common._music_frequency_hz(
            filtered,
            sample_rate_hz=52.0,
            low_frequency_hz=low_frequency_hz,
            high_frequency_hz=high_frequency_hz,
        )

        self.assertIsNotNone(refined_frequency_hz)

        refined_spm = refined_frequency_hz * 60.0
        coarse_spm = (52.0 * 60.0) / 54.0
        self.assertAlmostEqual(refined_spm, expected_spm, delta=0.1)
        self.assertLess(abs(refined_spm - expected_spm), abs(coarse_spm - expected_spm))

    def test_music_frequency_returns_none_for_flat_window(self):
        common = _load_common_module()

        self.assertIsNone(
            common._music_frequency_hz(
                [0.0] * 512,
                sample_rate_hz=52.0,
                low_frequency_hz=52.0 / 56.0,
                high_frequency_hz=52.0 / 51.0,
            )
        )

    def test_music_frequency_returns_none_for_seeded_noise_window(self):
        common = _load_common_module()
        rng = np.random.default_rng(12345)
        filtered_noise = common._zero_phase_bandpass(
            rng.normal(0.0, 1.0, 512).tolist()
        )

        self.assertIsNone(
            common._music_frequency_hz(
                filtered_noise,
                sample_rate_hz=52.0,
                low_frequency_hz=52.0 / 56.0,
                high_frequency_hz=52.0 / 51.0,
            )
        )

    def test_music_frequency_returns_none_for_non_finite_numeric_parameters(self):
        common = _load_common_module()
        filtered = common._zero_phase_bandpass(_sinusoid_window(58.25, harmonic=0.05))

        invalid_cases = (
            {
                "sample_rate_hz": float("nan"),
                "low_frequency_hz": 52.0 / 56.0,
                "high_frequency_hz": 52.0 / 51.0,
            },
            {
                "sample_rate_hz": 52.0,
                "low_frequency_hz": float("nan"),
                "high_frequency_hz": 52.0 / 51.0,
            },
            {
                "sample_rate_hz": 52.0,
                "low_frequency_hz": 52.0 / 56.0,
                "high_frequency_hz": float("inf"),
            },
        )

        for kwargs in invalid_cases:
            with self.subTest(kwargs=kwargs):
                self.assertIsNone(common._music_frequency_hz(filtered, **kwargs))

    def test_music_frequency_returns_none_for_invalid_grid_size(self):
        common = _load_common_module()
        filtered = common._zero_phase_bandpass(_sinusoid_window(58.25, harmonic=0.05))

        for grid_size in (0, -4):
            with self.subTest(grid_size=grid_size):
                self.assertIsNone(
                    common._music_frequency_hz(
                        filtered,
                        sample_rate_hz=52.0,
                        low_frequency_hz=52.0 / 56.0,
                        high_frequency_hz=52.0 / 51.0,
                        grid_size=grid_size,
                    )
                )

    def test_consensus_music_estimator_returns_zero_for_short_window(self):
        common = _load_common_module()

        self.assertEqual(common.estimate_consensus_music_stroke_rate([0.0] * 32), 0.0)

    def test_consensus_music_estimator_returns_in_range_value(self):
        common = _load_common_module()

        estimate = common.estimate_consensus_music_stroke_rate(
            _sinusoid_window(61.5, harmonic=0.10)
        )

        self.assertGreater(estimate, 20.0)
        self.assertLess(estimate, 120.0)
        self.assertAlmostEqual(estimate, 61.5, delta=1.0)

    def test_consensus_music_estimator_tracks_low_end_valid_rates(self):
        common = _load_common_module()

        for stroke_rate_spm in (24.0, 28.0, 34.0):
            with self.subTest(stroke_rate_spm=stroke_rate_spm):
                estimate = common.estimate_consensus_music_stroke_rate(
                    _sinusoid_window(stroke_rate_spm, harmonic=0.10)
                )
                self.assertAlmostEqual(estimate, stroke_rate_spm, delta=1.0)

    def test_consensus_music_estimator_returns_zero_for_seeded_noise_window(self):
        common = _load_common_module()

        self.assertEqual(
            common.estimate_consensus_music_stroke_rate(_seeded_noise_window(0)),
            0.0,
        )

    def test_consensus_music_estimator_returns_zero_for_known_bad_noise_seed_29(self):
        common = _load_common_module()

        self.assertEqual(
            common.estimate_consensus_music_stroke_rate(_seeded_noise_window(29)),
            0.0,
        )

    def test_consensus_music_estimator_returns_zero_for_known_bad_noise_seed_46(self):
        common = _load_common_module()

        self.assertEqual(
            common.estimate_consensus_music_stroke_rate(_seeded_noise_window(46)),
            0.0,
        )

    def test_consensus_music_estimator_returns_zero_for_ramp_window(self):
        common = _load_common_module()

        self.assertEqual(
            common.estimate_consensus_music_stroke_rate(_ramp_window()),
            0.0,
        )

    def test_consensus_music_estimator_returns_zero_for_invalid_parameters(self):
        common = _load_common_module()
        values = _sinusoid_window(61.5, harmonic=0.10)

        invalid_cases = (
            {"sample_rate_hz": float("nan")},
            {"min_stroke_rate_spm": float("nan")},
            {"max_stroke_rate_spm": float("nan")},
            {"min_stroke_rate_spm": 90.0, "max_stroke_rate_spm": 45.0},
        )

        for kwargs in invalid_cases:
            with self.subTest(kwargs=kwargs):
                self.assertEqual(
                    common.estimate_consensus_music_stroke_rate(values, **kwargs),
                    0.0,
                )

    def test_consensus_music_estimator_returns_zero_when_cepstrum_candidate_is_missing(self):
        common = _load_common_module()
        original_cepstrum_period_candidate = common._cepstrum_period_candidate

        try:
            common._cepstrum_period_candidate = lambda *_args, **_kwargs: None
            self.assertEqual(
                common.estimate_consensus_music_stroke_rate(
                    _sinusoid_window(61.5, harmonic=0.10)
                ),
                0.0,
            )
        finally:
            common._cepstrum_period_candidate = original_cepstrum_period_candidate

    def test_consensus_music_estimator_falls_back_to_yin_when_music_returns_none(self):
        common = _load_common_module()
        original_music_frequency_hz = common._music_frequency_hz

        try:
            common._music_frequency_hz = lambda *_args, **_kwargs: None
            estimate = common.estimate_consensus_music_stroke_rate(
                _sinusoid_window(61.5, harmonic=0.10)
            )
        finally:
            common._music_frequency_hz = original_music_frequency_hz

        self.assertGreater(estimate, 0.0)
        self.assertAlmostEqual(estimate, 61.5, delta=1.0)

    def test_consensus_music_estimator_falls_back_to_yin_when_music_raises(self):
        common = _load_common_module()
        original_music_frequency_hz = common._music_frequency_hz

        def raising_music_frequency_hz(*_args, **_kwargs):
            raise RuntimeError("unexpected music failure")

        try:
            common._music_frequency_hz = raising_music_frequency_hz
            estimate = common.estimate_consensus_music_stroke_rate(
                _sinusoid_window(61.5, harmonic=0.10)
            )
        finally:
            common._music_frequency_hz = original_music_frequency_hz

        self.assertGreater(estimate, 0.0)
        self.assertAlmostEqual(estimate, 61.5, delta=1.0)

    def test_consensus_music_estimator_falls_back_to_yin_when_consensus_band_is_too_wide(self):
        common = _load_common_module()
        original_consensus_period_band = common._consensus_period_band
        original_music_frequency_hz = common._music_frequency_hz

        try:
            common._consensus_period_band = lambda *_args, **_kwargs: (40, 80)
            common._music_frequency_hz = lambda *_args, **_kwargs: 999.0 / 60.0
            values = _sinusoid_window(61.5, harmonic=0.10)
            filtered = common._zero_phase_bandpass(values)
            yin_period = common._yin_period_candidate(filtered)
            self.assertIsNotNone(yin_period)
            expected = (52.0 * 60.0) / yin_period
            estimate = common.estimate_consensus_music_stroke_rate(values)
        finally:
            common._consensus_period_band = original_consensus_period_band
            common._music_frequency_hz = original_music_frequency_hz

        self.assertAlmostEqual(estimate, expected, delta=0.01)

    def test_consensus_music_estimator_returns_zero_when_yin_candidate_is_missing(self):
        common = _load_common_module()
        original_yin_period_candidate = common._yin_period_candidate
        original_music_frequency_hz = common._music_frequency_hz
        original_consensus_period_band = common._consensus_period_band

        try:
            common._yin_period_candidate = lambda *_args, **_kwargs: None
            common._music_frequency_hz = lambda *_args, **_kwargs: 999.0 / 60.0
            common._consensus_period_band = lambda *_args, **_kwargs: (40, 80)
            self.assertEqual(
                common.estimate_consensus_music_stroke_rate(
                    _sinusoid_window(61.5, harmonic=0.10)
                ),
                0.0,
            )
        finally:
            common._yin_period_candidate = original_yin_period_candidate
            common._music_frequency_hz = original_music_frequency_hz
            common._consensus_period_band = original_consensus_period_band

    def test_consensus_music_y_calculate_reads_y_axis_snapshot(self):
        with _temporarily_unload_modules("common", "stroke_rate_algorithm_consensus_music_y"):
            with _without_algorithm_import_side_effects():
                algorithm = _load_algorithm_module("consensus_music_y")

                estimate = algorithm.calculate(
                    _snapshot_from_y(_sinusoid_window(61.5, harmonic=0.10))
                )

            self.assertNotIn("common", sys.modules)
            self.assertNotIn("stroke_rate_algorithm_consensus_music_y", sys.modules)

        self.assertAlmostEqual(estimate, 61.5, delta=1.0)

    def test_discover_python_algorithms_includes_consensus_music_y_not_magnitude(self):
        calculator = _load_calculator_module()

        names = {algorithm.name for algorithm in calculator.discover_python_algorithms()}

        self.assertIn("consensus_music_y", names)
        self.assertNotIn("consensus_music_magnitude", names)


if __name__ == "__main__":
    unittest.main()
