from __future__ import annotations
import numpy as np
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class SchmittTriggerBlock:
    manifest = BlockManifest(
        block_id="detection.schmitt_trigger", group="detection", language="py",
        entrypoint="analysis.algorithms.detection.py.schmitt_trigger:BLOCK",
        input_kinds=["series"], output_ports={"primary": "candidate"}, stateful=False,
        params_schema={
            "high_thresh": {"type": "float", "required": True, "description": "Upper trigger threshold"},
            "low_thresh": {"type": "float", "required": True, "description": "Lower reset threshold"},
        },
    )
    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        high = float(params.get("high_thresh", 0.5))
        low = float(params.get("low_thresh", -0.5))
        triggers = []
        armed = True
        for i, v in enumerate(values):
            if armed and v >= high:
                triggers.append(i)
                armed = False
            elif not armed and v <= low:
                armed = True
        intervals = (np.diff(triggers) / sr).tolist() if len(triggers) > 1 else []
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"intervals": intervals}, axis=source.axis, sample_rate_hz=sr)]})
BLOCK = SchmittTriggerBlock()
