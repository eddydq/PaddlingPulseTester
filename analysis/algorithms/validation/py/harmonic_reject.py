from __future__ import annotations

from analysis.scripts.blocks import BlockManifest, BlockResult


class HarmonicRejectBlock:
    manifest = BlockManifest(
        block_id="validation.harmonic_reject",
        group="validation",
        language="py",
        entrypoint="analysis.algorithms.validation.py.harmonic_reject:BLOCK",
        input_kinds=["candidate"],
        output_ports={"accepted": "candidate", "rejected": "candidate"},
        stateful=False,
        params_schema={
            "fundamental_spm": {"type": "float", "default": 60.0, "min": 0.0, "description": "Expected fundamental cadence"},
            "tolerance_spm": {"type": "float", "default": 5.0, "min": 0.0, "description": "Tolerance around harmonic checks"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        spm = float(source.data.get("spm", 0.0))
        fundamental = float(params.get("fundamental_spm", 60.0))
        tolerance = float(params.get("tolerance_spm", 5.0))
        harmonic_targets = (fundamental * 2.0, fundamental * 0.5)
        if any(abs(spm - target) <= tolerance for target in harmonic_targets):
            return BlockResult(outputs={"accepted": [], "rejected": [source]})
        return BlockResult(outputs={"accepted": [source], "rejected": []})


BLOCK = HarmonicRejectBlock()
