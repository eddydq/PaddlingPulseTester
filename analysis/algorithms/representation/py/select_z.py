from __future__ import annotations

from analysis.scripts.blocks import BlockResult, Packet, BlockManifest


class SelectZBlock:
    manifest = BlockManifest(
        block_id="representation.select_z",
        group="representation",
        language="py",
        entrypoint="analysis.algorithms.representation.py.select_z:BLOCK",
        input_kinds=["raw_window"],
        output_ports={"primary": "series"},
        stateful=False,
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = list(source.data["series"]["z"])
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": values}, axis="z", sample_rate_hz=source.sample_rate_hz)]})


BLOCK = SelectZBlock()
