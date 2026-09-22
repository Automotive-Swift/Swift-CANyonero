"""ISO 15765-2 protocol control information of a single CAN frame.

Only needed on raw CAN channels, where the adapter hands us individual frames
instead of a reassembled PDU. Extended (per-frame) addressing is not recognized
— with no channel configuration to tell us about it, guessing would produce
confident nonsense.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Sequence, Union


@dataclass(frozen=True)
class SingleFrame:
    payload: Sequence[int]


@dataclass(frozen=True)
class FirstFrame:
    total_length: int
    payload: Sequence[int]


@dataclass(frozen=True)
class ConsecutiveFrame:
    sequence: int
    payload: Sequence[int]


@dataclass(frozen=True)
class FlowControlFrame:
    state: int            # 0 clear to send, 1 wait, 2 overflow
    block_size: int
    separation_time: int


Frame = Union[SingleFrame, FirstFrame, ConsecutiveFrame, FlowControlFrame]

FLOW_STATES = {0: "clear to send", 1: "wait", 2: "overflow — abort"}


def parse(data: Sequence[int]) -> Optional[Frame]:
    if not data:
        return None
    pci = data[0]
    kind = pci >> 4

    if kind == 0x0:
        length = pci & 0x0F
        if not 1 <= length <= 7 or len(data) < length + 1:
            return None
        return SingleFrame(payload=list(data[1:1 + length]))
    if kind == 0x1:
        if len(data) < 3:
            return None
        return FirstFrame(total_length=((pci & 0x0F) << 8) | data[1], payload=list(data[2:]))
    if kind == 0x2:
        if len(data) < 2:
            return None
        return ConsecutiveFrame(sequence=pci & 0x0F, payload=list(data[1:]))
    if kind == 0x3:
        if len(data) < 3 or (pci & 0x0F) not in FLOW_STATES:
            return None
        return FlowControlFrame(state=pci & 0x0F, block_size=data[1], separation_time=data[2])
    return None


def separation_time_description(value: int) -> str:
    """Separation time as encoded by ISO 15765-2."""
    if 0x00 <= value <= 0x7F:
        return f"{value} ms"
    if 0xF1 <= value <= 0xF9:
        return f"{(value - 0xF0) * 100} µs"
    return f"reserved (0x{value:02X})"
