from analysis.scripts.blocks import Packet
from analysis.algorithms.representation.py.select_axis import BLOCK as SELECT_AXIS
from analysis.algorithms.estimation.py.autocorrelation import BLOCK as AUTOCORR
from analysis.algorithms.validation.py.spm_range_gate import BLOCK as RANGE_GATE


def test_select_axis_returns_series_packet():
    raw = Packet(kind="raw_window", data={"series": {"y": [1.0, 2.0, 3.0]}})
    result = SELECT_AXIS.run({"source": [raw]}, {"axis": "y"}, {})
    assert result.outputs["primary"][0].kind == "series"


def test_autocorrelation_emits_candidate_packet():
    packet = Packet(kind="series", data={"values": [0.0] * 512}, sample_rate_hz=52.0, axis="y")
    result = AUTOCORR.run({"source": [packet]}, {}, {})
    assert result.outputs["primary"][0].kind == "candidate"


def test_range_gate_routes_invalid_candidate_to_rejected():
    packet = Packet(kind="candidate", data={"spm": 160.0})
    result = RANGE_GATE.run({"source": [packet]}, {"min_spm": 20.0, "max_spm": 120.0}, {})
    assert result.outputs["accepted"] == []
    assert result.outputs["rejected"][0].data["spm"] == 160.0
