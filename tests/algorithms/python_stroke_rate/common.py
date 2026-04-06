from __future__ import annotations

import math

SAMPLE_STORE_CAPACITY = 512
SAMPLE_RATE_HZ = 52.0
MIN_STROKE_RATE_SPM = 20.0
MAX_STROKE_RATE_SPM = 120.0
PEAK_SCORE_FRACTION = 0.8
BUTTERWORTH_ORDER = 4
FILTER_MARGIN_HZ = 0.10


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

    import numpy as np
    from scipy.signal import butter, filtfilt

    low_hz, high_hz = _stroke_rate_bounds_hz(
        sample_rate_hz=sample_rate_hz,
        min_stroke_rate_spm=min_stroke_rate_spm,
        max_stroke_rate_spm=max_stroke_rate_spm,
    )
    centered = np.asarray(values, dtype=float) - float(np.mean(values))
    b_coefficients, a_coefficients = butter(
        BUTTERWORTH_ORDER,
        [low_hz, high_hz],
        btype="bandpass",
        fs=sample_rate_hz,
    )
    padlen = 3 * max(len(a_coefficients), len(b_coefficients))
    if len(centered) <= padlen:
        return []
    return filtfilt(b_coefficients, a_coefficients, centered).tolist()


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
