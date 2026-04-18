from __future__ import annotations

import numpy as np
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest


class VectorMagnitudeBlock:
    manifest = BlockManifest(
        block_id="representation.vector_magnitude",
        group="representation",
        language="py",
        entrypoint="analysis.algorithms.representation.py.vector_magnitude:BLOCK",
        input_kinds=["raw_window"],
        output_ports={"primary": "series"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        s = source.data["series"]
        x, y, z = np.array(s["x"]), np.array(s["y"]), np.array(s["z"])
        mag = np.sqrt(x**2 + y**2 + z**2).tolist()
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": mag}, axis="magnitude", sample_rate_hz=source.sample_rate_hz)]})


BLOCK = VectorMagnitudeBlock()
