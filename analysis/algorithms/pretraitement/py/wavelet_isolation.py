from __future__ import annotations
import numpy as np
import pywt
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class WaveletIsolationBlock:
    manifest = BlockManifest(
        block_id="pretraitement.wavelet_isolation",
        group="pretraitement",
        language="py",
        entrypoint="analysis.algorithms.pretraitement.py.wavelet_isolation:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "series"},
        stateful=False,
        params_schema={
            "wavelet": {"type": "str", "default": "db4", "description": "Wavelet family"},
            "level": {"type": "int", "default": 4, "min": 1, "max": 10, "description": "Decomposition level"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        wavelet = params.get("wavelet", "db4")
        level = int(params.get("level", 4))
        coeffs = pywt.wavedec(values, wavelet, level=level)
        coeffs[0] = np.zeros_like(coeffs[0])
        reconstructed = pywt.waverec(coeffs, wavelet)[:len(values)].tolist()
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": reconstructed}, axis=source.axis, sample_rate_hz=source.sample_rate_hz)]})

BLOCK = WaveletIsolationBlock()
