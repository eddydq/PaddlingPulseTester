from __future__ import annotations
import numpy as np
from scipy.signal import find_peaks
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class AdaptivePeakDetectBlock:
    manifest = BlockManifest(
        block_id="detection.adaptive_peak_detect", group="detection", language="py",
        entrypoint="analysis.algorithms.detection.py.adaptive_peak_detect:BLOCK",
        input_kinds=["series"], output_ports={"primary": "candidate"}, stateful=False,
        params_schema={
            "min_distance_samples": {"type": "int", "default": 26, "min": 1, "description": "Minimum distance between peaks in samples"},
            "prominence": {"type": "float", "default": 0.1, "min": 0.0, "description": "Required peak prominence"},
        },
    )
    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        dist = int(params.get("min_distance_samples", 26))
        prom = float(params.get("prominence", 0.1))
        peaks, _ = find_peaks(values, distance=dist, prominence=prom)
        intervals = (np.diff(peaks) / sr).tolist() if len(peaks) > 1 else []
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"intervals": intervals}, axis=source.axis, sample_rate_hz=sr)]})
BLOCK = AdaptivePeakDetectBlock()
