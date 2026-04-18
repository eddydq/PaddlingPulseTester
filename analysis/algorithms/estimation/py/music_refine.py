from __future__ import annotations
import numpy as np
from analysis.scripts.blocks import BlockResult, Packet, BlockManifest

class MusicRefineBlock:
    manifest = BlockManifest(
        block_id="estimation.music_refine", group="estimation", language="py",
        entrypoint="analysis.algorithms.estimation.py.music_refine:BLOCK",
        input_kinds=["series"], output_ports={"primary": "candidate"}, stateful=False,
        params_schema={
            "num_signals": {"type": "int", "default": 2, "description": "Number of signal components (2 per real sinusoid)"},
            "min_hz": {"type": "float", "default": 0.33, "description": "Minimum frequency"},
            "max_hz": {"type": "float", "default": 2.0, "description": "Maximum frequency"},
            "subspace_dim": {"type": "int", "default": 16, "description": "Autocorrelation matrix dimension"},
        },
    )
    def run(self, input_packets, params, state):
        source = input_packets["source"][0]
        values = np.array(source.data["values"], dtype=float)
        sr = source.sample_rate_hz or 52.0
        num_signals = int(params.get("num_signals", 2))
        min_hz = float(params.get("min_hz", 0.33))
        max_hz = float(params.get("max_hz", 2.0))
        M = int(params.get("subspace_dim", 16))
        values = values - values.mean()
        N = len(values)
        # Build data matrix for covariance estimation
        X = np.array([values[i:i + M] for i in range(N - M)])
        R = X.T @ X / (N - M)
        eigvals, eigvecs = np.linalg.eigh(R)
        # Noise subspace = eigenvectors with smallest eigenvalues
        noise_vecs = eigvecs[:, :M - num_signals]
        freqs = np.linspace(min_hz, max_hz, 1000)
        pseudo_spectrum = np.zeros(len(freqs))
        n_arr = np.arange(M)
        for i, f in enumerate(freqs):
            a = np.exp(1j * 2 * np.pi * f * n_arr / sr)
            proj = noise_vecs.T.conj() @ a
            pseudo_spectrum[i] = 1.0 / (np.real(proj.conj() @ proj) + 1e-12)
        peak_freq = freqs[np.argmax(pseudo_spectrum)]
        spm = peak_freq * 60.0
        return BlockResult(outputs={"primary": [Packet(kind="candidate", data={"spm": spm}, axis=source.axis, sample_rate_hz=sr)]})
BLOCK = MusicRefineBlock()
