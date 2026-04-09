import numpy as np
from analysis.scripts.blocks import Packet

RAW_WINDOW = Packet(
    kind="raw_window",
    data={"series": {"x": [1.0, 2.0, 3.0], "y": [4.0, 5.0, 6.0], "z": [7.0, 8.0, 9.0]}},
    sample_rate_hz=52.0,
)

SERIES_PACKET = Packet(kind="series", data={"values": list(np.sin(np.linspace(0, 4 * np.pi, 256)))}, sample_rate_hz=52.0)

_SR = 52.0
_T = np.arange(512) / _SR
_SINE_1HZ = Packet(kind="series", data={"values": np.sin(2 * np.pi * 1.0 * _T).tolist()}, sample_rate_hz=_SR)


def test_select_x():
    from analysis.algorithms.representation.py.select_x import BLOCK
    result = BLOCK.run({"source": [RAW_WINDOW]}, {}, {})
    assert result.outputs["primary"][0].data["values"] == [1.0, 2.0, 3.0]
    assert result.outputs["primary"][0].kind == "series"

def test_select_y():
    from analysis.algorithms.representation.py.select_y import BLOCK
    result = BLOCK.run({"source": [RAW_WINDOW]}, {}, {})
    assert result.outputs["primary"][0].data["values"] == [4.0, 5.0, 6.0]

def test_select_z():
    from analysis.algorithms.representation.py.select_z import BLOCK
    result = BLOCK.run({"source": [RAW_WINDOW]}, {}, {})
    assert result.outputs["primary"][0].data["values"] == [7.0, 8.0, 9.0]

def test_vector_magnitude():
    from analysis.algorithms.representation.py.vector_magnitude import BLOCK
    result = BLOCK.run({"source": [RAW_WINDOW]}, {}, {})
    values = result.outputs["primary"][0].data["values"]
    expected_0 = (1.0**2 + 4.0**2 + 7.0**2) ** 0.5
    assert abs(values[0] - expected_0) < 1e-9

def test_hpf_gravity():
    from analysis.algorithms.pretraitement.py.hpf_gravity import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {"cutoff_hz": 0.5}, {})
    assert result.outputs["primary"][0].kind == "series"
    assert len(result.outputs["primary"][0].data["values"]) == 256


def test_lowpass():
    from analysis.algorithms.pretraitement.py.lowpass import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {"cutoff_hz": 5.0}, {})
    assert result.outputs["primary"][0].kind == "series"
    assert len(result.outputs["primary"][0].data["values"]) == 256


def test_bandpass():
    from analysis.algorithms.pretraitement.py.bandpass import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {}, {})
    assert result.outputs["primary"][0].kind == "series"


def test_zero_phase_bandpass():
    from analysis.algorithms.pretraitement.py.zero_phase_bandpass import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {}, {})
    assert result.outputs["primary"][0].kind == "series"


def test_wavelet_isolation():
    from analysis.algorithms.pretraitement.py.wavelet_isolation import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {}, {})
    assert result.outputs["primary"][0].kind == "series"


def test_window_trim_end():
    from analysis.algorithms.pretraitement.py.window_trim import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {"keep_samples": 64, "anchor": "end"}, {})
    assert len(result.outputs["primary"][0].data["values"]) == 64


def test_window_trim_start():
    from analysis.algorithms.pretraitement.py.window_trim import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {"keep_samples": 64, "anchor": "start"}, {})
    assert len(result.outputs["primary"][0].data["values"]) == 64


def test_fft_dominant():
    from analysis.algorithms.estimation.py.fft_dominant import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    spm = result.outputs["primary"][0].data["spm"]
    assert 55 < spm < 65

def test_yin_period():
    from analysis.algorithms.estimation.py.yin_period import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    spm = result.outputs["primary"][0].data["spm"]
    assert 55 < spm < 66

