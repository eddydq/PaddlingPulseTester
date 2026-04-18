from __future__ import annotations

import importlib
import math

SAMPLE_RATE_HZ = 52.0
HPF_DEFAULT_CUTOFF_HZ = 0.25
LPF_DEFAULT_CUTOFF_HZ = 3.0
BANDPASS_DEFAULT_LOW_HZ = 0.25
BANDPASS_DEFAULT_HIGH_HZ = 2.5
BUTTERWORTH_ORDER = 2
ENVELOPE_DEFAULT_DECAY = 0.95

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


def vector_magnitude(series: dict[str, list[float]]) -> list[float]:
    x_values = series["x"]
    y_values = series["y"]
    z_values = series["z"]
    return [
        math.sqrt(x * x + y * y + z * z)
        for x, y, z in zip(x_values, y_values, z_values)
    ]


def remove_gravity_hpf(
    values: list[float],
    *,
    cutoff_hz: float = HPF_DEFAULT_CUTOFF_HZ,
    sample_rate_hz: float = SAMPLE_RATE_HZ,
) -> list[float]:
    np = _get_numpy()
    scipy_signal = _get_scipy_signal()

    nyquist = sample_rate_hz / 2.0
    normalized = cutoff_hz / nyquist
    if normalized <= 0.0 or normalized >= 1.0:
        return list(values)

    b, a = scipy_signal.butter(BUTTERWORTH_ORDER, normalized, btype="highpass")
    data = np.asarray(values, dtype=float)
    padlen = 3 * max(len(a), len(b))
    if len(data) <= padlen:
        return list(values)
    return scipy_signal.filtfilt(b, a, data).tolist()


def lowpass_filter(
    values: list[float],
    *,
    cutoff_hz: float = LPF_DEFAULT_CUTOFF_HZ,
    sample_rate_hz: float = SAMPLE_RATE_HZ,
) -> list[float]:
    np = _get_numpy()
    scipy_signal = _get_scipy_signal()

    nyquist = sample_rate_hz / 2.0
    normalized = cutoff_hz / nyquist
    if normalized <= 0.0 or normalized >= 1.0:
        return list(values)

    b, a = scipy_signal.butter(BUTTERWORTH_ORDER, normalized, btype="lowpass")
    data = np.asarray(values, dtype=float)
    padlen = 3 * max(len(a), len(b))
    if len(data) <= padlen:
        return list(values)
    return scipy_signal.filtfilt(b, a, data).tolist()


def bandpass_filter(
    values: list[float],
    *,
    low_hz: float = BANDPASS_DEFAULT_LOW_HZ,
    high_hz: float = BANDPASS_DEFAULT_HIGH_HZ,
    sample_rate_hz: float = SAMPLE_RATE_HZ,
) -> list[float]:
    np = _get_numpy()
    scipy_signal = _get_scipy_signal()

    nyquist = sample_rate_hz / 2.0
    low_norm = low_hz / nyquist
    high_norm = high_hz / nyquist
    if low_norm <= 0.0 or high_norm >= 1.0 or low_norm >= high_norm:
        return list(values)

    b, a = scipy_signal.butter(4, [low_norm, high_norm], btype="bandpass")
    data = np.asarray(values, dtype=float)
    padlen = 3 * max(len(a), len(b))
    if len(data) <= padlen:
        return list(values)
    return scipy_signal.filtfilt(b, a, data).tolist()


def adaptive_envelope(
    values: list[float],
    *,
    decay: float = ENVELOPE_DEFAULT_DECAY,
) -> tuple[list[float], list[float]]:
    if not values:
        return [], []

    env_max = [values[0]]
    env_min = [values[0]]
    for i in range(1, len(values)):
        v = values[i]
        env_max.append(max(v, env_max[-1] * decay + v * (1.0 - decay)))
        env_min.append(min(v, env_min[-1] * decay + v * (1.0 - decay)))
    return env_max, env_min


def kalman_2d_smooth(
    measurements: list[float],
    *,
    Q: float = 4.0,
    R: float = 2.0,
) -> list[float]:
    if not measurements:
        return []

    state_rate = 0.0
    state_accel = 0.0
    P = [[1000.0, 0.0], [0.0, 1000.0]]
    dt = 1.0
    results = []

    for meas in measurements:
        pred_rate = state_rate + dt * state_accel
        pred_accel = state_accel
        P_pred = [
            [P[0][0] + dt * P[1][0] + dt * P[0][1] + dt * dt * P[1][1] + Q, P[0][1] + dt * P[1][1]],
            [P[1][0] + dt * P[1][1], P[1][1] + Q],
        ]

        if meas > 0.0:
            S = P_pred[0][0] + R
            if S > 0.0:
                K0 = P_pred[0][0] / S
                K1 = P_pred[1][0] / S
                innovation = meas - pred_rate
                state_rate = pred_rate + K0 * innovation
                state_accel = pred_accel + K1 * innovation
                P[0][0] = (1.0 - K0) * P_pred[0][0]
                P[0][1] = (1.0 - K0) * P_pred[0][1]
                P[1][0] = P_pred[1][0] - K1 * P_pred[0][0]
                P[1][1] = P_pred[1][1] - K1 * P_pred[0][1]
            else:
                state_rate = pred_rate
                state_accel = pred_accel
                P = P_pred
        else:
            state_rate = pred_rate
            state_accel = pred_accel
            P = P_pred

        results.append(max(0.0, state_rate))

    return results
