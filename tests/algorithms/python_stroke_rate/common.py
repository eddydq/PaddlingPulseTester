from __future__ import annotations

import importlib
import math

SAMPLE_STORE_CAPACITY = 512
SAMPLE_RATE_HZ = 52.0
MIN_STROKE_RATE_SPM = 20.0
MAX_STROKE_RATE_SPM = 120.0
PEAK_SCORE_FRACTION = 0.8
BUTTERWORTH_ORDER = 4
FILTER_MARGIN_HZ = 0.10
YIN_THRESHOLD = 0.15
YIN_FALLBACK_MAX_CMNDF = 0.25
NUMERICAL_EPSILON = 1e-12
CONSENSUS_TOLERANCE_FRACTION = 0.05
CONSENSUS_MARGIN_SAMPLES = 2
MUSIC_GRID_SIZE = 4096
MUSIC_SNAPSHOT_LENGTH = 96
MUSIC_MIN_PEAK_PROMINENCE_RATIO = 1.5
MUSIC_SINGLE_CANDIDATE_MIN_PEAK_PROMINENCE_RATIO = 4.0

_NUMPY_MODULE = None
_SCIPY_SIGNAL_MODULE = None


def _get_numpy():
    global _NUMPY_MODULE

    if _NUMPY_MODULE is None:
        _NUMPY_MODULE = importlib.import_module("numpy")
    return _NUMPY_MODULE


def _get_scipy_signal():
    global _SCIPY_SIGNAL_MODULE

    if _SCIPY_SIGNAL_MODULE is None:
        _SCIPY_SIGNAL_MODULE = importlib.import_module("scipy.signal")
    return _SCIPY_SIGNAL_MODULE


def _stroke_rate_bounds_hz(
    *,
    sample_rate_hz: float = SAMPLE_RATE_HZ,
    min_stroke_rate_spm: float = MIN_STROKE_RATE_SPM,
    max_stroke_rate_spm: float = MAX_STROKE_RATE_SPM,
) -> tuple[float, float]:
    if sample_rate_hz <= 0.0:
        raise ValueError("sample_rate_hz must be positive")

    nyquist_hz = sample_rate_hz / 2.0
    low_hz = max(0.01, (min_stroke_rate_spm / 60.0) - FILTER_MARGIN_HZ)
    high_hz = min((max_stroke_rate_spm / 60.0) + FILTER_MARGIN_HZ, nyquist_hz * 0.95)
    if low_hz >= high_hz:
        raise ValueError("invalid filter passband")
    return low_hz, high_hz


def _stroke_rate_lag_bounds(
    sample_count: int,
    *,
    sample_rate_hz: float = SAMPLE_RATE_HZ,
    min_stroke_rate_spm: float = MIN_STROKE_RATE_SPM,
    max_stroke_rate_spm: float = MAX_STROKE_RATE_SPM,
) -> tuple[int, int]:
    lag_min = max(1, math.ceil((sample_rate_hz * 60.0) / max_stroke_rate_spm))
    lag_max = min(
        sample_count - 1,
        math.floor((sample_rate_hz * 60.0) / min_stroke_rate_spm),
    )
    if lag_min > lag_max:
        raise ValueError("invalid lag bounds")
    return lag_min, lag_max


def _zero_phase_bandpass(
    values: list[float],
    *,
    sample_rate_hz: float = SAMPLE_RATE_HZ,
    min_stroke_rate_spm: float = MIN_STROKE_RATE_SPM,
    max_stroke_rate_spm: float = MAX_STROKE_RATE_SPM,
) -> list[float]:
    if sample_rate_hz <= 0.0:
        return []

    np = _get_numpy()
    scipy_signal = _get_scipy_signal()

    low_hz, high_hz = _stroke_rate_bounds_hz(
        sample_rate_hz=sample_rate_hz,
        min_stroke_rate_spm=min_stroke_rate_spm,
        max_stroke_rate_spm=max_stroke_rate_spm,
    )
    centered = np.asarray(values, dtype=float) - float(np.mean(values))
    b_coefficients, a_coefficients = scipy_signal.butter(
        BUTTERWORTH_ORDER,
        [low_hz, high_hz],
        btype="bandpass",
        fs=sample_rate_hz,
    )
    padlen = 3 * max(len(a_coefficients), len(b_coefficients))
    if len(centered) <= padlen:
        return []
    return scipy_signal.filtfilt(b_coefficients, a_coefficients, centered).tolist()


