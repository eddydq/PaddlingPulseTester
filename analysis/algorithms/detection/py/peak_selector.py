from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class PeakSelectorBlock:
    manifest = BlockManifest(
        block_id="detection.peak_selector", group="detection", language="py",
        entrypoint="analysis.algorithms.detection.py.peak_selector:BLOCK",
        input_kinds=["candidate"], output_ports={"primary": "candidate"}, stateful=False,
        params_schema={
            "count": {"type": "int", "default": 3, "min": 1, "description": "Number of intervals to keep"},
            "strategy": {"type": "str", "default": "last", "enum": ["last", "first"], "description": "Which intervals to keep"},
        },
    )
    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        intervals = list(source.data.get("intervals", []))
        n = int(params.get("count", 3))
        strategy = params.get("strategy", "last")
        trimmed = intervals[-n:] if strategy == "last" else intervals[:n]
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"intervals": trimmed}, sample_rate_hz=source.sample_rate_hz)]})
BLOCK = PeakSelectorBlock()
