from __future__ import annotations
import numpy as np
from scipy.signal import butter, sosfiltfilt
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class ZeroPhaseBandpassBlock:
    manifest = BlockManifest(
        block_id="pretraitement.zero_phase_bandpass",
        group="pretraitement",
        language="py",
        entrypoint="analysis.algorithms.pretraitement.py.zero_phase_bandpass:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "series"},
        stateful=False,
        params_schema={
            "low_hz": {"type": "float", "default": 0.5, "min": 0.01, "description": "Lower cutoff frequency"},
            "high_hz": {"type": "float", "default": 5.0, "min": 0.02, "description": "Upper cutoff frequency"},
            "order": {"type": "int", "default": 4, "min": 1, "max": 10, "description": "Filter order"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        low = float(params.get("low_hz", 0.5))
        high = float(params.get("high_hz", 5.0))
        order = int(params.get("order", 4))
        sos = butter(order, [low, high], btype="band", fs=sr, output="sos")
        filtered = sosfiltfilt(sos, values).tolist()
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": filtered}, axis=source.axis, sample_rate_hz=sr)]})

BLOCK = ZeroPhaseBandpassBlock()
