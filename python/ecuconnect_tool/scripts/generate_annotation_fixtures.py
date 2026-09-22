#!/usr/bin/env python3
"""Regenerate `Tests/Fixtures/annotations.json`, the golden fixtures shared by
the Swift and Python annotators.

Rendering happens through the Python implementation; the Swift test suite
replays the same file. Regenerate only after reviewing why the output changed —
a diff here means one of the two implementations moved.

Usage:
    scripts/generate_annotation_fixtures.py
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

os.environ["NO_COLOR"] = "1"
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from ecuconnect_tool.annotations import (  # noqa: E402
    FRAMING_PDU,
    FRAMING_RAW_FRAMES,
    MessageAnnotator,
    annotation_lines,
)

VIN = list(b"WBA3A5C51DF123456")

#: (name, framing, request, response)
CASES = [
    ("obd2-rpm", "pdu", [0x01, 0x0C], [0x41, 0x0C, 0x0C, 0xB5]),
    ("obd2-coolant-temperature", "pdu", [0x01, 0x05], [0x41, 0x05, 0x5A]),
    ("obd2-multi-pid-request", "pdu", [0x01, 0x0C, 0x0D, 0x05], None),
    ("obd2-supported-pids", "pdu", [0x01, 0x00], [0x41, 0x00, 0xBE, 0x3E, 0xB8, 0x11]),
    ("obd2-fuel-system-status", "pdu", [0x01, 0x03], [0x41, 0x03, 0x02, 0x00]),
    ("obd2-secondary-air-status", "pdu", [0x01, 0x12], [0x41, 0x12, 0x04]),
    ("obd2-auxiliary-input-status", "pdu", [0x01, 0x1E], [0x41, 0x1E, 0x01]),
    ("obd2-vehicle-speed", "pdu", [0x01, 0x0D], [0x41, 0x0D, 0x64]),
    ("obd2-control-module-voltage", "pdu", [0x01, 0x42], [0x41, 0x42, 0x3A, 0x98]),
    ("obd2-maf", "pdu", [0x01, 0x10], [0x41, 0x10, 0x0A, 0xF0]),
    ("obd2-obd-standard", "pdu", [0x01, 0x1C], [0x41, 0x1C, 0x01]),
    ("obd2-fuel-trim", "pdu", [0x01, 0x06], [0x41, 0x06, 0x70]),
    ("obd2-timing-advance", "pdu", [0x01, 0x0E], [0x41, 0x0E, 0x90]),
    ("obd2-vin", "pdu", [0x09, 0x02], [0x49, 0x02, 0x01] + VIN),
    ("obd2-calibration-ids", "pdu", [0x09, 0x04], [0x49, 0x04, 0x01] + list(b"CAL-ID-1\x00CAL-ID-2")),
    ("obd2-stored-dtcs", "pdu", [0x03], [0x43, 0x02, 0x01, 0x33, 0x04, 0x20]),
    ("obd2-no-dtcs", "pdu", [0x03], [0x43, 0x00]),
    ("obd2-pending-dtcs", "pdu", [0x07], [0x47, 0x01, 0x01, 0x71]),
    ("obd2-clear-dtcs", "pdu", [0x04], [0x44]),
    ("uds-session-control", "pdu", [0x10, 0x03], [0x50, 0x03, 0x00, 0x32, 0x01, 0xF4]),
    ("uds-ecu-reset", "pdu", [0x11, 0x01], [0x51, 0x01]),
    ("uds-read-vin-by-did", "pdu", [0x22, 0xF1, 0x90], [0x62, 0xF1, 0x90] + VIN),
    ("uds-vendor-did", "pdu", [0x22, 0x12, 0x34], [0x62, 0x12, 0x34, 0xDE, 0xAD, 0xBE, 0xEF]),
    ("uds-write-did", "pdu", [0x2E, 0xF1, 0x99, 0x20, 0x26, 0x09, 0x22], [0x6E, 0xF1, 0x99]),
    ("uds-security-access", "pdu", [0x27, 0x01], [0x67, 0x01, 0x11, 0x22, 0x33, 0x44]),
    ("uds-security-send-key", "pdu", [0x27, 0x02, 0xAA, 0xBB], [0x67, 0x02]),
    ("uds-routine-control", "pdu", [0x31, 0x01, 0xFF, 0x00], [0x71, 0x01, 0xFF, 0x00, 0x00]),
    ("uds-tester-present-suppressed", "pdu", [0x3E, 0x80], None),
    ("uds-communication-control", "pdu", [0x28, 0x03, 0x01], [0x68, 0x03]),
    ("uds-control-dtc-setting", "pdu", [0x85, 0x02], [0xC5, 0x02]),
    ("uds-read-dtc-by-status-mask", "pdu", [0x19, 0x02, 0x8C],
     [0x59, 0x02, 0x4F, 0xCD, 0x04, 0x20, 0x0C, 0x80, 0x1C, 0x60, 0x0F]),
    ("uds-read-dtc-count", "pdu", [0x19, 0x01, 0x8C], [0x59, 0x01, 0xFF, 0x01, 0x00, 0x03]),
    ("uds-request-download", "pdu",
     [0x34, 0x00, 0x44, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00], [0x74, 0x20, 0x04, 0x02]),
    ("uds-transfer-data", "pdu", [0x36, 0x01, 0xAA, 0xBB], [0x76, 0x01]),
    ("nrc-security-access-denied", "pdu", None, [0x7F, 0x22, 0x33]),
    ("nrc-response-pending", "pdu", None, [0x7F, 0x31, 0x78]),
    ("nrc-service-not-supported", "pdu", None, [0x7F, 0x19, 0x11]),
    ("nrc-truncated", "pdu", None, [0x7F, 0x22]),
    ("non-diagnostic-payload", "pdu", None, [0x12, 0x34]),
    ("raw-single-frame-rpm", "raw", [0x02, 0x01, 0x0C], [0x04, 0x41, 0x0C, 0x0C, 0xB5, 0x55, 0x55, 0x55]),
    ("raw-single-frame-nrc", "raw", None, [0x03, 0x7F, 0x22, 0x33, 0x55, 0x55, 0x55, 0x55]),
    ("raw-first-frame", "raw", None, [0x10, 0x14, 0x49, 0x02, 0x01, 0x57, 0x42, 0x41]),
    ("raw-consecutive-frame", "raw", None, [0x21, 0x33, 0x41, 0x35, 0x43, 0x35, 0x31, 0x44]),
    ("raw-flow-control", "raw", [0x30, 0x00, 0x0A, 0x55, 0x55, 0x55, 0x55, 0x55], None),
    ("raw-flow-control-overflow", "raw", None, [0x32, 0x00, 0xF1]),
    ("raw-not-isotp", "raw", None, [0xDE, 0xAD, 0xBE, 0xEF]),
]

COMMENT = ("Golden fixtures shared by the Swift and Python annotators. Rendered lines are "
           "colorless; regenerate with python/ecuconnect_tool/scripts/"
           "generate_annotation_fixtures.py after reviewing the diff.")


def build() -> dict:
    cases = []
    for name, framing, request, response in CASES:
        annotator = MessageAnnotator(FRAMING_RAW_FRAMES if framing == "raw" else FRAMING_PDU)
        entry: dict = {"name": name, "framing": framing}
        if request is not None:
            entry["request"] = "".join(f"{byte:02X}" for byte in request)
            annotation = annotator.annotate_request(request)
            entry["expectedRequest"] = annotation_lines(annotation) if annotation else []
        if response is not None:
            entry["response"] = "".join(f"{byte:02X}" for byte in response)
            annotation = annotator.annotate_response(response)
            entry["expectedResponse"] = annotation_lines(annotation) if annotation else []
        cases.append(entry)
    return {"_comment": COMMENT, "cases": cases}


def main() -> int:
    target = Path(__file__).resolve().parents[3] / "Tests/Fixtures/annotations.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("w", encoding="utf-8") as handle:
        json.dump(build(), handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    print(f"wrote {len(CASES)} fixtures to {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
