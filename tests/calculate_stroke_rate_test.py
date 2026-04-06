import importlib.util
import sys
import unittest
from pathlib import Path

import numpy as np


def _load_module(module_path: Path, module_name: str):
    spec = importlib.util.spec_from_file_location(module_name, module_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


def _load_common_module():
    return _load_module(
        Path("tests/algorithms/python_stroke_rate/common.py"),
        "stroke_rate_common",
    )


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


class ConsensusMusicHelpersTest(unittest.TestCase):
    def test_zero_phase_bandpass_keeps_zero_lag_correlation(self):
        common = _load_common_module()
        source = _sinusoid_window(60.0, harmonic=0.10)

        filtered = common._zero_phase_bandpass(source)

        source_centered = np.asarray(source) - np.mean(source)
        filtered_centered = np.asarray(filtered) - np.mean(filtered)
        lag = int(
            np.argmax(np.correlate(filtered_centered, source_centered, mode="full"))
            - (len(source_centered) - 1)
        )

        self.assertEqual(lag, 0)


if __name__ == "__main__":
    unittest.main()
