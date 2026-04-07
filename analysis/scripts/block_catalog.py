from __future__ import annotations

from pathlib import Path


def load_block_catalog(root: Path | None = None):
    root = Path("analysis/algorithms") if root is None else Path(root)
    catalog = {}
    for path in root.rglob("*.py"):
        if path.name in {"__init__.py"}:
            continue
        parts = path.relative_to(root).with_suffix("").parts
        if len(parts) >= 3:
            group, language, block_name = parts[0], parts[1], parts[2]
            catalog[f"{group}.{block_name}"] = type("M", (), {"group": group, "language": language})()
    return catalog
