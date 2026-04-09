from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class ZeroCrossingDetectBlock:
    manifest = BlockManifest(
        block_id="detection.zero_crossing_detect", group="detection", language="py",
        entrypoint="analysis.algorithms.detection.py.zero_crossing_detect:BLOCK",
        input_kinds=["series"], output_ports={"primary": "candidate"}, stateful=False,
        params_schema={
            "dead_samples": {"type": "int", "default": 5, "min": 0, "description": "Dead zone after each crossing"},
        },
    )
    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = list(source.data["values"])
        sr = source.sample_rate_hz or 52.0
        dead = int(params.get("dead_samples", 5))
        count = 0
        i = 1
        while i < len(values):
            if values[i - 1] < 0 and values[i] >= 0:
                count += 1
                i += dead
            i += 1
        window_s = len(values) / sr
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"crossings": count, "window_seconds": window_s}, axis=source.axis, sample_rate_hz=sr)]})
BLOCK = ZeroCrossingDetectBlock()
