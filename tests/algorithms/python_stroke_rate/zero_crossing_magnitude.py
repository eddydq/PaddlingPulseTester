from _filters import bandpass_filter, vector_magnitude
from common import SAMPLE_RATE_HZ, SAMPLE_STORE_CAPACITY

ALGORITHM_NAME = "zero_crossing_magnitude"

HYSTERESIS_BAND = 0.05


def _count_zero_crossings(values):
    count = 0
    armed = False
    for v in values:
        if not armed and v < -HYSTERESIS_BAND:
            armed = True
        elif armed and v > HYSTERESIS_BAND:
            count += 1
            armed = False
    return count


def calculate(snapshot: dict[str, object]) -> float:
    series = snapshot["series"]
    if len(series["x"]) != SAMPLE_STORE_CAPACITY:
        return 0.0
    values = vector_magnitude(series)
    values = bandpass_filter(values, low_hz=0.25, high_hz=2.5)
    crossings = _count_zero_crossings(values)
    duration_minutes = len(values) / SAMPLE_RATE_HZ / 60.0
    if duration_minutes <= 0.0 or crossings < 2:
        return 0.0
    return crossings / duration_minutes
