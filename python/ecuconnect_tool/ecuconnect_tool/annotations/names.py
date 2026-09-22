"""Lookups over the generated naming tables.

Mirrors `Sources/ecuconnect-tool/DiagnosticNames.swift`; the tables themselves
are generated from it, so the two cannot drift.
"""

from __future__ import annotations

from typing import Optional, Sequence

from ._names import (
    DATA_IDENTIFIER_RANGES,
    NEGATIVE_RESPONSE_NAMES,
    SERVICE_NAMES,
    STANDARD_DATA_IDENTIFIERS,
    VEHICLE_INFORMATION_NAMES,
)
from ._sub_functions import SUB_FUNCTIONS

#: Services carrying a sub-function byte, and therefore the suppress-positive-response bit.
SERVICES_WITH_SUB_FUNCTION = frozenset({0x10, 0x11, 0x19, 0x27, 0x28, 0x29, 0x31, 0x3E, 0x85, 0x86, 0x87})

NEGATIVE_RESPONSE = 0x7F
RESPONSE_PENDING = 0x78


def is_obd2_service(sid: int) -> bool:
    """OBD2 occupies services 0x01…0x0A; above that we are in UDS/KWP territory."""
    return 0x01 <= sid <= 0x0A


def service_name(sid: int) -> str:
    return SERVICE_NAMES.get(sid, f"Service 0x{sid:02X}")


def negative_response_name(nrc: int) -> str:
    return NEGATIVE_RESPONSE_NAMES.get(nrc, "Unknown NRC")


def vehicle_information_name(info_type: int) -> str:
    return VEHICLE_INFORMATION_NAMES.get(info_type, f"Info Type 0x{info_type:02X}")


def data_identifier_name(did: int) -> Optional[str]:
    name = STANDARD_DATA_IDENTIFIERS.get(did)
    if name is not None:
        return name
    for first, last, range_name in DATA_IDENTIFIER_RANGES:
        if first <= did <= last:
            return range_name
    return None


def sub_function_name(service: int, value: int) -> Optional[str]:
    """Some sub-functions (e.g. TesterPresent 0x80) encode the suppression bit
    as part of their own value, so try the byte verbatim before masking it off."""
    table = SUB_FUNCTIONS.get(service)
    if table is None:
        return None
    return table.get(value) or table.get(value & 0x7F)


_STATUS_FLAGS = (
    (0x01, "testFailed"),
    (0x02, "testFailedThisCycle"),
    (0x04, "pending"),
    (0x08, "confirmed"),
    (0x10, "testNotCompletedSinceClear"),
    (0x20, "testFailedSinceClear"),
    (0x40, "testNotCompletedThisCycle"),
    (0x80, "warningIndicatorRequested"),
)


def status_flags(mask: int) -> Sequence[str]:
    """Expand a DTC status byte (ISO 14229-1 Annex D) into short labels."""
    return tuple(name for bit, name in _STATUS_FLAGS if mask & bit)
