from analysis.scripts.blocks import load_block_catalog
from analysis.scripts.blocks import exercise_python_blocks


def test_catalog_contains_all_python_block_groups():
    catalog = load_block_catalog()
    groups = {manifest.group for manifest in catalog.values() if manifest.language == "py"}
    assert groups == {"representation", "pretraitement", "estimation", "detection", "validation", "suivi"}


def test_exercise_script_returns_summary():
    summary = exercise_python_blocks()
    assert summary["total_blocks"] > 0
    assert summary["failed_blocks"] == []
