from __future__ import annotations
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class WindowTrimBlock:
    manifest = BlockManifest(
        block_id="pretraitement.window_trim",
        group="pretraitement",
        language="py",
        entrypoint="analysis.algorithms.pretraitement.py.window_trim:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "series"},
        stateful=False,
        params_schema={
            "keep_samples": {"type": "int", "default": 256, "min": 1, "description": "Number of samples to keep"},
            "anchor": {"type": "str", "default": "end", "enum": ["start", "end"], "description": "Keep from start or end"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = list(source.data["values"])
        n = int(params.get("keep_samples", 256))
        anchor = params.get("anchor", "end")
        if anchor == "end":
            trimmed = values[-n:]
        else:
            trimmed = values[:n]
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": trimmed}, axis=source.axis, sample_rate_hz=source.sample_rate_hz)]})

BLOCK = WindowTrimBlock()
