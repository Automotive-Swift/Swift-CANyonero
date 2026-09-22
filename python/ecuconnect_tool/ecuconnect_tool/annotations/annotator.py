"""Turns request and response PDUs into human-readable annotations.

A port of `Sources/ecuconnect-tool/MessageAnnotator.swift`; the two are kept in
step by the shared golden fixtures in `tests/annotation_fixtures.json`.
"""

from __future__ import annotations

from typing import List, Optional, Sequence

from . import converters, dtc as dtc_module, isotp, names
from ._pid_names import PID_NAMES
from .model import Annotation, DTC, Detail, Field, Measurement, Note, Severity

#: How much unwrapping a payload needs before it is a diagnostic PDU.
FRAMING_PDU = "pdu"
FRAMING_RAW_FRAMES = "rawFrames"


class MessageAnnotator:

    def __init__(self, framing: str = FRAMING_PDU) -> None:
        self.framing = framing

    # -- requests --------------------------------------------------------

    def annotate_request(self, data: Sequence[int]) -> Optional[Annotation]:
        if self.framing != FRAMING_PDU:
            return self._annotate_frame(data, Severity.REQUEST)
        return self._annotate_pdu_request(data)

    def _annotate_pdu_request(self, data: Sequence[int]) -> Optional[Annotation]:
        if not data:
            return None
        sid = data[0]
        parameters = list(data[1:])
        details = (self._obd2_request_details(sid, parameters)
                   if names.is_obd2_service(sid)
                   else self._uds_request_details(sid, parameters))
        return Annotation(
            severity=Severity.REQUEST,
            headline=f"{sid:02X} {names.service_name(sid)}",
            details=details,
        )

    @staticmethod
    def _obd2_request_details(sid: int, parameters: List[int]) -> List[Detail]:
        if sid == 0x01:
            # A single request may ask for up to six PIDs at once.
            return [Field(f"PID {pid:02X}", pid_name(pid)) for pid in parameters]
        if sid == 0x02:
            if not parameters:
                return []
            details: List[Detail] = [Field(f"PID {parameters[0]:02X}", pid_name(parameters[0]))]
            if len(parameters) >= 2:
                details.append(Field("Frame", f"{parameters[1]:02X}"))
            return details
        if sid == 0x06:
            return [Field("Test ID", f"{tid:02X}") for tid in parameters]
        if sid == 0x09:
            return [Field(f"Info type {t:02X}", names.vehicle_information_name(t)) for t in parameters]
        return []

    @staticmethod
    def _uds_request_details(sid: int, parameters: List[int]) -> List[Detail]:
        details: List[Detail] = []

        if sid == 0x22:
            # Read data by identifier accepts a list of two-byte DIDs.
            for index in range(0, len(parameters) - 1, 2):
                details.append(_data_identifier_detail((parameters[index] << 8) | parameters[index + 1]))
        elif sid == 0x2E:
            if len(parameters) >= 2:
                details.append(_data_identifier_detail((parameters[0] << 8) | parameters[1]))
                record = parameters[2:]
                if record:
                    details.append(Field("Value", _rendered_record(record)))
        elif sid == 0x27:
            if parameters:
                sub = parameters[0] & 0x7F
                step = "Send key" if sub % 2 == 0 else "Request seed"
                details.append(Field("Security access", f"{step}, level {(sub + 1) // 2}"))
        elif sid == 0x31:
            if parameters:
                name = names.sub_function_name(sid, parameters[0])
                if name:
                    details.append(Field("Sub-function", name))
                if len(parameters) >= 3:
                    details.append(Field("Routine", f"0x{(parameters[1] << 8) | parameters[2]:04X}"))
        elif sid in (0x34, 0x35):
            if len(parameters) >= 2:
                details.append(Field("Data format", f"{parameters[0]:02X}"))
                details.append(Field("Address/length format", f"{parameters[1]:02X}"))
        elif sid == 0x36:
            if parameters:
                details.append(Field("Block sequence counter", str(parameters[0])))
        else:
            if parameters:
                name = names.sub_function_name(sid, parameters[0])
                if name:
                    details.append(Field("Sub-function", name))

        # The suppression bit lives in the sub-function byte of every service that has one.
        if parameters and parameters[0] & 0x80 and sid in names.SERVICES_WITH_SUB_FUNCTION:
            details.append(Note("positive response suppressed"))

        return details

    # -- responses -------------------------------------------------------

    def annotate_response(self, data: Sequence[int]) -> Optional[Annotation]:
        if self.framing != FRAMING_PDU:
            return self._annotate_frame(data, Severity.OK)
        return self._annotate_pdu_response(data)

    def _annotate_pdu_response(self, data: Sequence[int]) -> Optional[Annotation]:
        if not data:
            return None
        response_sid = data[0]

        if response_sid == names.NEGATIVE_RESPONSE:
            return _negative_response_annotation(data)

        if not response_sid & 0x40:
            return Annotation(
                severity=Severity.WARNING,
                headline="Unrecognized payload",
                details=[Note(f"first byte {response_sid:02X} is neither a positive "
                              f"nor a negative response")],
            )

        sid = response_sid & ~0x40
        details = (self._obd2_response_details(sid, list(data))
                   if names.is_obd2_service(sid)
                   else self._uds_response_details(sid, list(data)))
        return Annotation(
            severity=Severity.OK,
            headline=f"{response_sid:02X} {names.service_name(sid)}",
            details=details,
        )

    @staticmethod
    def _obd2_response_details(sid: int, data: List[int]) -> List[Detail]:
        if sid in (0x03, 0x07, 0x0A):
            # CAN protocols prefix a DTC count byte, which makes the length even.
            start = 1 if len(data) % 2 else 2
            codes = [dtc_module.decode(pair) for pair in dtc_module.chunked(data[start:], 2)]
            return _dtc_details([code for code in codes if code], "no trouble codes reported")
        if sid == 0x04:
            return [Note("trouble codes and freeze frame data cleared")]
        if sid in (0x01, 0x02, 0x06, 0x09):
            return _measurement_details(sid, data)
        return []

    @staticmethod
    def _uds_response_details(sid: int, data: List[int]) -> List[Detail]:
        details: List[Detail] = []

        if sid == 0x19:
            if len(data) < 2:
                return details
            report_type = data[1]
            name = names.sub_function_name(sid, report_type)
            if name:
                details.append(Field("Report type", name))
            details.extend(_read_dtc_information_details(report_type, data))
        elif sid == 0x22:
            if len(data) >= 3:
                details.append(_data_identifier_detail((data[1] << 8) | data[2]))
                record = data[3:]
                if record:
                    details.append(Measurement("Value", _rendered_record(record)))
        elif sid == 0x2E:
            if len(data) >= 3:
                details.append(_data_identifier_detail((data[1] << 8) | data[2]))
                details.append(Note("written"))
        elif sid == 0x10:
            if len(data) >= 2:
                name = names.sub_function_name(sid, data[1])
                if name:
                    details.append(Field("Session", name))
            if len(data) >= 6:
                p2 = (data[2] << 8) | data[3]
                p2_star = ((data[4] << 8) | data[5]) * 10
                details.append(Field("Timings", f"P2 = {p2} ms, P2* = {p2_star} ms"))
        elif sid == 0x27:
            if len(data) >= 2:
                sub = data[1]
                level = (sub + 1) // 2
                if sub % 2 == 0:
                    details.append(Field("Security access", f"key accepted, level {level}"))
                else:
                    details.append(Field("Security access", f"seed for level {level}"))
                    seed = data[2:]
                    if seed:
                        details.append(Field("Seed", " ".join(f"{b:02X}" for b in seed)))
        elif sid == 0x31:
            if len(data) >= 4:
                name = names.sub_function_name(sid, data[1])
                if name:
                    details.append(Field("Sub-function", name))
                details.append(Field("Routine", f"0x{(data[2] << 8) | data[3]:04X}"))
                status = data[4:]
                if status:
                    details.append(Field("Status", " ".join(f"{b:02X}" for b in status)))
        elif sid in (0x34, 0x35):
            if len(data) >= 3:
                length_format = data[1] >> 4
                if length_format and len(data) >= 2 + length_format:
                    block_length = 0
                    for byte in data[2:2 + length_format]:
                        block_length = (block_length << 8) | byte
                    details.append(Field("Max block length", f"{block_length} bytes"))
        elif sid == 0x36:
            if len(data) >= 2:
                details.append(Field("Block sequence counter", str(data[1])))
        else:
            if len(data) >= 2:
                name = names.sub_function_name(sid, data[1])
                if name:
                    details.append(Field("Sub-function", name))

        return details

    # -- raw CAN frames --------------------------------------------------

    def _annotate_frame(self, data: Sequence[int], severity: Severity) -> Optional[Annotation]:
        frame = isotp.parse(data)
        if frame is None:
            return None

        if isinstance(frame, isotp.SingleFrame):
            # A single frame carries the whole PDU, so decode it as usual.
            pdu = MessageAnnotator(FRAMING_PDU)
            annotation = (pdu.annotate_request(frame.payload) if severity is Severity.REQUEST
                          else pdu.annotate_response(frame.payload))
            if annotation is None:
                return None
            return Annotation(
                severity=annotation.severity,
                headline=annotation.headline,
                details=list(annotation.details) + [Note(f"ISO-TP single frame, {len(frame.payload)} byte(s)")],
            )

        if isinstance(frame, isotp.FirstFrame):
            details: List[Detail] = [Field("Total length", f"{frame.total_length} bytes")]
            if frame.payload:
                sid = frame.payload[0]
                details.append(Field("Service", f"{sid:02X} {names.service_name(sid & ~0x40)}"))
            details.append(Note("waiting for consecutive frames"))
            return Annotation(severity=severity, headline="ISO-TP first frame", details=details)

        if isinstance(frame, isotp.ConsecutiveFrame):
            return Annotation(
                severity=severity,
                headline=f"ISO-TP consecutive frame #{frame.sequence}",
                details=[Note(f"{len(frame.payload)} byte(s)")],
            )

        return Annotation(
            severity=Severity.ERROR if frame.state == 2 else severity,
            headline=f"ISO-TP flow control ({isotp.FLOW_STATES[frame.state]})",
            details=[
                Field("Block size", "unlimited" if frame.block_size == 0 else str(frame.block_size)),
                Field("Separation time", isotp.separation_time_description(frame.separation_time)),
            ],
        )


