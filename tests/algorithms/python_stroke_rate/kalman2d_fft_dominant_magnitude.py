from _filters import kalman_2d_smooth
from fft_dominant_magnitude import calculate as _raw_calculate

ALGORITHM_NAME = "kalman2d_fft_dominant_magnitude"

_raw_history = []


def reset():
    global _raw_history
    _raw_history = []


def calculate(snapshot: dict[str, object]) -> float:
    raw = _raw_calculate(snapshot)
    _raw_history.append(raw)
    smoothed = kalman_2d_smooth(_raw_history)
    return smoothed[-1] if smoothed else 0.0
