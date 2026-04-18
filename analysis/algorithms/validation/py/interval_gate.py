from __future__ import annotations

from analysis.scripts.blocks import BlockManifest, BlockResult


class IntervalGateBlock:
    manifest = BlockManifest(
        block_id="validation.interval_gate",
        group="validation",
        language="py",
        entrypoint="analysis.algorithms.validation.py.interval_gate:BLOCK",
        input_kinds=["candidate"],
        output_ports={"accepted": "candidate", "rejected": "candidate"},
        stateful=False,
        params_schema={
            "min_s": {"type": "float", "default": 0.5, "min": 0.0, "description": "Minimum mean interval in seconds"},
            "max_s": {"type": "float", "default": 3.0, "min": 0.0, "description": "Maximum mean interval in seconds"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        intervals = [float(value) for value in source.data.get("intervals", [])]
        mean_interval = sum(intervals) / len(intervals) if intervals else 0.0
        min_s = float(params.get("min_s", 0.5))
        max_s = float(params.get("max_s", 3.0))
        if intervals and min_s <= mean_interval <= max_s:
            return BlockResult(outputs={"accepted": [source], "rejected": []})
        return BlockResult(outputs={"accepted": [], "rejected": [source]})


BLOCK = IntervalGateBlock()
