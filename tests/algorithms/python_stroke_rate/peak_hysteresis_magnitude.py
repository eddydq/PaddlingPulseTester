from _filters import (
    adaptive_envelope,
    lowpass_filter,
    remove_gravity_hpf,
    vector_magnitude,
)
from common import SAMPLE_RATE_HZ, SAMPLE_STORE_CAPACITY

ALGORITHM_NAME = "peak_hysteresis_magnitude"

HIGH_FRACTION = 0.6
LOW_FRACTION = 0.3
MIN_PEAK_INTERVAL = int(SAMPLE_RATE_HZ * 60.0 / 120.0)
MAX_PEAK_INTERVAL = int(SAMPLE_RATE_HZ * 60.0 / 20.0)


def _schmitt_trigger(values):
    env_max, env_min = adaptive_envelope(values)
    triggers = []
    armed = False
    for i, v in enumerate(values):
        dynamic_range = env_max[i] - env_min[i]
        if dynamic_range <= 0.0:
            continue
        high_thresh = env_min[i] + HIGH_FRACTION * dynamic_range
        low_thresh = env_min[i] + LOW_FRACTION * dynamic_range
        if not armed and v >= high_thresh:
            armed = True
            if not triggers or (i - triggers[-1]) >= MIN_PEAK_INTERVAL:
                triggers.append(i)
        elif armed and v <= low_thresh:
            armed = False
    return triggers


def _spm_from_triggers(triggers):
    if len(triggers) < 2:
        return 0.0
    intervals = [
        triggers[i] - triggers[i - 1]
        for i in range(1, len(triggers))
        if MIN_PEAK_INTERVAL <= (triggers[i] - triggers[i - 1]) <= MAX_PEAK_INTERVAL
    ]
    if not intervals:
        return 0.0
    mean_interval = sum(intervals) / len(intervals)
    return (SAMPLE_RATE_HZ * 60.0) / mean_interval


def calculate(snapshot: dict[str, object]) -> float:
    series = snapshot["series"]
    if len(series["x"]) != SAMPLE_STORE_CAPACITY:
        return 0.0
    values = vector_magnitude(series)
    values = remove_gravity_hpf(values)
    values = lowpass_filter(values)
    triggers = _schmitt_trigger(values)
    return _spm_from_triggers(triggers)
