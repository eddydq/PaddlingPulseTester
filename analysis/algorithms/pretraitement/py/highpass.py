from __future__ import annotations

import os
import sys
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../../../tests/algorithms/python_stroke_rate')))

from _filters import remove_gravity_hpf
from analysis.scripts.block_contract import BlockResult, Packet
from analysis.scripts.block_manifest import BlockManifest


class HighPassBlock:
    manifest = BlockManifest(
        block_id="pretraitement.highpass",
        group="pretraitement",
        language="py",
        entrypoint="analysis.algorithms.pretraitement.py.highpass:BLOCK",
        input_kinds=["series"],
        output_ports={"primary": "series"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = remove_gravity_hpf(list(source.data["values"]))
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": values}, axis=source.axis, sample_rate_hz=source.sample_rate_hz)]})

BLOCK = HighPassBlock()