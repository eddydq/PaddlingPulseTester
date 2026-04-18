from _filters import bandpass_filter
from common import SAMPLE_STORE_CAPACITY, estimate_autocorrelation_stroke_rate

ALGORITHM_NAME = "autocorrelation_bandpass_y"


def calculate(snapshot: dict[str, object]) -> float:
    series = snapshot["series"]
    if len(series["y"]) != SAMPLE_STORE_CAPACITY:
        return 0.0
    values = bandpass_filter(list(series["y"]))
    return estimate_autocorrelation_stroke_rate(values)
