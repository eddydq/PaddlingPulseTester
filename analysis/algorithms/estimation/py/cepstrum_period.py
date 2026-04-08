from __future__ import annotations
import numpy as np
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class CepstrumPeriodBlock:
    manifest = BlockManifest(
        block_id="estimation.cepstrum_period", group="estimation", language="py",
        entrypoint="analysis.algorithms.estimation.py.cepstrum_period:BLOCK",
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
        min_hz = float(params.get("min_hz", 0.33))
        max_hz = float(params.get("max_hz", 2.0))
        min_q = int(sr / max_hz)
        max_q = min(int(sr / min_hz), len(values) // 2)
        cepstrum = np.fft.irfft(np.log(np.abs(np.fft.rfft(values)) + 1e-12))
        segment = cepstrum[min_q:max_q + 1]
        if len(segment) == 0:
            return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": 0.0}, sample_rate_hz=sr)]})
        peak_q = min_q + int(np.argmax(segment))
        spm = 60.0 * sr / peak_q if peak_q > 0 else 0.0
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, axis=source.axis, sample_rate_hz=sr)]})
BLOCK = CepstrumPeriodBlock()
