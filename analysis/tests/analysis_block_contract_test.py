from analysis.scripts.blocks import Packet, BlockResult
from analysis.scripts.blocks import BlockManifest


def test_packet_round_trips_through_dict():
    packet = Packet(
        kind="series",
        data={"values": [1.0, 2.0, 3.0]},
        sample_rate_hz=52.0,
        window_id="w-001",
        axis="y",
        confidence=0.75,
        source_block="representation.select_axis",
    )
    assert Packet.from_dict(packet.to_dict()) == packet


def test_manifest_rejects_unknown_group():
    try:
        BlockManifest(
            block_id="bad.group",
            group="bad",
            language="py",
            entrypoint="module:BLOCK",
            input_kinds=["series"],
            output_ports={"primary": "candidate"},
            stateful=False,
        )
    except ValueError as exc:
        assert "group" in str(exc)
    else:
        raise AssertionError("expected ValueError")


def test_block_result_allows_multiple_named_outputs():
    result = BlockResult(outputs={"accepted": [], "rejected": []}, state={"armed": True})
    assert set(result.outputs) == {"accepted", "rejected"}
