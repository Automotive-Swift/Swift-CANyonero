from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


def health_snapshot(value: dict) -> dict:
    if value.get("error"):
        raise ValueError(f"system.health: {value['error']}")
    if type(value.get("schema")) is not int or value["schema"] != 1:
        raise ValueError("Unsupported system.health schema; expected schema 1.")
    try:
        for name in ("internal", "psram"):
            for field in ("free", "minimum", "largest"):
                amount = value["memory"][name][field]
                if type(amount) is not int or amount < 0:
                    raise ValueError(f"Invalid {name}.{field}")
        if not isinstance(value["serial"], str) or not isinstance(value["firmware"], str):
            raise ValueError("Invalid firmware identity")
        if type(value["uptime_ms"]) is not int or value["uptime_ms"] < 0:
            raise ValueError("Invalid uptime")
        radios = value["radios"]
        if type(radios["ble"]) is not bool or type(radios["network"]) is not bool:
            raise ValueError("Invalid radio state")
        if not isinstance(radios["owner"], str) or type(radios["restore_in_ms"]) is not int:
            raise ValueError("Invalid radio ownership")
    except (KeyError, TypeError) as exc:
        raise ValueError("Incomplete system.health response") from exc
    return value


def health_summary(value: dict) -> str:
    def size(amount: int) -> str:
        return f"{amount} B ({amount / 1024:.1f} KiB)"

    lines = [f"ECUconnect {value['serial']} · {value['firmware']} · uptime {value['uptime_ms'] / 1000:g} s"]
    for label, key in (("Internal heap", "internal"), ("PSRAM", "psram")):
        memory = value["memory"][key]
        lines.append(f"{label}: {size(memory['free'])} free; minimum {size(memory['minimum'])}; largest {size(memory['largest'])}")
    radios = value["radios"]
    lines.append(f"Radios: BLE {'on' if radios['ble'] else 'off'}, network {'on' if radios['network'] else 'off'}; owner {radios['owner']}; restore in {radios['restore_in_ms']} ms")
    if "elf_sha256" in value:
        lines.append(f"ELF SHA-256: {value['elf_sha256']}")
    details = [f"{label}: {value[key]}" for label, key in (("Voltage (V)", "voltage"), ("Tasks", "task_count"), ("Boot count", "boot_count"), ("Reset reason", "reset_reason"), ("Core dumps", "coredump_count")) if key in value]
    lines.extend(details)
    if "allocation_failures" in value:
        failures = value["allocation_failures"]
        lines.append(f"Allocation failures: {failures.get('count', 0)}; last {failures.get('last_bytes', 0)} bytes, caps {failures.get('last_caps', 0)}")
    for name in ("network", "ble"):
        stop = value.get("last_stops", {}).get(name)
        if stop:
            before = stop["before"]["internal"]
            after = stop["after"]["internal"]
            lines.append(f"Last {name} stop: {size(before['free'])} → {size(after['free'])}; reclaimed {size(stop['internal_reclaimed'])}")
            lines.append(f"  Largest block: {size(before['largest'])} → {size(after['largest'])}")
    return "\n".join(lines)


@dataclass
class HealthEventCursor:
    after: int = 0
    through: Optional[int] = None

    def advance(self, page: dict) -> bool:
        end, next_cursor, more = (page.get(key) for key in ("through", "next", "more"))
        if (type(end) is not int or type(next_cursor) is not int or type(more) is not bool
                or end < 0 or next_cursor < self.after or next_cursor > end
                or not isinstance(page.get("events"), list)
                or (self.through is not None and self.through != end)):
            raise ValueError("Invalid health history page.")
        if more and (next_cursor <= self.after or next_cursor >= end):
            raise ValueError("Health history cursor made no progress.")
        self.through, self.after = end, next_cursor
        return more


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
