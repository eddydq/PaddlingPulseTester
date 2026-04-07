from __future__ import annotations

from analysis.scripts.block_contract import BlockResult
from analysis.scripts.block_manifest import BlockManifest


class SpmRangeGateBlock:
    manifest = BlockManifest(
        block_id="validation.spm_range_gate",
        group="validation",
        language="py",
        entrypoint="analysis.algorithms.validation.py.spm_range_gate:BLOCK",
        input_kinds=["candidate"],
        output_ports={"accepted": "candidate", "rejected": "candidate"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        spm = float(source.data.get("spm", 0.0))
        if float(params.get("min_spm", 20.0)) <= spm <= float(params.get("max_spm", 120.0)):
            return BlockResult(outputs={"accepted": [source], "rejected": []})
        return BlockResult(outputs={"accepted": [], "rejected": [source]})

BLOCK = SpmRangeGateBlock()