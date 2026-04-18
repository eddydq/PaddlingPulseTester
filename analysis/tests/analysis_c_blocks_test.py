from pathlib import Path


def test_c_block_sources_exist_for_each_group():
    expected = [
        Path("analysis/algorithms/representation/c/vector_magnitude.c"),
        Path("analysis/algorithms/pretraitement/c/highpass.c"),
        Path("analysis/algorithms/estimation/c/autocorrelation.c"),
        Path("analysis/algorithms/detection/c/zero_crossing_detect.c"),
        Path("analysis/algorithms/validation/c/spm_range_gate.c"),
        Path("analysis/algorithms/suivi/c/kalman_2d.c"),
    ]
    for path in expected:
        assert path.exists(), f"missing {path}"
