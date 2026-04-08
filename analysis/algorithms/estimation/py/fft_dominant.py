from __future__ import annotations
import numpy as np
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class FftDominantBlock:
    manifest = BlockManifest(
        block_id="estimation.fft_dominant", group="estimation", language="py",
        entrypoint="analysis.algorithms.estimation.py.fft_dominant:BLOCK",
        input_kinds=["series"], output_ports={"primary": "candidate"}, stateful=False,
        params_schema={
            "min_hz": {"type": "float", "default": 0.33, "description": "Minimum frequency"},
            "max_hz": {"type": "float", "default": 2.0, "description": "Maximum frequency"},
        },
    )
    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        min_hz, max_hz = float(params.get("min_hz", 0.33)), float(params.get("max_hz", 2.0))
        spectrum = np.abs(np.fft.rfft(values - values.mean()))
        freqs = np.fft.rfftfreq(len(values), d=1.0 / sr)
        mask = (freqs >= min_hz) & (freqs <= max_hz)
        if not mask.any():
            return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": 0.0}, sample_rate_hz=sr)]})
        freq = freqs[mask][np.argmax(spectrum[mask])]
        spm = freq * 60.0
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, axis=source.axis, sample_rate_hz=sr)]})
BLOCK = FftDominantBlock()
