from __future__ import annotations

from analysis.scripts.blocks import BlockManifest, BlockResult


class ConfidenceGateBlock:
    manifest = BlockManifest(
        block_id="validation.confidence_gate",
        group="validation",
        language="py",
        entrypoint="analysis.algorithms.validation.py.confidence_gate:BLOCK",
        input_kinds=["candidate"],
        output_ports={"accepted": "candidate", "rejected": "candidate"},
        stateful=False,
        params_schema={
            "min_confidence": {"type": "float", "default": 0.5, "min": 0.0, "description": "Minimum confidence to accept"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        confidence = 0.0 if source.confidence is None else float(source.confidence)
        min_confidence = float(params.get("min_confidence", 0.5))
        if confidence >= min_confidence:
            return BlockResult(outputs={"accepted": [source], "rejected": []})
        return BlockResult(outputs={"accepted": [], "rejected": [source]})


BLOCK = ConfidenceGateBlock()
