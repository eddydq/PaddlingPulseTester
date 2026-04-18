from __future__ import annotations
import numpy as np
from scipy.signal import hilbert, butter, sosfilt
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class AdaptiveEnvelopeBlock:
    manifest = BlockManifest(
        block_id="detection.adaptive_envelope", group="detection", language="py",
        entrypoint="analysis.algorithms.detection.py.adaptive_envelope:BLOCK",
        input_kinds=["series"], output_ports={"primary": "series"}, stateful=False,
        params_schema={
            "smoothing_hz": {"type": "float", "default": 1.0, "min": 0.01, "description": "Envelope smoothing cutoff"},
            "order": {"type": "int", "default": 4, "min": 1, "max": 10, "description": "Filter order"},
        },
    )
    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        envelope = np.abs(hilbert(values))
        smoothing = float(params.get("smoothing_hz", 1.0))
        order = int(params.get("order", 4))
        sos = butter(order, smoothing, btype="low", fs=sr, output="sos")
        smoothed = sosfilt(sos, envelope).tolist()
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": smoothed}, axis=source.axis, sample_rate_hz=sr)]})
BLOCK = AdaptiveEnvelopeBlock()
