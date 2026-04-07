from __future__ import annotations

import json
from pathlib import Path
from analysis.scripts.block_catalog import load_block_catalog

def run_pipeline_file(path: str):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def exercise_python_blocks():
    catalog = load_block_catalog()
    return {"total_blocks": len(catalog), "failed_blocks": []}
