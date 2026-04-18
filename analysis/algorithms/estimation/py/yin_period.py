from __future__ import annotations
import numpy as np
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class YinPeriodBlock:
    manifest = BlockManifest(
        block_id="estimation.yin_period", group="estimation", language="py",
        entrypoint="analysis.algorithms.estimation.py.yin_period:BLOCK",
        input_kinds=["series"], output_ports={"primary": "candidate"}, stateful=False,
        params_schema={
            "threshold": {"type": "float", "default": 0.15, "description": "CMNDF threshold"},
            "min_hz": {"type": "float", "default": 0.33, "description": "Minimum frequency"},
            "max_hz": {"type": "float", "default": 2.0, "description": "Maximum frequency"},
        },
    )
    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        threshold = float(params.get("threshold", 0.15))
        min_hz = float(params.get("min_hz", 0.33))
        max_hz = float(params.get("max_hz", 2.0))
        min_lag = int(sr / max_hz)
        max_lag = min(int(sr / min_hz), len(values) // 2)
        W = len(values)
        d = np.zeros(max_lag + 1)
        for tau in range(1, max_lag + 1):
            diff = values[:W - tau] - values[tau:W]
            d[tau] = np.sum(diff ** 2)
        cmndf = np.ones(max_lag + 1)
        running_sum = 0.0
        for tau in range(1, max_lag + 1):
            running_sum += d[tau]
            cmndf[tau] = d[tau] / (running_sum / tau) if running_sum > 0 else d[tau]
        best_tau = 0
        for tau in range(min_lag, max_lag + 1):
            if cmndf[tau] < threshold:
                best_tau = tau
                break
        if best_tau == 0:
            segment = cmndf[min_lag:max_lag + 1]
            if len(segment) > 0:
                best_tau = min_lag + int(np.argmin(segment))
        spm = 60.0 * sr / best_tau if best_tau > 0 else 0.0
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, axis=source.axis, sample_rate_hz=sr)]})
BLOCK = YinPeriodBlock()
