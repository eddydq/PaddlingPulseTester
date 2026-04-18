from __future__ import annotations
import numpy as np
from scipy.signal import hilbert
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class HilbertFreqBlock:
    manifest = BlockManifest(
        block_id="estimation.hilbert_freq", group="estimation", language="py",
        entrypoint="analysis.algorithms.estimation.py.hilbert_freq:BLOCK",
        input_kinds=["series"], output_ports={"primary": "candidate"}, stateful=False,
    )
    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        analytic = hilbert(values)
        phase = np.unwrap(np.angle(analytic))
        inst_freq = np.diff(phase) / (2 * np.pi) * sr
        median_freq = float(np.median(inst_freq[inst_freq > 0])) if np.any(inst_freq > 0) else 0.0
        spm = median_freq * 60.0
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, axis=source.axis, sample_rate_hz=sr)]})
BLOCK = HilbertFreqBlock()
