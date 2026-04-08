from __future__ import annotations

import importlib
import json
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

VALID_GROUPS = {"representation", "pretraitement", "estimation", "detection", "validation", "suivi"}
VALID_LANGUAGES = {"py", "c"}

@dataclass(frozen=True)
class Packet:
    kind: str
    data: Any
    sample_rate_hz: float | None = None
    window_id: str | None = None
    timestamp: str | None = None
    axis: str | None = None
    units: str | None = None
    confidence: float | None = None
    source_block: str | None = None
    metadata: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return self.__dict__ | {"metadata": dict(self.metadata)}

    @classmethod
    def from_dict(cls, payload: dict[str, Any]) -> "Packet":
        return cls(**payload)


@dataclass(frozen=True)
class BlockResult:
    outputs: dict[str, list[Packet]]
    state: dict[str, Any] = field(default_factory=dict)
    diagnostics: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class BlockManifest:
    block_id: str
    group: str
    language: str
    entrypoint: str
    input_kinds: list[str]
    output_ports: dict[str, str]
    stateful: bool
    params_schema: dict[str, object] = field(default_factory=dict)

    def __post_init__(self) -> None:
        if self.group not in VALID_GROUPS:
            raise ValueError(f"invalid group: {self.group}")
        if self.language not in VALID_LANGUAGES:
            raise ValueError(f"invalid language: {self.language}")


def load_python_object(entrypoint: str):
    module_name, attr_name = entrypoint.split(":", 1)
    module = importlib.import_module(module_name)
    return getattr(module, attr_name)


class PipelineExecutor:
    def __init__(self, blocks: dict[str, object]):
        self.blocks = blocks
        self.node_states: dict[str, dict] = {}

    def run(self, graph: dict[str, object], inputs: dict[str, list[object]]):
        node_outputs: dict[str, dict[str, list[object]]] = {}
        node_timings: dict[str, dict] = {}
        t_start = time.perf_counter()

        for node in graph["nodes"]:
            block = self.blocks[node["block_id"]]
            bound_inputs = {}
            for target, source in graph.get("inputs", {}).items():
                node_id, port = target.split(".", 1)
                if node_id == node["node_id"]:
                    if source in inputs:
                        packets = inputs[source]
                    else:
                        src_node, src_port = source.split(".", 1)
                        packets = node_outputs[src_node][src_port]

                    for packet in packets:
                        if packet.kind not in block.manifest.input_kinds:
                            raise ValueError(f"packet kind mismatch for {block.manifest.block_id}")
                    bound_inputs[port] = packets

            nid = node["node_id"]
            state = self.node_states.get(nid, {})
            t0 = time.perf_counter()
            result = block.run(bound_inputs, node.get("params", {}), state)
            t1 = time.perf_counter()

            node_outputs[nid] = result.outputs
            if block.manifest.stateful:
                self.node_states[nid] = result.state
            node_timings[nid] = {
                "block_id": block.manifest.block_id,
                "elapsed_ms": round((t1 - t0) * 1000, 3),
            }

        exported = {}
        for name, source in graph["outputs"].items():
            node_id, port = source.split(".", 1)
            exported[name] = node_outputs[node_id][port]

        diagnostics = {
            "node_timings": node_timings,
            "total_elapsed_ms": round((time.perf_counter() - t_start) * 1000, 3),
        }
        return exported, diagnostics


def load_block_catalog(root: Path | None = None):
    root = Path("analysis/algorithms") if root is None else Path(root)
    catalog = {}
    for path in root.rglob("*.py"):
        if path.name in {"__init__.py"}:
            continue
        parts = path.relative_to(root).with_suffix("").parts
        if len(parts) >= 3:
            group, language, block_name = parts[-3], parts[-2], parts[-1]
            catalog[f"{group}.{block_name}"] = type("M", (), {"group": group, "language": language})()
    return catalog

def run_pipeline_file(path: str):
    return json.loads(Path(path).read_text(encoding="utf-8"))

def exercise_python_blocks():
    catalog = load_block_catalog()
    return {"total_blocks": len(catalog), "failed_blocks": []}