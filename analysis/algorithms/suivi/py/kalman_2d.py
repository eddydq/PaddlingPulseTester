from __future__ import annotations

import numpy as np

from analysis.scripts.blocks import BlockManifest, BlockResult, Packet


class Kalman2dBlock:
    manifest = BlockManifest(
        block_id="suivi.kalman_2d",
        group="suivi",
        language="py",
        entrypoint="analysis.algorithms.suivi.py.kalman_2d:BLOCK",
        input_kinds=["candidate"],
        output_ports={"primary": "estimate"},
        stateful=True,
        params_schema={
            "process_noise": {"type": "float", "default": 1.0, "min": 0.001, "description": "Process noise"},
            "measurement_noise": {"type": "float", "default": 10.0, "min": 0.001, "description": "Measurement noise"},
        },
    )

    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        measurement = float(source.data.get("spm", 0.0))
        process_noise = float(params.get("process_noise", 1.0))
        measurement_noise = float(params.get("measurement_noise", 10.0))

        x = np.array(state.get("x", [measurement, 0.0]), dtype=float)
        p = np.array(state.get("P", [[1000.0, 0.0], [0.0, 1000.0]]), dtype=float)
        f = np.array([[1.0, 1.0], [0.0, 1.0]], dtype=float)
        h = np.array([[1.0, 0.0]], dtype=float)
        q = np.array([[process_noise, 0.0], [0.0, process_noise]], dtype=float)
        r = np.array([[measurement_noise]], dtype=float)

        x = f @ x
        p = f @ p @ f.T + q

        innovation = measurement - (h @ x)[0]
        innovation_covariance = (h @ p @ h.T + r)[0, 0]
        gain = (p @ h.T) / innovation_covariance
        x = x + (gain @ np.array([[innovation]], dtype=float)).flatten()
        p = p - gain @ h @ p

        return BlockResult(
            outputs={"primary": [Packet(kind="estimate", data={"spm": float(x[0])}, sample_rate_hz=source.sample_rate_hz)]},
            state={"x": x.tolist(), "P": p.tolist()},
        )


BLOCK = Kalman2dBlock()
