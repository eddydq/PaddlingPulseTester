from __future__ import annotations
import numpy as np
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class AutocorrelationBlock:
    manifest = BlockManifest(
        block_id="estimation.autocorrelation",
        group="estimation",
        language="py",
        entrypoint="analysis.algorithms.estimation.py.autocorrelation:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "candidate"},
        stateful=False,
        params_schema={
            "min_lag_samples": {"type": "int", "default": 15, "min": 1, "description": "Minimum lag in samples"},
            "max_lag_samples": {"type": "int", "default": 160, "min": 2, "description": "Maximum lag in samples"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        min_lag = int(params.get("min_lag_samples", 15))
        max_lag = int(params.get("max_lag_samples", min(160, len(values) // 2)))
        values = values - values.mean()
        full = np.correlate(values, values, mode="full")
        acf = full[len(values) - 1:]
        if acf[0] != 0:
            acf = acf / acf[0]
        segment = acf[min_lag:max_lag + 1]
        if len(segment) == 0:
            return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": 0.0}, sample_rate_hz=sr)]})
        best_lag = min_lag + int(np.argmax(segment))
        spm = 60.0 * sr / best_lag
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, axis=source.axis, sample_rate_hz=sr)]})

BLOCK = AutocorrelationBlock()
