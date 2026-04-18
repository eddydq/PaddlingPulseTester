from __future__ import annotations

from analysis.scripts.blocks import BlockManifest, BlockResult


class ConfirmationFilterBlock:
    manifest = BlockManifest(
        block_id="suivi.confirmation_filter",
        group="suivi",
        language="py",
        entrypoint="analysis.algorithms.suivi.py.confirmation_filter:BLOCK",
        input_kinds=["estimate"],
        output_ports={"primary": "estimate"},
        stateful=True,
        params_schema={
            "required_streak": {"type": "int", "default": 3, "min": 1, "description": "Consecutive estimates required before emitting"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        streak = int(state.get("streak", 0)) + 1
        required_streak = int(params.get("required_streak", 3))
        new_state = {"streak": streak}
        if streak >= required_streak:
            return BlockResult(outputs={"primary": [source]}, state=new_state)
        return BlockResult(outputs={"primary": []}, state=new_state)


BLOCK = ConfirmationFilterBlock()
