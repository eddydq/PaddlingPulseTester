from common import estimate_autocorrelation_stroke_rate

ALGORITHM_NAME = "autocorrelation_x"


def calculate(snapshot: dict[str, object]) -> float:
    series = snapshot["series"]
    return estimate_autocorrelation_stroke_rate(series["x"])
