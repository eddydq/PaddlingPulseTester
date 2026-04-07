import importlib

from _filters import remove_gravity_hpf
from common import SAMPLE_RATE_HZ, SAMPLE_STORE_CAPACITY

ALGORITHM_NAME = "fft_dominant_y"

MIN_FREQ_HZ = 20.0 / 60.0
MAX_FREQ_HZ = 120.0 / 60.0


def _fft_dominant_frequency(values, sample_rate_hz):
    np = importlib.import_module("numpy")

    data = np.asarray(values, dtype=float)
    data = data - np.mean(data)
    window = np.hanning(len(data))
    windowed = data * window

    spectrum = np.abs(np.fft.rfft(windowed))
    freqs = np.fft.rfftfreq(len(windowed), d=1.0 / sample_rate_hz)

    mask = (freqs >= MIN_FREQ_HZ) & (freqs <= MAX_FREQ_HZ)
    if not np.any(mask):
        return 0.0

    masked_spectrum = spectrum.copy()
    masked_spectrum[~mask] = 0.0
    peak_idx = int(np.argmax(masked_spectrum))

    if peak_idx == 0 or peak_idx >= len(spectrum) - 1:
        return freqs[peak_idx]

    alpha = spectrum[peak_idx - 1]
    beta = spectrum[peak_idx]
    gamma = spectrum[peak_idx + 1]
    denom = alpha - 2.0 * beta + gamma
    if abs(denom) < 1e-12:
        return freqs[peak_idx]

    delta = 0.5 * (alpha - gamma) / denom
    return freqs[peak_idx] + delta * (freqs[1] - freqs[0])


def calculate(snapshot: dict[str, object]) -> float:
    series = snapshot["series"]
    if len(series["y"]) != SAMPLE_STORE_CAPACITY:
        return 0.0
    values = remove_gravity_hpf(list(series["y"]))
    freq_hz = _fft_dominant_frequency(values, SAMPLE_RATE_HZ)
    if freq_hz <= 0.0:
        return 0.0
    spm = freq_hz * 60.0
    if spm < 20.0 or spm > 120.0:
        return 0.0
    return spm
