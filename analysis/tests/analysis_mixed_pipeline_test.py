import csv
from pathlib import Path

from analysis.scripts.blocks import Packet
from analysis.scripts import run_all_pipelines


def test_pipeline_module_definition_exists():
    assert Path("analysis/pipelines/autocorrelation_pipeline.py").exists()


class _FakeExecutor:
    def __init__(self, blocks):
        self.blocks = blocks

    def run(self, graph, inputs):
        return {"accepted": [Packet(kind="candidate", data={"spm": 61.5})]}, {"node_timings": {}, "total_elapsed_ms": 1.0}


class _FakePipeline:
    OUTPUT_SERIES_NAME = "autocorrelation_y"

    @staticmethod
    def get_blocks():
        return {}

    @staticmethod
    def get_graph():
        return {"nodes": [], "inputs": {}, "outputs": {}}


def test_process_log_file_accepts_executor_tuple_return(tmp_path, monkeypatch):
    monkeypatch.setattr(run_all_pipelines, "PipelineExecutor", _FakeExecutor)

    csv_path = tmp_path / "raw.csv"
    output_csv = tmp_path / "out.csv"
    fieldnames = ["timestamp", "count"]
    row = {"timestamp": "2026-04-07T12:00:00", "count": str(run_all_pipelines.SAMPLE_STORE_CAPACITY)}
    for axis_name in run_all_pipelines.AXES:
        for sample_index in range(run_all_pipelines.SAMPLE_STORE_CAPACITY):
            field_name = f"{axis_name}_{sample_index:03d}"
            fieldnames.append(field_name)
            row[field_name] = "0.0"

    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerow(row)

    rows_written = run_all_pipelines.process_log_file(csv_path, output_csv, [_FakePipeline()])

    assert rows_written == 1
    with output_csv.open("r", newline="", encoding="utf-8") as handle:
        result_rows = list(csv.DictReader(handle))
    assert result_rows[0]["autocorrelation_y"] == "61.500"
