from __future__ import annotations

import importlib


def load_python_object(entrypoint: str):
    module_name, attr_name = entrypoint.split(":", 1)
    module = importlib.import_module(module_name)
    return getattr(module, attr_name)
