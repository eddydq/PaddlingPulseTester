from _filters import (
    adaptive_envelope,
    lowpass_filter,
    remove_gravity_hpf,
    vector_magnitude,
)
from common import SAMPLE_RATE_HZ, SAMPLE_STORE_CAPACITY

ALGORITHM_NAME = "peak_adaptive_magnitude"

MIN_PEAK_INTERVAL = int(SAMPLE_RATE_HZ * 60.0 / 120.0)
MAX_PEAK_INTERVAL = int(SAMPLE_RATE_HZ * 60.0 / 20.0)


def _detect_peaks_adaptive(values):
    env_max, env_min = adaptive_envelope(values)
    peaks = []
    for i in range(1, len(values) - 1):
        dynamic_range = env_max[i] - env_min[i]
        if dynamic_range <= 0.0:
            continue
        threshold = env_min[i] + 0.6 * dynamic_range
        if values[i] > threshold and values[i] >= values[i - 1] and values[i] >= values[i + 1]:
            if not peaks or (i - peaks[-1]) >= MIN_PEAK_INTERVAL:
                peaks.append(i)
    return peaks


def _spm_from_peaks(peaks):
    if len(peaks) < 2:
        return 0.0
    intervals = [
        peaks[i] - peaks[i - 1]
        for i in range(1, len(peaks))
        if MIN_PEAK_INTERVAL <= (peaks[i] - peaks[i - 1]) <= MAX_PEAK_INTERVAL
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
    peaks = _detect_peaks_adaptive(values)
    return _spm_from_peaks(peaks)
