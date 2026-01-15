from __future__ import annotations

import importlib

try:
    canyonero = importlib.import_module(".canyonero_py", __name__)
except Exception as exc:  # pragma: no cover
    raise ImportError(
        "canyonero_py extension is not available; build the package first with "
        "`python3 -m pip install -e ./python/ecuconnect_tool`"
    ) from exc

__all__ = ["canyonero"]
