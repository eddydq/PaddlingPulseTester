from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


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
