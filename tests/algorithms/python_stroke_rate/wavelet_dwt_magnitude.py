import importlib

from _filters import vector_magnitude
from common import SAMPLE_STORE_CAPACITY, estimate_autocorrelation_stroke_rate

ALGORITHM_NAME = "wavelet_dwt_magnitude"

DWT_WAVELET = "db4"
DWT_LEVEL = 5


def calculate(snapshot: dict[str, object]) -> float:
    np = importlib.import_module("numpy")
    pywt = importlib.import_module("pywt")

    series = snapshot["series"]
    if len(series["x"]) != SAMPLE_STORE_CAPACITY:
        return 0.0

    values = vector_magnitude(series)
    data = np.asarray(values, dtype=float)

    coeffs = pywt.wavedec(data, DWT_WAVELET, level=DWT_LEVEL)

    reconstructed = np.zeros_like(data)
    for level_idx in [4, 5]:
        if level_idx < len(coeffs):
            zeroed = [np.zeros_like(c) for c in coeffs]
            zeroed[0] = np.zeros_like(coeffs[0])
            idx = len(coeffs) - level_idx
            if 0 <= idx < len(coeffs):
                zeroed[idx] = coeffs[idx]
            reconstructed += pywt.waverec(zeroed, DWT_WAVELET)[:len(data)]

    return estimate_autocorrelation_stroke_rate(reconstructed.tolist())
