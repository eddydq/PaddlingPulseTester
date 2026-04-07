from __future__ import annotations


class PipelineExecutor:
    def __init__(self, blocks: dict[str, object]):
        self.blocks = blocks

    def run(self, graph: dict[str, object], inputs: dict[str, list[object]]):
        node_outputs: dict[str, dict[str, list[object]]] = {}
        for node in graph["nodes"]:
            block = self.blocks[node["block_id"]]
            bound_inputs = {}
            for target, source in graph.get("inputs", {}).items():
                node_id, port = target.split(".", 1)
                if node_id == node["node_id"]:
                    packets = inputs[source]
                    for packet in packets:
                        if packet.kind not in block.manifest.input_kinds:
                            raise ValueError(f"packet kind mismatch for {block.manifest.block_id}")
                    bound_inputs[port] = packets
            node_outputs[node["node_id"]] = block.run(bound_inputs, node.get("params", {}), {}).outputs

        exported = {}
        for name, source in graph["outputs"].items():
            node_id, port = source.split(".", 1)
            exported[name] = node_outputs[node_id][port]
        return exported
