from __future__ import annotations

from analysis.scripts.blocks import BlockManifest, BlockResult


class FallbackSelectorBlock:
    manifest = BlockManifest(
        block_id="validation.fallback_selector",
        group="validation",
        language="py",
        entrypoint="analysis.algorithms.validation.py.fallback_selector:BLOCK",
        input_kinds=["candidate"],
        output_ports={"selected": "candidate"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        packets = list(input_packets.get("source", []))
        if not packets:
            return BlockResult(outputs={"selected": []})

        selected = max(
            packets,
            key=lambda packet: float("-inf") if packet.confidence is None else float(packet.confidence),
        )
        return BlockResult(outputs={"selected": [selected]})


BLOCK = FallbackSelectorBlock()
