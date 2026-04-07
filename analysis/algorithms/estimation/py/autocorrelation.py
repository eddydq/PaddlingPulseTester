from __future__ import annotations

import os
import sys
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../../../tests/algorithms/python_stroke_rate')))

from common import estimate_autocorrelation_stroke_rate
from analysis.scripts.block_contract import BlockResult, Packet
from analysis.scripts.block_manifest import BlockManifest

class AutocorrelationBlock:
    manifest = BlockManifest(
        block_id="estimation.autocorrelation",
        group="estimation",
        language="py",
        entrypoint="analysis.algorithms.estimation.py.autocorrelation:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "candidate"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        spm = estimate_autocorrelation_stroke_rate(list(source.data["values"]))
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, axis=source.axis, sample_rate_hz=source.sample_rate_hz)]})

BLOCK = AutocorrelationBlock()