# -- shared helpers -------------------------------------------------------

def pid_name(pid: int) -> str:
    return PID_NAMES.get(pid, f"PID 0x{pid:02X}")


def _negative_response_annotation(data: Sequence[int]) -> Annotation:
    if len(data) < 3:
        return Annotation(severity=Severity.ERROR, headline="Negative response (truncated)")
    service, nrc = data[1], data[2]
    pending = nrc == names.RESPONSE_PENDING
    return Annotation(
        severity=Severity.PENDING if pending else Severity.ERROR,
        headline="Response pending" if pending else "Negative response",
        details=[
            Field("Service", f"{service:02X} {names.service_name(service)}"),
            Field(f"NRC {nrc:02X}", names.negative_response_name(nrc)),
        ],
    )


def _measurement_details(sid: int, data: List[int]) -> List[Detail]:
    if len(data) < 2:
        return []
    pid = data[1]
    if sid == 0x09:
        label = names.vehicle_information_name(pid)
    elif sid == 0x06:
        label = f"Test {pid:02X}"
    else:
        label = pid_name(pid)

    # Freeze frame responses carry an extra frame number before the data.
    data_offset = 3 if data[0] == 0x42 else 2
    converted = converters.convert(sid, pid, data[data_offset:])

    if isinstance(converted, converters.Value):
        return [Measurement(label, converted.text)]
    if isinstance(converted, converters.Strings):
        return [Measurement(label, value) for value in converted.values]
    if isinstance(converted, converters.PIDList):
        return [Field(label, " ".join(f"{p:02X}" for p in converted.pids))]
    if isinstance(converted, converters.TroubleCodeValue):
        code = dtc_module.TroubleCode(converted.code)
        return [DTC(code.code, dtc_module.explanation(code), ())]

    return [Field(f"Parameter {pid:02X}", label)]


