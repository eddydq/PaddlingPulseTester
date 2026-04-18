from analysis.scripts.blocks import BlockResult, Packet
from analysis.scripts.blocks import BlockManifest
from analysis.scripts.blocks import PipelineExecutor


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
    result, _ = PipelineExecutor({"representation.inline": _InlineBlock()}).run(graph, {"input.raw": [packet]})
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


def test_executor_returns_timing_diagnostics():
    graph = {
        "nodes": [{"node_id": "n1", "block_id": "representation.inline", "params": {}}],
        "inputs": {"n1.source": "input.raw"},
        "outputs": {"final": "n1.primary"},
    }
    packet = Packet(kind="raw_window", data={"values": [1, 2, 3]})
    result, diagnostics = PipelineExecutor({"representation.inline": _InlineBlock()}).run(graph, {"input.raw": [packet]})
    assert result["final"][0].kind == "series"
    assert "n1" in diagnostics["node_timings"]
    assert diagnostics["node_timings"]["n1"]["elapsed_ms"] >= 0
    assert diagnostics["total_elapsed_ms"] >= 0


class _StatefulCounter:
    manifest = BlockManifest(
        block_id="estimation.counter",
        group="estimation",
        language="py",
        entrypoint="inline:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "candidate"},
        stateful=True,
    )

    def run(self, input_packets, params, state):
        count = state.get("count", 0) + 1
        return BlockResult(
            outputs={"primary": [Packet(kind="candidate", data={"count": count})]},
            state={"count": count},
        )


def test_executor_propagates_state_across_runs():
    blocks = {"estimation.counter": _StatefulCounter()}
    executor = PipelineExecutor(blocks)
    graph = {
        "nodes": [{"node_id": "n1", "block_id": "estimation.counter", "params": {}}],
        "inputs": {"n1.source": "input.data"},
        "outputs": {"final": "n1.primary"},
    }
    packet = Packet(kind="series", data={"values": [1.0]})
    r1, _ = executor.run(graph, {"input.data": [packet]})
    r2, _ = executor.run(graph, {"input.data": [packet]})
    assert r1["final"][0].data["count"] == 1
    assert r2["final"][0].data["count"] == 2
