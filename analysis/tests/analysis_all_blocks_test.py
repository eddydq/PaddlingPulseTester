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