def _yin_period_candidate(
    values: list[float],
    *,
    sample_rate_hz: float = SAMPLE_RATE_HZ,
    min_stroke_rate_spm: float = MIN_STROKE_RATE_SPM,
    max_stroke_rate_spm: float = MAX_STROKE_RATE_SPM,
    threshold: float = YIN_THRESHOLD,
) -> int | None:
    if len(values) < 2:
        return None

    np = _get_numpy()

    samples = np.asarray(values, dtype=float)
    min_lag = max(1, math.ceil((sample_rate_hz * 60.0) / max_stroke_rate_spm))
    if len(samples) <= min_lag or not np.isfinite(samples).all():
        return None
    if float(np.ptp(samples)) <= NUMERICAL_EPSILON:
        return None

    min_lag, max_lag = _stroke_rate_lag_bounds(
        len(values),
        sample_rate_hz=sample_rate_hz,
        min_stroke_rate_spm=min_stroke_rate_spm,
        max_stroke_rate_spm=max_stroke_rate_spm,
    )
    difference = np.zeros(max_lag + 1, dtype=float)

    for lag in range(1, max_lag + 1):
        delta = samples[:-lag] - samples[lag:]
        difference[lag] = float(np.dot(delta, delta))

    cmndf = np.ones(max_lag + 1, dtype=float)
    running_sum = 0.0
    for lag in range(1, max_lag + 1):
        running_sum += difference[lag]
        cmndf[lag] = difference[lag] * lag / max(running_sum, NUMERICAL_EPSILON)

    for lag in range(max(min_lag, 2), max_lag):
        if (
            cmndf[lag] < threshold
            and cmndf[lag] <= cmndf[lag - 1]
            and cmndf[lag] <= cmndf[lag + 1]
        ):
            return lag

    best_lag = min(range(min_lag, max_lag + 1), key=lambda lag_value: cmndf[lag_value])
    if float(cmndf[best_lag]) > YIN_FALLBACK_MAX_CMNDF:
        return None
    return int(best_lag)


def _cepstrum_period_candidate(
    values: list[float],
    *,
    sample_rate_hz: float = SAMPLE_RATE_HZ,
    min_stroke_rate_spm: float = MIN_STROKE_RATE_SPM,
    max_stroke_rate_spm: float = MAX_STROKE_RATE_SPM,
) -> int | None:
    if len(values) < 2:
        return None

    np = _get_numpy()

    samples = np.asarray(values, dtype=float)
    min_lag = max(1, math.ceil((sample_rate_hz * 60.0) / max_stroke_rate_spm))
    if len(samples) <= min_lag or not np.isfinite(samples).all():
        return None
    if float(np.ptp(samples)) <= NUMERICAL_EPSILON:
        return None

    min_lag, max_lag = _stroke_rate_lag_bounds(
        len(values),
        sample_rate_hz=sample_rate_hz,
        min_stroke_rate_spm=min_stroke_rate_spm,
        max_stroke_rate_spm=max_stroke_rate_spm,
    )
    spectrum = np.fft.rfft(samples)
    log_magnitude = np.log(np.maximum(np.abs(spectrum), NUMERICAL_EPSILON))
    cepstrum = np.fft.irfft(log_magnitude, n=len(values))
    search = cepstrum[min_lag : max_lag + 1]
    if search.size == 0 or not np.isfinite(search).all():
        return None
    return int(min_lag + int(np.argmax(search)))


def _consensus_period_band(
    yin_period: int | None,
    cepstrum_period: int | None,
    *,
    min_lag: int,
    max_lag: int,
    tolerance_fraction: float = CONSENSUS_TOLERANCE_FRACTION,
) -> tuple[int, int] | None:
    candidates = [
        period
        for period in (yin_period, cepstrum_period)
        if period is not None and min_lag <= period <= max_lag
    ]
    if not candidates:
        return None
    if len(candidates) == 1:
        period = candidates[0]
        return (
            max(min_lag, period - CONSENSUS_MARGIN_SAMPLES),
            min(max_lag, period + CONSENSUS_MARGIN_SAMPLES),
        )

    lower = min(candidates)
    upper = max(candidates)
    tolerance_samples = max(1, math.ceil(lower * tolerance_fraction))
    if upper - lower <= tolerance_samples:
        return (
            max(min_lag, lower - CONSENSUS_MARGIN_SAMPLES),
            min(max_lag, upper + CONSENSUS_MARGIN_SAMPLES),
        )
    return (lower, upper)


