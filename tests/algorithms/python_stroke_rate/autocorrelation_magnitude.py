from common import estimate_autocorrelation_stroke_rate, magnitude_series

ALGORITHM_NAME = "autocorrelation_magnitude"


def calculate(snapshot: dict[str, object]) -> float:
    series = snapshot["series"]
    return estimate_autocorrelation_stroke_rate(magnitude_series(series))
