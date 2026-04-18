from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class IntervalToSpmBlock:
    manifest = BlockManifest(
        block_id="estimation.interval_to_spm", group="estimation", language="py",
        entrypoint="analysis.algorithms.estimation.py.interval_to_spm:BLOCK",
        input_kinds=["candidate"], output_ports={"primary": "candidate"}, stateful=False,
    )
    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        intervals = source.data.get("intervals", [])
        if not intervals:
            return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": 0.0}, sample_rate_hz=source.sample_rate_hz)]})
        mean_interval = sum(intervals) / len(intervals)
        spm = 60.0 / mean_interval if mean_interval > 0 else 0.0
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, sample_rate_hz=source.sample_rate_hz)]})
BLOCK = IntervalToSpmBlock()
