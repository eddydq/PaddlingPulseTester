from __future__ import annotations

from dataclasses import dataclass, field


VALID_GROUPS = {"representation", "pretraitement", "estimation", "detection", "validation", "suivi"}
VALID_LANGUAGES = {"py", "c"}


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