def _music_frequency_hz(
    values: list[float],
    *,
    sample_rate_hz: float,
    low_frequency_hz: float,
    high_frequency_hz: float,
    min_peak_prominence_ratio: float = MUSIC_MIN_PEAK_PROMINENCE_RATIO,
    grid_size: int = MUSIC_GRID_SIZE,
) -> float | None:
    np = _get_numpy()

    samples = np.asarray(values, dtype=float)
    if (
        samples.size < 32
        or not np.isfinite(samples).all()
        or float(np.ptp(samples)) <= NUMERICAL_EPSILON
        or not math.isfinite(sample_rate_hz)
        or not math.isfinite(low_frequency_hz)
        or not math.isfinite(high_frequency_hz)
        or not math.isfinite(min_peak_prominence_ratio)
        or sample_rate_hz <= 0.0
        or low_frequency_hz <= 0.0
        or high_frequency_hz <= low_frequency_hz
        or min_peak_prominence_ratio <= 0.0
        or grid_size <= 0
    ):
        return None

    snapshot_length = min(MUSIC_SNAPSHOT_LENGTH, samples.size // 2)
    trajectory = np.lib.stride_tricks.sliding_window_view(samples, snapshot_length).T
    column_count = trajectory.shape[1]
    if column_count <= 1:
        return None

    covariance = (trajectory @ trajectory.T) / float(column_count)
    eigenvalues, eigenvectors = np.linalg.eigh(covariance)
    if not np.isfinite(eigenvalues).all():
        return None

    if eigenvectors.shape[1] <= 2:
        return None

    noise_subspace = eigenvectors[:, :-2]
    sample_index = np.arange(snapshot_length, dtype=float)
    frequency_grid_hz = np.linspace(low_frequency_hz, high_frequency_hz, grid_size)
    steering_matrix = np.exp(
        (-2.0j * np.pi / sample_rate_hz)
        * np.outer(frequency_grid_hz, sample_index)
    )
    projections = steering_matrix @ noise_subspace.conj()
    denominators = np.sum(np.abs(projections) ** 2, axis=1)
    pseudospectrum = 1.0 / np.maximum(denominators, NUMERICAL_EPSILON)

    if not np.isfinite(pseudospectrum).all():
        return None

    peak = float(np.max(pseudospectrum))
    median = float(np.median(pseudospectrum))
    if median <= 0.0 or (
        peak / max(median, NUMERICAL_EPSILON)
    ) < min_peak_prominence_ratio:
        return None

    return float(frequency_grid_hz[int(np.argmax(pseudospectrum))])


def magnitude_series(series: dict[str, list[float]]) -> list[float]:
    x_values = series["x"]
    y_values = series["y"]
    z_values = series["z"]
    return [
        math.sqrt((x_value * x_value) + (y_value * y_value) + (z_value * z_value))
        for x_value, y_value, z_value in zip(x_values, y_values, z_values)
    ]


def _pearson_autocorrelation(values: list[float], lag: int) -> float:
    if lag <= 0 or lag >= len(values):
        return 0.0

    left = values[:-lag]
    right = values[lag:]
    if not left or not right:
        return 0.0

    left_mean = sum(left) / len(left)
    right_mean = sum(right) / len(right)
    numerator = 0.0
    left_energy = 0.0
    right_energy = 0.0

    for left_value, right_value in zip(left, right):
        centered_left = left_value - left_mean
        centered_right = right_value - right_mean
        numerator += centered_left * centered_right
        left_energy += centered_left * centered_left
        right_energy += centered_right * centered_right

    if left_energy == 0.0 or right_energy == 0.0:
        return 0.0

    return numerator / math.sqrt(left_energy * right_energy)


def _select_peak_lag(scores: list[tuple[int, float]]) -> int:
    if not scores:
        return 0

    peak_candidates: list[tuple[int, float]] = []
    for index in range(1, len(scores) - 1):
        previous_score = scores[index - 1][1]
        current_lag, current_score = scores[index]
        next_score = scores[index + 1][1]
        if current_score >= previous_score and current_score > next_score:
            peak_candidates.append((current_lag, current_score))

    if peak_candidates:
        strongest_peak_score = max(score for _lag, score in peak_candidates)
        minimum_accepted_score = strongest_peak_score * PEAK_SCORE_FRACTION
        for lag, score in peak_candidates:
            if score >= minimum_accepted_score:
                return lag

    return max(scores, key=lambda item: item[1])[0]


def estimate_autocorrelation_stroke_rate(
    values: list[float],
    *,
    sample_rate_hz: float = SAMPLE_RATE_HZ,
    min_stroke_rate_spm: float = MIN_STROKE_RATE_SPM,
    max_stroke_rate_spm: float = MAX_STROKE_RATE_SPM,
) -> float:
    if len(values) < 2 or sample_rate_hz <= 0.0:
        return 0.0

    lag_min = max(1, math.ceil((sample_rate_hz * 60.0) / max_stroke_rate_spm))
    lag_max = min(
        len(values) - 1,
        math.floor((sample_rate_hz * 60.0) / min_stroke_rate_spm),
    )

    if lag_min > lag_max:
        return 0.0

    scores = [
        (lag, _pearson_autocorrelation(values, lag))
        for lag in range(lag_min, lag_max + 1)
    ]
    best_lag = _select_peak_lag(scores)
    best_score = next((score for lag, score in scores if lag == best_lag), float("-inf"))

    if best_lag == 0 or best_score <= 0.0:
        return 0.0

    return (sample_rate_hz * 60.0) / best_lag


def estimate_consensus_music_stroke_rate(
    values: list[float],
    *,
    sample_rate_hz: float = SAMPLE_RATE_HZ,
    min_stroke_rate_spm: float = MIN_STROKE_RATE_SPM,
    max_stroke_rate_spm: float = MAX_STROKE_RATE_SPM,
) -> float:
    try:
        if not all(
            math.isfinite(parameter)
            for parameter in (
                sample_rate_hz,
                min_stroke_rate_spm,
                max_stroke_rate_spm,
            )
        ):
            return 0.0
    except TypeError:
        return 0.0

    if (
        len(values) != SAMPLE_STORE_CAPACITY
        or sample_rate_hz <= 0.0
        or min_stroke_rate_spm <= 0.0
        or max_stroke_rate_spm <= 0.0
        or min_stroke_rate_spm > max_stroke_rate_spm
    ):
        return 0.0

    try:
        filtered = _zero_phase_bandpass(
            values,
            sample_rate_hz=sample_rate_hz,
            min_stroke_rate_spm=min_stroke_rate_spm,
            max_stroke_rate_spm=max_stroke_rate_spm,
        )
    except (OverflowError, TypeError, ValueError):
        return 0.0
    if len(filtered) != len(values):
        return 0.0

    try:
        min_lag, max_lag = _stroke_rate_lag_bounds(
            len(filtered),
            sample_rate_hz=sample_rate_hz,
            min_stroke_rate_spm=min_stroke_rate_spm,
            max_stroke_rate_spm=max_stroke_rate_spm,
        )
        yin_period = _yin_period_candidate(
            filtered,
            sample_rate_hz=sample_rate_hz,
            min_stroke_rate_spm=min_stroke_rate_spm,
            max_stroke_rate_spm=max_stroke_rate_spm,
        )
        cepstrum_period = _cepstrum_period_candidate(
            filtered,
            sample_rate_hz=sample_rate_hz,
            min_stroke_rate_spm=min_stroke_rate_spm,
            max_stroke_rate_spm=max_stroke_rate_spm,
        )
    except (OverflowError, TypeError, ValueError):
        return 0.0
    candidate_count = sum(
        1
        for period in (yin_period, cepstrum_period)
        if period is not None and min_lag <= period <= max_lag
    )
    period_band = _consensus_period_band(
        yin_period,
        cepstrum_period,
        min_lag=min_lag,
        max_lag=max_lag,
    )
    if period_band is None:
        return 0.0

    try:
        low_frequency_hz = sample_rate_hz / float(period_band[1])
        high_frequency_hz = sample_rate_hz / float(period_band[0])
        music_frequency_hz = _music_frequency_hz(
            filtered,
            sample_rate_hz=sample_rate_hz,
            low_frequency_hz=low_frequency_hz,
            high_frequency_hz=high_frequency_hz,
            min_peak_prominence_ratio=(
                MUSIC_SINGLE_CANDIDATE_MIN_PEAK_PROMINENCE_RATIO
                if candidate_count == 1
                else MUSIC_MIN_PEAK_PROMINENCE_RATIO
            ),
        )
    except Exception:
        return 0.0
    if music_frequency_hz is None:
        return 0.0

    stroke_rate_spm = music_frequency_hz * 60.0
    if stroke_rate_spm < min_stroke_rate_spm or stroke_rate_spm > max_stroke_rate_spm:
        return 0.0
    return stroke_rate_spm
