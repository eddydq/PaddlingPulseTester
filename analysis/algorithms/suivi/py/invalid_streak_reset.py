from __future__ import annotations

from analysis.scripts.blocks import BlockManifest, BlockResult


class InvalidStreakResetBlock:
    manifest = BlockManifest(
        block_id="suivi.invalid_streak_reset",
        group="suivi",
        language="py",
        entrypoint="analysis.algorithms.suivi.py.invalid_streak_reset:BLOCK",
        input_kinds=["estimate"],
        output_ports={"primary": "estimate"},
        stateful=True,
        params_schema={
            "max_invalid": {"type": "int", "default": 5, "min": 1, "description": "Consecutive invalid estimates before reset"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        spm = float(source.data.get("spm", 0.0))
        max_invalid = int(params.get("max_invalid", 5))
        invalid_count = int(state.get("invalid_count", 0))

        if spm == 0.0:
            invalid_count += 1
        else:
            invalid_count = 0

        reset = invalid_count >= max_invalid
        if reset:
            return BlockResult(outputs={"primary": []}, state={"invalid_count": 0, "reset": True})
        return BlockResult(outputs={"primary": [source]}, state={"invalid_count": invalid_count, "reset": False})


BLOCK = InvalidStreakResetBlock()
