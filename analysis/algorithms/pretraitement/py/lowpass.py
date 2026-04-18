from __future__ import annotations
import numpy as np
from scipy.signal import butter, sosfilt
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class LowpassBlock:
    manifest = BlockManifest(
        block_id="pretraitement.lowpass",
        group="pretraitement",
        language="py",
        entrypoint="analysis.algorithms.pretraitement.py.lowpass:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "series"},
        stateful=False,
        params_schema={
            "cutoff_hz": {"type": "float", "default": 5.0, "min": 0.01, "description": "Low-pass cutoff frequency"},
            "order": {"type": "int", "default": 4, "min": 1, "max": 10, "description": "Filter order"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        cutoff = float(params.get("cutoff_hz", 5.0))
        order = int(params.get("order", 4))
        sos = butter(order, cutoff, btype="low", fs=sr, output="sos")
        filtered = sosfilt(sos, values).tolist()
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": filtered}, axis=source.axis, sample_rate_hz=sr)]})

BLOCK = LowpassBlock()
