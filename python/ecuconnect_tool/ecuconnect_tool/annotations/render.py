"""Terminal rendering for annotations, matching the Swift tool's layout."""

from __future__ import annotations

import os
import sys

from .model import Annotation, DTC, Field, Measurement, Note, Severity

_SYMBOLS = {
    Severity.REQUEST: "»",
    Severity.OK: "✓",
    Severity.PENDING: "…",
    Severity.WARNING: "!",
    Severity.ERROR: "✗",
}

_COLORS = {
    Severity.REQUEST: "36",   # cyan
    Severity.OK: "32",        # green
    Severity.PENDING: "33",   # yellow
    Severity.WARNING: "33",
    Severity.ERROR: "31",     # red
}

_DIM = "2"
_BOLD_CYAN = "1;36"
_BOLD_MAGENTA = "1;35"


def _color_enabled() -> bool:
    if os.environ.get("CLICOLOR_FORCE", "") in ("1", "yes", "true"):
        return True
    if os.environ.get("NO_COLOR") is not None:
        return False
    if not sys.stdout.isatty():
        return False
    return os.environ.get("TERM", "") not in ("", "dumb", "cons25", "emacs")


def _paint(text: str, code: str) -> str:
    if not _color_enabled():
        return text
    return f"\033[{code}m{text}\033[0m"


def _render_detail(detail) -> str:
    if isinstance(detail, Field):
        return f"{_paint(detail.label, _DIM)}: {detail.value}"
    if isinstance(detail, Measurement):
        return f"{_paint(detail.label, _DIM)}: {_paint(detail.value, _BOLD_CYAN)}"
    if isinstance(detail, DTC):
        line = _paint(detail.code, _BOLD_MAGENTA)
        if detail.explanation:
            line += f" — {detail.explanation}"
        if detail.status:
            line += " " + _paint("[" + ", ".join(detail.status) + "]", _DIM)
        return line
    if isinstance(detail, Note):
        return _paint(detail.text, _DIM)
    return str(detail)


def annotation_lines(annotation: Annotation) -> list[str]:
    color = _COLORS[annotation.severity]
    lines = [f" {_paint(_SYMBOLS[annotation.severity], color)}  {_paint(annotation.headline, color)}"]
    lines.extend(f"     {_render_detail(detail)}" for detail in annotation.details)
    return lines


def print_annotation(annotation: Annotation) -> None:
    for line in annotation_lines(annotation):
        print(line)
