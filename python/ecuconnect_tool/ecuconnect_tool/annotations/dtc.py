"""Diagnostic trouble code decoding, mirroring `Automotive.DTC`.

OBD2 (SAE J2012) packs a code into two bytes: the top two bits select the
category, the next two bits the first digit, the remaining nibbles the rest.
UDS (ISO 14229-1) adds a third byte plus a status byte.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Sequence

from ._core_strings import CORE_STRINGS
from ._dtc_tables import DTC_DESCRIPTIONS

_KINDS = ("P", "C", "B", "U")


@dataclass(frozen=True)
class TroubleCode:
    code: str
    status: int = 0

    @property
    def kind(self) -> str:
        return self.code[0]

    @property
    def type_digit(self) -> str:
        return self.code[1]

    @property
    def category_digit(self) -> str:
        return self.code[2]

    @property
    def fault_digits(self) -> str:
        return self.code[3:5]


def decode(data: Sequence[int]) -> Optional[TroubleCode]:
    """Decode a two- or three-byte code, optionally followed by a status byte."""
    if not 2 <= len(data) <= 4:
        return None
    code_bytes = list(data[:3]) if len(data) >= 3 else list(data[:2])
    if len(data) == 4:
        code_bytes = list(data[:3])
    if not any(code_bytes):
        return None  # all zeroes means "no DTC", not code P0000

    a = code_bytes[0]
    digits = [(a & 0x30) >> 4, a & 0x0F]
    b = code_bytes[1]
    digits += [b >> 4, b & 0x0F]
    if len(code_bytes) == 3:
        c = code_bytes[2]
        digits += [c >> 4, c & 0x0F]

    code = _KINDS[a >> 6] + "".join(f"{digit:X}" for digit in digits)
    status = data[3] if len(data) == 4 else 0
    return TroubleCode(code=code, status=status)


def _fragment(key: str) -> Optional[str]:
    return CORE_STRINGS.get(key)


def generic_explanation(trouble_code: TroubleCode) -> str:
    """Component-based explanation, used when the code has no exact entry."""
    kind = _fragment("OBD2_DTC_KIND_" + trouble_code.kind)
    type_name = _fragment("OBD2_DTC_TYPE_" + trouble_code.type_digit)
    category = _fragment("OBD2_DTC_CAT_" + trouble_code.category_digit)
    fault = _fragment("OBD2_DTC_FAULT_" + trouble_code.fault_digits)

    classification = " • ".join(part for part in (kind, type_name) if part)
    detail = " – ".join(part for part in (category, fault) if part)

    if classification and detail:
        return f"{classification} • {detail}"
    return classification or detail or trouble_code.code


def explanation(trouble_code: TroubleCode) -> str:
    exact = DTC_DESCRIPTIONS.get(trouble_code.code)
    if exact:
        return exact
    # Three-byte UDS codes rarely have an exact entry; fall back to the base code.
    base = DTC_DESCRIPTIONS.get(trouble_code.code[:5])
    if base:
        return base
    return generic_explanation(trouble_code)


def chunked(data: Sequence[int], size: int) -> list[list[int]]:
    return [list(data[index:index + size]) for index in range(0, len(data) - size + 1, size)]
