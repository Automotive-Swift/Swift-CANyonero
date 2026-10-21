from __future__ import annotations

import io
import sys

import pytest
import typer

from ecuconnect_tool import cli
from ecuconnect_tool import macos_l2cap


class TerminalInput(io.StringIO):
    def isatty(self) -> bool:
        return True


def test_single_ble_adapter_is_selected(monkeypatch):
    monkeypatch.setattr(cli.sys, "platform", "darwin")
    monkeypatch.setattr(macos_l2cap, "discover_devices", lambda service, timeout: [("12345678-1234-1234-1234-123456789ABC", "ECUconnect")])
    assert cli._resolve_ble_endpoint("ecuconnect-l2cap://FFF1:129") == (
        "ecuconnect-l2cap://FFF1:129/12345678-1234-1234-1234-123456789ABC"
    )


def test_multiple_ble_adapters_prompt_for_selection(monkeypatch):
    monkeypatch.setattr(cli.sys, "platform", "darwin")
    monkeypatch.setattr(macos_l2cap, "discover_devices", lambda service, timeout: [
        ("12345678-1234-1234-1234-123456789ABC", "First"),
        ("87654321-4321-4321-4321-CBA987654321", "Second"),
    ])
    monkeypatch.setattr(sys, "stdin", TerminalInput("2\n"))
    assert cli._resolve_ble_endpoint("ecuconnect-l2cap://FFF1:129").endswith(
        "/87654321-4321-4321-4321-CBA987654321"
    )


def test_multiple_ble_adapters_need_explicit_peer_without_terminal(monkeypatch):
    monkeypatch.setattr(cli.sys, "platform", "darwin")
    monkeypatch.setattr(macos_l2cap, "discover_devices", lambda service, timeout: [("a", "First"), ("b", "Second")])
    monkeypatch.setattr(sys, "stdin", io.StringIO())
    with pytest.raises(typer.BadParameter, match="Multiple BLE adapters"):
        cli._resolve_ble_endpoint("ecuconnect-l2cap://FFF1:129")


def test_explicit_peer_does_not_scan(monkeypatch):
    monkeypatch.setattr(macos_l2cap, "discover_devices", lambda service, timeout: pytest.fail("unexpected scan"))
    endpoint = "ecuconnect-l2cap://FFF1:129/12345678-1234-1234-1234-123456789ABC"
    assert cli._resolve_ble_endpoint(endpoint) == endpoint
