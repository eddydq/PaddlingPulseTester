from analysis.scripts.block_contract import BlockResult, Packet
from analysis.scripts.block_manifest import BlockManifest
from analysis.scripts.pipeline_executor import PipelineExecutor


class _InlineBlock:
    manifest = BlockManifest(
        block_id="representation.inline",
        group="representation",
        language="py",
        entrypoint="inline:BLOCK",
        input_kinds=["raw_window"],
        output_ports={"primary": "series"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        packet = input_packets["source"][0]
        return BlockResult(outputs={"primary": [Packet(kind="series", data=packet.data)]})


def test_executor_routes_named_outputs_between_nodes():
    graph = {
        "nodes": [{"node_id": "n1", "block_id": "representation.inline", "params": {}}],
        "inputs": {"n1.source": "input.raw"},
        "outputs": {"final": "n1.primary"},
    }
    packet = Packet(kind="raw_window", data={"values": [1, 2, 3]})
    result = PipelineExecutor({"representation.inline": _InlineBlock()}).run(graph, {"input.raw": [packet]})
    assert result["final"][0].kind == "series"


def test_executor_rejects_kind_mismatch():
    graph = {
        "nodes": [{"node_id": "n1", "block_id": "representation.inline", "params": {}}],
        "inputs": {"n1.source": "input.raw"},
        "outputs": {"final": "n1.primary"},
    }
    packet = Packet(kind="candidate", data={"spm": 61.5})
    try:
        PipelineExecutor({"representation.inline": _InlineBlock()}).run(graph, {"input.raw": [packet]})
    except ValueError as exc:
        assert "kind" in str(exc)
    else:
        raise AssertionError("expected ValueError")
