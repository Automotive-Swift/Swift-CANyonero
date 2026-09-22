"""Replays `Tests/Fixtures/annotations.json` — the same fixtures the Swift
implementation is tested against, which is what keeps the two in step."""

from __future__ import annotations

import json
import os
from pathlib import Path

import pytest

os.environ.setdefault("NO_COLOR", "1")

from ecuconnect_tool.annotations import (  # noqa: E402
    FRAMING_PDU,
    FRAMING_RAW_FRAMES,
    MessageAnnotator,
    annotation_lines,
)
from ecuconnect_tool.annotations import converters  # noqa: E402

FIXTURES = Path(__file__).resolve().parents[3] / "Tests/Fixtures/annotations.json"


def load_cases():
    if not FIXTURES.is_file():
        pytest.skip(f"{FIXTURES} not available")
    return json.loads(FIXTURES.read_text(encoding="utf-8"))["cases"]


def annotator_for(framing: str) -> MessageAnnotator:
    return MessageAnnotator(FRAMING_RAW_FRAMES if framing == "raw" else FRAMING_PDU)


@pytest.mark.parametrize("case", load_cases(), ids=lambda case: case["name"])
def test_annotation_matches_fixture(case):
    annotator = annotator_for(case["framing"])

    if "request" in case:
        annotation = annotator.annotate_request(bytes.fromhex(case["request"]))
        lines = annotation_lines(annotation) if annotation else []
        assert lines == case["expectedRequest"]

    if "response" in case:
        annotation = annotator.annotate_response(bytes.fromhex(case["response"]))
        lines = annotation_lines(annotation) if annotation else []
        assert lines == case["expectedResponse"]


#: Converters whose Swift counterpart yields a rich typed value we deliberately
#: do not reproduce; the annotator falls back to naming the parameter.
UNIMPLEMENTED_CONVERTERS = {
    "monitorStatus",
    "noxSensorConcentration",
    "oxygenSensorMaxValues",
    "oxygenSensorPositions",
    "particulateMatterSensor",
    "performanceCounters",
}


def test_every_referenced_converter_is_known():
    """A converter added to the spec table must be implemented or listed above."""
    referenced = {spec[2] for spec in converters.MEASUREMENT_SPECS.values()}
    unhandled = referenced - converters.known_converters() - UNIMPLEMENTED_CONVERTERS
    assert not unhandled, f"unimplemented converters: {sorted(unhandled)}"
