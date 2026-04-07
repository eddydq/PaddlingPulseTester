from _filters import remove_gravity_hpf, vector_magnitude
from common import SAMPLE_STORE_CAPACITY, estimate_autocorrelation_stroke_rate

ALGORITHM_NAME = "autocorrelation_hpf_magnitude"


def calculate(snapshot: dict[str, object]) -> float:
    series = snapshot["series"]
    if len(series["x"]) != SAMPLE_STORE_CAPACITY:
        return 0.0
    values = vector_magnitude(series)
    values = remove_gravity_hpf(values)
    return estimate_autocorrelation_stroke_rate(values)
