from pathlib import Path


def test_mixed_pipeline_definition_exists():
    assert Path("analysis/scripts/pipelines/mixed_reference.json").exists()
