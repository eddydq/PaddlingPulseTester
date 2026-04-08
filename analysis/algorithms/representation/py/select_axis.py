from __future__ import annotations

from analysis.scripts.blocks import BlockResult, Packet
from analysis.scripts.blocks import BlockManifest


class SelectAxisBlock:
    manifest = BlockManifest(
        block_id="representation.select_axis",
        group="representation",
        language="py",
        entrypoint="analysis.algorithms.representation.py.select_axis:BLOCK",
        input_kinds=["raw_window"],
        output_ports={"primary": "series"},
        stateful=False,
        params_schema={
            "axis": {"type": "str", "default": "y", "enum": ["x", "y", "z"], "description": "IMU axis to extract"},
        },
    )

    def run(self, input_packets, params, state):
        axis = params.get("axis", "y")
        source = input_packets["source"][0]
        values = list(source.data["series"][axis])
        return BlockResult(outputs={"primary": [Packet(kind="series", data={"values": values}, axis=axis, sample_rate_hz=source.sample_rate_hz)]})


BLOCK = SelectAxisBlock()
