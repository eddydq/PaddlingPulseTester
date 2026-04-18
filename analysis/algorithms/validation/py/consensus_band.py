from __future__ import annotations

from statistics import median

from analysis.scripts.blocks import BlockManifest, BlockResult


class ConsensusBandBlock:
    manifest = BlockManifest(
        block_id="validation.consensus_band",
        group="validation",
        language="py",
        entrypoint="analysis.algorithms.validation.py.consensus_band:BLOCK",
        input_kinds=["candidate"],
        output_ports={"accepted": "candidate", "rejected": "candidate"},
        stateful=False,
        params_schema={
            "tolerance_spm": {"type": "float", "default": 5.0, "min": 0.0, "description": "Max deviation from median SPM"},
        },
    )

    def run(self, input_packets, params, state):
        packets = list(input_packets.get("source", []))
        if not packets:
            return BlockResult(outputs={"accepted": [], "rejected": []})

        tolerance = float(params.get("tolerance_spm", 5.0))
        target = float(median(float(packet.data.get("spm", 0.0)) for packet in packets))
        accepted = []
        rejected = []
        for packet in packets:
            spm = float(packet.data.get("spm", 0.0))
            if abs(spm - target) <= tolerance:
                accepted.append(packet)
            else:
                rejected.append(packet)
        return BlockResult(outputs={"accepted": accepted, "rejected": rejected})


BLOCK = ConsensusBandBlock()