def test_cepstrum_period():
    from analysis.algorithms.estimation.py.cepstrum_period import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    spm = result.outputs["primary"][0].data["spm"]
    assert 55 < spm < 65

def test_music_refine():
    from analysis.algorithms.estimation.py.music_refine import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    spm = result.outputs["primary"][0].data["spm"]
    assert 55 < spm < 65

def test_hilbert_freq():
    from analysis.algorithms.estimation.py.hilbert_freq import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    spm = result.outputs["primary"][0].data["spm"]
    assert 55 < spm < 65

def test_interval_to_spm():
    from analysis.algorithms.estimation.py.interval_to_spm import BLOCK
    pkt = Packet(kind="candidate", data={"intervals": [1.0, 1.0, 1.0]}, sample_rate_hz=52.0)
    result = BLOCK.run({"source": [pkt]}, {}, {})
    assert abs(result.outputs["primary"][0].data["spm"] - 60.0) < 0.1

def test_crossings_to_spm():
    from analysis.algorithms.estimation.py.crossings_to_spm import BLOCK
    pkt = Packet(kind="candidate", data={"crossings": 10, "window_seconds": 5.0}, sample_rate_hz=52.0)
    result = BLOCK.run({"source": [pkt]}, {}, {})
    assert abs(result.outputs["primary"][0].data["spm"] - 60.0) < 0.1

def test_adaptive_envelope():
    from analysis.algorithms.detection.py.adaptive_envelope import BLOCK
    result = BLOCK.run({"source": [SERIES_PACKET]}, {}, {})
    assert result.outputs["primary"][0].kind == "series"
    values = result.outputs["primary"][0].data["values"]
    assert all(v >= 0 for v in values)

def test_adaptive_peak_detect():
    from analysis.algorithms.detection.py.adaptive_peak_detect import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {"min_distance_samples": 26, "prominence": 0.1}, {})
    pkt = result.outputs["primary"][0]
    assert pkt.kind == "candidate"
    assert "intervals" in pkt.data

def test_schmitt_trigger():
    from analysis.algorithms.detection.py.schmitt_trigger import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {"high_thresh": 0.3, "low_thresh": -0.3}, {})
    pkt = result.outputs["primary"][0]
    assert pkt.kind == "candidate"
    assert "intervals" in pkt.data

def test_zero_crossing_detect():
    from analysis.algorithms.detection.py.zero_crossing_detect import BLOCK
    result = BLOCK.run({"source": [_SINE_1HZ]}, {}, {})
    pkt = result.outputs["primary"][0]
    assert pkt.kind == "candidate"
    assert "crossings" in pkt.data

def test_peak_selector_last():
    from analysis.algorithms.detection.py.peak_selector import BLOCK
    pkt = Packet(kind="candidate", data={"intervals": [1.0, 1.1, 0.9, 1.0, 1.05]}, sample_rate_hz=52.0)
    result = BLOCK.run({"source": [pkt]}, {"count": 3, "strategy": "last"}, {})
    assert len(result.outputs["primary"][0].data["intervals"]) == 3


def test_interval_gate_accepts():
    from analysis.algorithms.validation.py.interval_gate import BLOCK

    pkt = Packet(kind="candidate", data={"intervals": [1.0]})
    result = BLOCK.run({"source": [pkt]}, {"min_s": 0.5, "max_s": 3.0}, {})
    assert len(result.outputs["accepted"]) == 1


def test_interval_gate_rejects():
    from analysis.algorithms.validation.py.interval_gate import BLOCK

    pkt = Packet(kind="candidate", data={"intervals": [0.1]})
    result = BLOCK.run({"source": [pkt]}, {"min_s": 0.5, "max_s": 3.0}, {})
    assert len(result.outputs["rejected"]) == 1


