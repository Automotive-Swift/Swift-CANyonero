"""The firmware update flow.

Flashing is the one command where a bug is expensive and cannot be observed in
a dry run, so the PDU sequence and the chunking are pinned here.
"""

from __future__ import annotations

import pytest
from typer.testing import CliRunner

from ecuconnect_tool import cli as cli_module
from ecuconnect_tool.cli import app

runner = CliRunner()


class FakeInfo:
    """Stands in for `canyonero.Info`, which the binding builds field by field."""

    vendor = "ACME"
    model = "ECUconnect"
    hardware = "rev1"
    serial = "SN-1"
    firmware = "1.2.3"


class FakeClient:
    """Records the calls the update command makes, in order."""

    instances: list["FakeClient"] = []

    def __init__(self, **kwargs):
        self.calls: list[tuple] = []
        self.chunks: list[bytes] = []
        self.closed = False
        self.failure: Exception | None = None
        FakeClient.instances.append(self)

    # -- context manager --
    def __enter__(self):
        self.calls.append(("enter",))
        return self

    def __exit__(self, exc_type, exc, tb):
        self.closed = True

    def close(self):
        self.closed = True

    # -- the calls the command makes --
    def connect(self):
        # `_connect_with_spinner` must run before the context is entered, the
        # same way the other long-running commands do it.
        assert not self.closed
        self.calls.append(("connect",))

    def request_info(self, timeout: float = 2.0):
        self.calls.append(("request_info",))
        return FakeInfo()

    def read_voltage(self, timeout: float = 2.0) -> float:
        self.calls.append(("read_voltage",))
        return 12.6

    def prepare_update(self, timeout: float = 10.0):
        self.calls.append(("prepare_update",))
        if self.failure:
            raise self.failure

    def send_update_data(self, data: bytes, timeout: float = 10.0):
        self.calls.append(("send_update_data", len(data)))
        self.chunks.append(bytes(data))

    def commit_update(self, timeout: float = 10.0):
        self.calls.append(("commit_update",))

    def reset(self, timeout: float = 10.0):
        self.calls.append(("reset",))


@pytest.fixture(autouse=True)
def fake_client(monkeypatch):
    FakeClient.instances = []
    monkeypatch.setattr(cli_module, "EcuconnectClient", FakeClient)
    monkeypatch.setattr(cli_module, "_connect_with_spinner", lambda client, endpoint: client.connect())
    monkeypatch.setattr(cli_module.time, "sleep", lambda _seconds: None)
    return FakeClient


def write_image(tmp_path, size: int):
    path = tmp_path / "firmware.bin"
    path.write_bytes(bytes(index % 256 for index in range(size)))
    return path


def test_update_sends_the_expected_pdu_sequence(tmp_path):
    image = write_image(tmp_path, 10_000)
    result = runner.invoke(app, ["update", str(image), "--chunk-size", "4000"])

    assert result.exit_code == 0, result.output
    flash, reconnect = FakeClient.instances
    assert [call[0] for call in flash.calls] == [
        "connect", "enter", "request_info", "read_voltage", "prepare_update",
        "send_update_data", "send_update_data", "send_update_data",
        "commit_update", "reset",
    ]
    assert [call[0] for call in reconnect.calls] == ["connect", "enter", "request_info"]
    assert flash.closed and reconnect.closed


def test_update_chunks_cover_the_image_exactly(tmp_path):
    image = write_image(tmp_path, 10_000)
    result = runner.invoke(app, ["update", str(image), "--chunk-size", "4000"])

    assert result.exit_code == 0, result.output
    flash = FakeClient.instances[0]
    assert [len(chunk) for chunk in flash.chunks] == [4000, 4000, 2000]
    assert b"".join(flash.chunks) == image.read_bytes()


def test_update_handles_an_image_smaller_than_one_chunk(tmp_path):
    image = write_image(tmp_path, 7)
    result = runner.invoke(app, ["update", str(image), "--chunk-size", "4000"])

    assert result.exit_code == 0, result.output
    assert [len(chunk) for chunk in FakeClient.instances[0].chunks] == [7]


def test_update_refuses_a_missing_file(tmp_path):
    result = runner.invoke(app, ["update", str(tmp_path / "nope.bin")])
    assert result.exit_code == 1
    assert "Can't find file" in result.output
    assert FakeClient.instances == []


def test_update_refuses_an_empty_file(tmp_path):
    image = tmp_path / "empty.bin"
    image.write_bytes(b"")
    result = runner.invoke(app, ["update", str(image)])
    assert result.exit_code == 1
    assert "is empty" in result.output
    assert FakeClient.instances == []


def test_update_reports_a_refusal_instead_of_flashing(tmp_path, monkeypatch):
    image = write_image(tmp_path, 100)

    class RefusingClient(FakeClient):
        def prepare_update(self, timeout: float = 10.0):
            self.calls.append(("prepare_update",))
            raise RuntimeError("Adapter did not prepare the firmware update: PDUType.error_hardware")

    monkeypatch.setattr(cli_module, "EcuconnectClient", RefusingClient)
    result = runner.invoke(app, ["update", str(image)])

    assert result.exit_code == 1
    assert "Firmware update failed" in result.output
    assert not FakeClient.instances[0].chunks
