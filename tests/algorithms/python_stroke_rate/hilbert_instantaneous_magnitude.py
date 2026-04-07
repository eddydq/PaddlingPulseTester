import importlib

from _filters import bandpass_filter, vector_magnitude
from common import SAMPLE_RATE_HZ, SAMPLE_STORE_CAPACITY

ALGORITHM_NAME = "hilbert_instantaneous_magnitude"

MIN_SPM = 20.0
MAX_SPM = 120.0


def calculate(snapshot: dict[str, object]) -> float:
    np = importlib.import_module("numpy")
    scipy_signal = importlib.import_module("scipy.signal")

    series = snapshot["series"]
    if len(series["x"]) != SAMPLE_STORE_CAPACITY:
        return 0.0

    values = vector_magnitude(series)
    values = bandpass_filter(values, low_hz=0.25, high_hz=2.5)

    analytic = scipy_signal.hilbert(values)
    instantaneous_phase = np.unwrap(np.angle(analytic))
    instantaneous_freq = np.diff(instantaneous_phase) / (2.0 * np.pi) * SAMPLE_RATE_HZ

    valid = instantaneous_freq[(instantaneous_freq >= MIN_SPM / 60.0) & (instantaneous_freq <= MAX_SPM / 60.0)]
    if len(valid) == 0:
        return 0.0

    median_freq_hz = float(np.median(valid))
    spm = median_freq_hz * 60.0
    if spm < MIN_SPM or spm > MAX_SPM:
        return 0.0
    return spm