def test_consensus_band():
    from analysis.algorithms.validation.py.consensus_band import BLOCK

    pkts = [
        Packet(kind="candidate", data={"spm": 60.0}),
        Packet(kind="candidate", data={"spm": 62.0}),
        Packet(kind="candidate", data={"spm": 120.0}),
    ]
    result = BLOCK.run({"source": pkts}, {"tolerance_spm": 5.0}, {})
    assert len(result.outputs["accepted"]) == 2
    assert len(result.outputs["rejected"]) == 1


def test_harmonic_reject_rejects_double():
    from analysis.algorithms.validation.py.harmonic_reject import BLOCK

    pkt = Packet(kind="candidate", data={"spm": 120.0})
    result = BLOCK.run({"source": [pkt]}, {"fundamental_spm": 60.0, "tolerance_spm": 5.0}, {})
    assert len(result.outputs["rejected"]) == 1


def test_harmonic_reject_accepts_fundamental():
    from analysis.algorithms.validation.py.harmonic_reject import BLOCK

    pkt = Packet(kind="candidate", data={"spm": 60.0})
    result = BLOCK.run({"source": [pkt]}, {"fundamental_spm": 60.0, "tolerance_spm": 5.0}, {})
    assert len(result.outputs["accepted"]) == 1


def test_confidence_gate():
    from analysis.algorithms.validation.py.confidence_gate import BLOCK

    pkt_good = Packet(kind="candidate", data={"spm": 60.0}, confidence=0.8)
    pkt_bad = Packet(kind="candidate", data={"spm": 60.0}, confidence=0.2)
    r1 = BLOCK.run({"source": [pkt_good]}, {"min_confidence": 0.5}, {})
    r2 = BLOCK.run({"source": [pkt_bad]}, {"min_confidence": 0.5}, {})
    assert len(r1.outputs["accepted"]) == 1
    assert len(r2.outputs["rejected"]) == 1


def test_fallback_selector():
    from analysis.algorithms.validation.py.fallback_selector import BLOCK

    pkts = [
        Packet(kind="candidate", data={"spm": 60.0}, confidence=0.5),
        Packet(kind="candidate", data={"spm": 65.0}, confidence=0.9),
        Packet(kind="candidate", data={"spm": 70.0}, confidence=0.3),
    ]
    result = BLOCK.run({"source": pkts}, {}, {})
    assert result.outputs["selected"][0].data["spm"] == 65.0


def test_kalman_2d_smooths():
    from analysis.algorithms.suivi.py.kalman_2d import BLOCK

    state = {}
    results = []
    for spm in [60.0, 62.0, 58.0, 61.0]:
        pkt = Packet(kind="candidate", data={"spm": spm})
        result = BLOCK.run({"source": [pkt]}, {}, state)
        state = result.state
        results.append(result.outputs["primary"][0].data["spm"])
    assert all(isinstance(value, float) for value in results)
    assert results[-1] != 61.0


def test_confirmation_filter_requires_streak():
    from analysis.algorithms.suivi.py.confirmation_filter import BLOCK

    state = {}
    pkt = Packet(kind="estimate", data={"spm": 60.0})
    r1 = BLOCK.run({"source": [pkt]}, {"required_streak": 3}, state)
    assert r1.outputs["primary"] == []
    state = r1.state
    r2 = BLOCK.run({"source": [pkt]}, {"required_streak": 3}, state)
    assert r2.outputs["primary"] == []
    state = r2.state
    r3 = BLOCK.run({"source": [pkt]}, {"required_streak": 3}, state)
    assert len(r3.outputs["primary"]) == 1


def test_invalid_streak_reset():
    from analysis.algorithms.suivi.py.invalid_streak_reset import BLOCK

    state = {}
    empty = Packet(kind="estimate", data={"spm": 0.0})
    for _ in range(4):
        result = BLOCK.run({"source": [empty]}, {"max_invalid": 5}, state)
        state = result.state
    assert state.get("invalid_count", 0) == 4
    assert state.get("reset", False) is False
    result = BLOCK.run({"source": [empty]}, {"max_invalid": 5}, state)
    assert result.state.get("reset", False) is True
