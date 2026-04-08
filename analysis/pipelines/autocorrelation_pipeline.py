from __future__ import annotations

from analysis.algorithms.representation.py.select_axis import BLOCK as SELECT_AXIS
from analysis.algorithms.pretraitement.py.hpf_gravity import BLOCK as HPF_GRAVITY
from analysis.algorithms.estimation.py.autocorrelation import BLOCK as AUTOCORR
from analysis.algorithms.validation.py.spm_range_gate import BLOCK as RANGE_GATE

PIPELINE_NAME = "autocorrelation"
OUTPUT_SERIES_NAME = "autocorrelation_y"

def get_blocks():
    return {
        "representation.select_axis": SELECT_AXIS,
        "pretraitement.hpf_gravity": HPF_GRAVITY,
        "estimation.autocorrelation": AUTOCORR,
        "validation.spm_range_gate": RANGE_GATE
    }

def get_graph():
    return {
        "nodes": [
            {"node_id": "n1", "block_id": "representation.select_axis", "params": {"axis": "y"}},
            {"node_id": "n2", "block_id": "pretraitement.hpf_gravity", "params": {}},
            {"node_id": "n3", "block_id": "estimation.autocorrelation", "params": {}},
            {"node_id": "n4", "block_id": "validation.spm_range_gate", "params": {"min_spm": 20.0, "max_spm": 120.0}},
        ],
        "inputs": {
            "n1.source": "input.raw",
            "n2.source": "n1.primary",
            "n3.source": "n2.primary",
            "n4.source": "n3.primary"
        },
        "outputs": {
            "accepted": "n4.accepted",
            "rejected": "n4.rejected"
        }
    }
