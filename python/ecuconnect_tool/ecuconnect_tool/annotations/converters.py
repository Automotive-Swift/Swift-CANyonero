"""OBD2 measurement conversion, ported from `OBD2.MeasurementSpec.convert`.

The formulas are transcribed verbatim from Swift-Automotive; the converter
*names* come from the generated spec table, so a converter that exists there
but not here is caught by the test suite rather than silently returning a wrong
number. Converters whose Swift counterpart produces a rich typed value
(monitor status, performance counters, oxygen sensor positions) return `None`
here — the annotator then falls back to naming the parameter, which is honest.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Sequence, Union

from ._core_strings import CORE_STRINGS
from ._measurement_specs import MEASUREMENT_SPECS
from . import dtc as dtc_module


@dataclass(frozen=True)
class Value:
    """A converted measurement, ready for display."""
    text: str


@dataclass(frozen=True)
class Strings:
    values: Sequence[str]


@dataclass(frozen=True)
class PIDList:
    pids: Sequence[int]


@dataclass(frozen=True)
class TroubleCodeValue:
    code: str


Converted = Union[Value, Strings, PIDList, TroubleCodeValue]


def _format(value: float, unit: Optional[str]) -> str:
    text = f"{value:.3f}".rstrip("0").rstrip(".")
    if text in ("", "-"):
        text = "0"
    return f"{text} {unit}" if unit else text


def _u16(data: Sequence[int]) -> float:
    """The trailing two bytes, which is what the Swift converters read."""
    return float(data[-2] * 256 + data[-1])


#: (multiplier, offset, unit) for the unit-and-scaling ids the spec table uses.
_UAS = {
    "rotationalFrequency": (0.25, 0.0, "RPM"),
    "speedKmh01": (0.01, 0.0, "km/h"),
    "speedKmh1": (1.0, 0.0, "km/h"),
    "voltageVolts001": (0.001, 0.0, "V"),
    "voltageVolts01": (0.01, 0.0, "V"),
    "secondsPerBitUnsigned": (1.0, 0.0, "s"),
    "hundredMsPerBitUnsigned": (100.0, 0.0, "ms"),
    "distanceKilometers1": (1.0, 0.0, "km"),
    "pressureKilopascals1": (1.0, 0.0, "kPa"),
    "angleDegrees05": (0.5, 0.0, "°"),
    "counts1": (1.0, 0.0, "counts"),
}


def _convert_uas(uas_id: str, payload: Sequence[int]) -> Optional[Value]:
    if not payload:
        return None
    raw = float(payload[0]) if len(payload) == 1 else _u16(payload)

    if uas_id == "temperatureCelsius01":
        # A single byte is the OBD2 encoding, two bytes the J1979DA one.
        value = raw - 40.0 if len(payload) == 1 else (0.1 * raw) - 40.0
        return Value(_format(value, "°C"))
    if uas_id == "temperatureCelsius01Signed":
        signed = raw - 65536.0 if raw > 32767 else raw
        return Value(_format(0.1 * signed, "°C"))

    scaling = _UAS.get(uas_id)
    if scaling is None:
        return None
    multiplier, offset, unit = scaling
    return Value(_format(multiplier * raw + offset, unit))


def convert(service: int, pid: int, data: Sequence[int]) -> Optional[Converted]:
    """Convert the data record of an OBD2 response.

    `data` is everything after the response service id and the PID.
    """
    spec = MEASUREMENT_SPECS.get((service, pid))
    if spec is None:
        return None
    mnemonic, _length, converter, argument, unit = spec
    payload = list(data)
    if not payload and converter not in ("engineRunTimeTotals",):
        return None

    a = float(payload[0]) if payload else 0.0
    last = float(payload[-1]) if payload else 0.0
    has2 = len(payload) >= 2

    # --- textual ---------------------------------------------------------
    if converter == "ascii":
        text = "".join(chr(b) for b in payload if 0x08 < b < 0x80).strip()
        return Value(text) if text else None
    if converter == "multiAscii":
        parts, current = [], ""
        for byte in payload:
            if byte == 0x00:
                if current:
                    parts.append(current)
                current = ""
            elif 0x08 < byte < 0x80:
                current += chr(byte)
        if current:
            parts.append(current)
        return Strings(parts) if parts else None
    if converter == "hex":
        return Value(" ".join(f"{b:02X}" for b in payload))
    if converter == "multiHex":
        chunks = [payload[i:i + 4] for i in range(0, len(payload) - 3, 4)]
        return Strings([" ".join(f"{b:02X}" for b in chunk) for chunk in chunks]) if chunks else None
    if converter == "fuelSystemStatus":
        # PID 0x03 reports one status byte per fuel system; the wording matches
        # `OBD2.FuelSystemStatus.description` in Swift-Automotive.
        if not has2:
            return None
        def loop_state(byte: int) -> str:
            return CORE_STRINGS.get(f"OBD2_FUEL_SYSTEM_STATUS_{byte:02X}",
                                    CORE_STRINGS["OBD2_FUEL_SYSTEM_STATUS_00"])
        return Value(f"System 1: {loop_state(payload[-2])}; System 2: {loop_state(payload[-1])}")
    if converter == "secondaryAirStatus":
        key = f"OBD2_SECONDARY_AIR_STATUS_{int(last):02X}"
        return Value(CORE_STRINGS[key]) if key in CORE_STRINGS else None
    if converter == "auxiliaryInputStatus":
        return Value("Power Take Off active" if int(last) & 0x01 else "Power Take Off inactive")
    if converter == "localized":
        return Value(CORE_STRINGS.get(f"OBD2_{mnemonic}_{int(last):02X}", f"0x{int(last):02X}"))

    # --- structured ------------------------------------------------------
    if converter == "dtc":
        if not has2:
            return None
        code = dtc_module.decode(payload[-2:])
        return TroubleCodeValue(code.code) if code else TroubleCodeValue("P0000")
    if converter == "pids":
        if len(payload) < 4:
            return None
        window = payload[-4:]
        bits = (window[0] << 24) | (window[1] << 16) | (window[2] << 8) | window[3]
        base = int(argument or 0)
        pids = [base + index + 1 for index in range(32)
                if bits & (1 << (31 - index)) and base + index + 1 <= 0xFF]
        return PIDList(pids) if pids else None

    # --- scalars ---------------------------------------------------------
    if converter == "uas":
        return _convert_uas(str(argument), payload)
    if converter == "uint8":
        return Value(_format(last, unit))
    if converter in ("engineLoad", "percentSingleByte", "alcoholPercent"):
        return Value(_format(last * 100.0 / 255.0, "%"))
    if converter == "fuelTrim":
        return Value(_format((last - 128.0) * 100.0 / 128.0, "%"))
    if converter == "fuelPressure":
        return Value(_format(last * 3.0, "kPa"))
    if converter == "timingAdvance":
        return Value(_format((last - 128.0) / 2.0, "°"))
    if converter == "throttlePosition":
        # Multi-byte PIDs encode the commanded position in the first data byte.
        source = a if len(payload) >= 2 else last
        return Value(_format(source * 100.0 / 255.0, "%"))
    if converter in ("barometricPressure",):
        return Value(_format(last, "kPa"))
    if converter in ("engineOilTemp", "temperature"):
        return Value(_format(last - 40.0, "°C"))
    if converter in ("torquePercent",):
        return Value(_format(last - 125.0, "%"))
    if converter == "transmissionGear":
        return Value("Park/Neutral" if last == 0 else f"gear {int(last)}")
    if converter == "mafMaxValue":
        return Value(_format(last * 10.0, "g/s"))
    if converter == "commandedEGR":
        return Value(_format(a * 100.0 / 255.0, "%")) if has2 else None
    if converter == "oxygenSensorVoltage":
        # Byte A is the voltage, byte B the short term fuel trim.
        return Value(_format(payload[-2] / 200.0, "V")) if has2 else None

    if not has2:
        return None
    u16 = _u16(payload)

    if converter in ("manifoldAirFlow", "massAirFlowSensor"):
        return Value(_format(u16 / 100.0, "g/s"))
    if converter == "fuelRailPressure":
        return Value(_format(u16 * 10.0, "kPa"))
    if converter in ("vaporPressure", "evapVaporPressureWide"):
        return Value(_format(u16 / 4.0, "Pa"))
    if converter == "evapPressure":
        return Value(_format(u16, "Pa"))
    if converter == "absoluteLoad":
        return Value(_format(u16 * 100.0 / 255.0, "%"))
    if converter == "airFuelRatio":
        return Value(_format(u16 / 32768.0, None))
    if converter == "referenceTorque":
        return Value(_format(u16, "Nm"))
    if converter == "fuelInjectionTiming":
        return Value(_format((u16 - 26880.0) / 128.0, "°"))
    if converter == "fuelRate":
        return Value(_format(u16 * 0.05, "L/h"))
    if converter == "turboRPM":
        return Value(_format(u16 * 10.0, "RPM"))
    if converter in ("turboTemp", "catalystTemp"):
        return Value(_format(u16 / 10.0 - 40.0, "°C"))
    if converter in ("exhaustTemp", "egt"):
        return Value(_format(u16 * 0.1 - 40.0, "°C"))
    if converter == "particulateFilter":
        return Value(_format(u16 * 0.01, "kPa"))
    if converter == "engineRunTime":
        return Value(_format(u16, "min"))
    if converter == "odometer":
        if len(payload) < 4:
            return None
        window = payload[-4:]
        value = ((window[0] * 16777216.0) + (window[1] * 65536.0) + (window[2] * 256.0) + window[3]) * 0.1
        return Value(_format(value, "km"))
    if converter == "wideRangeO2Lambda":
        if len(payload) < 4:
            return None
        lambda_value = ((256.0 * payload[0]) + payload[1]) * 0.0000305
        current = (((256.0 * payload[2]) + payload[3]) * 0.00390625) - 128.0
        return Value(f"λ {lambda_value:.4f}, {current:.2f} mA")
    if converter == "engineTorqueData":
        return Value(" / ".join(f"{byte - 125:.0f}%" for byte in payload))
    if converter == "engineRunTimeTotals":
        support = payload[0]
        parts, index = [], 1
        for bit, label in ((0x01, "total"), (0x02, "idle"), (0x04, "pto")):
            if support & bit and len(payload) >= index + 4:
                window = payload[index:index + 4]
                seconds = (window[0] << 24) | (window[1] << 16) | (window[2] << 8) | window[3]
                parts.append(f"{label}={seconds} s ({seconds / 3600.0:.1f} h)")
                index += 4
        return Value(", ".join(parts) if parts else f"support=0x{support:02X} (no totals)")

    return None


def known_converters() -> set[str]:
    """Every converter name this module handles — used by the test suite."""
    return {
        "ascii", "multiAscii", "hex", "multiHex", "localized", "dtc", "pids", "uas", "uint8",
        "engineLoad", "percentSingleByte", "alcoholPercent", "fuelTrim", "fuelPressure",
        "timingAdvance", "throttlePosition", "barometricPressure", "engineOilTemp", "temperature",
        "torquePercent", "transmissionGear", "mafMaxValue", "commandedEGR", "oxygenSensorVoltage",
        "manifoldAirFlow", "massAirFlowSensor", "fuelRailPressure", "vaporPressure",
        "evapVaporPressureWide", "evapPressure", "absoluteLoad", "airFuelRatio", "referenceTorque",
        "fuelInjectionTiming", "fuelRate", "turboRPM", "turboTemp", "catalystTemp", "exhaustTemp",
        "egt", "particulateFilter", "engineRunTime", "odometer", "wideRangeO2Lambda",
        "engineTorqueData", "engineRunTimeTotals", "fuelSystemStatus", "secondaryAirStatus",
        "auxiliaryInputStatus",
    }