def _read_dtc_information_details(report_type: int, data: List[int]) -> List[Detail]:
    """Only the report types that carry DTC records in a self-describing
    layout are decoded; the rest stay raw."""
    if report_type in (0x01, 0x07, 0x11, 0x12):
        if len(data) < 6:
            return []
        return [Field("Matching DTCs", str((data[4] << 8) | data[5]))]
    if report_type in (0x02, 0x0A, 0x15, 0x17):
        records = data[3:]   # skip 59, sub-function, statusAvailabilityMask
        codes = [dtc_module.decode(record) for record in dtc_module.chunked(records, 4)]
        return _dtc_details([code for code in codes if code], "no trouble codes reported")
    return []


def _dtc_details(codes, empty_note: Optional[str]) -> List[Detail]:
    if not codes:
        return [Note(empty_note)] if empty_note else []
    return [DTC(code.code, dtc_module.explanation(code), names.status_flags(code.status))
            for code in codes]


def _data_identifier_detail(did: int) -> Detail:
    label = f"DID 0x{did:04X}"
    name = names.data_identifier_name(did)
    return Field(label, name if name else "vendor-specific")


def _rendered_record(data: Sequence[int]) -> str:
    """Prefer a readable string over hex when the record is plausible ASCII."""
    printable = all(0x20 <= b <= 0x7E or b == 0x00 for b in data)
    if printable and any(0x20 <= b <= 0x7E for b in data):
        text = "".join(chr(b) for b in data if b != 0x00).strip()
        if text:
            return f'"{text}"'
    return " ".join(f"{b:02X}" for b in data)
