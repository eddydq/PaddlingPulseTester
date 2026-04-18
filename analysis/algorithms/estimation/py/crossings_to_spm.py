from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class CrossingsToSpmBlock:
    manifest = BlockManifest(
        block_id="estimation.crossings_to_spm", group="estimation", language="py",
        entrypoint="analysis.algorithms.estimation.py.crossings_to_spm:BLOCK",
        input_kinds=["candidate"], output_ports={"primary": "candidate"}, stateful=False,
    )
    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        crossings = source.data.get("crossings", 0)
        window_s = source.data.get("window_seconds", 1.0)
        half_cycles_per_s = crossings / window_s if window_s > 0 else 0
        spm = half_cycles_per_s * 30.0
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, sample_rate_hz=source.sample_rate_hz)]})
BLOCK = CrossingsToSpmBlock()
