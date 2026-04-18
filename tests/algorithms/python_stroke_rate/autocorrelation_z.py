from common import estimate_autocorrelation_stroke_rate

ALGORITHM_NAME = "autocorrelation_z"


def calculate(snapshot: dict[str, object]) -> float:
    series = snapshot["series"]
    return estimate_autocorrelation_stroke_rate(series["z"])
