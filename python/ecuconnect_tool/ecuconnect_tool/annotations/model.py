"""The annotation model, mirroring `Sources/ecuconnect-tool/Annotation.swift`.

Details stay structured instead of pre-formatted text so that the same
annotation can be rendered to a terminal, a log or JSON without the annotator
having to know which of them is the destination.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Optional, Sequence, Union


class Severity(Enum):
    REQUEST = "request"
    OK = "ok"
    PENDING = "pending"
    WARNING = "warning"
    ERROR = "error"


@dataclass(frozen=True)
class Field:
    label: str
    value: str


@dataclass(frozen=True)
class Measurement:
    label: str
    value: str


@dataclass(frozen=True)
class DTC:
    code: str
    explanation: Optional[str] = None
    status: Sequence[str] = ()


@dataclass(frozen=True)
class Note:
    text: str


Detail = Union[Field, Measurement, DTC, Note]


@dataclass(frozen=True)
class Annotation:
    severity: Severity
    headline: str
    details: Sequence[Detail] = field(default_factory=tuple)
