from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest
from typer.testing import CliRunner
from ecuconnect_tool import cli
from ecuconnect_tool.health import HealthEventCursor, health_snapshot

FIXTURE = Path(__file__).resolve().parents[3] / "Tests/Fixtures/health.json"
runner = CliRunner()


class FakeClient:
    instances = []
    failure = None
    snapshot = None
    pages = []

    def __init__(self, **kwargs):
        self.calls = []
        self.closed = False
        self.connections = 0
        FakeClient.instances.append(self)

    def __enter__(self):
        self.connections += 1
        return self

    def __exit__(self, *args):
        self.closed = True

    def rpc_call(self, method, params=None, timeout=2):
        self.calls.append((method, copy.deepcopy(params)))
        if self.failure:
            raise self.failure
        if method == "system.health":
            return copy.deepcopy(self.snapshot)
        assert method == "system.health.events"
        return copy.deepcopy(self.pages.pop(0))


@pytest.fixture(autouse=True)
def client(monkeypatch):
    FakeClient.instances = []
    FakeClient.failure = None
    FakeClient.snapshot = json.loads(FIXTURE.read_text())
    FakeClient.pages = []
    monkeypatch.setattr(cli, "EcuconnectClient", FakeClient)
    monkeypatch.setattr(cli.time, "sleep", lambda interval: None)
    return FakeClient


def test_json_watch_has_no_banners_and_reuses_one_connection(client):
    client.snapshot["future_field"] = "kept"
    result = runner.invoke(cli.app, ["health", "--json", "--watch", "--count", "2"])
    assert result.exit_code == 0, result.output
    samples = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(samples) == 2
    assert samples[0]["firmware"] == "0.9.536"
    assert samples[0]["future_field"] == "kept"
    assert len(client.instances) == 1
    assert client.instances[0].connections == 1
    assert client.instances[0].closed
    assert client.instances[0].calls == [("system.health", None)] * 2


def test_summary_contains_release_acceptance_data(client):
    result = runner.invoke(cli.app, ["health"])
    assert result.exit_code == 0, result.output
    for text in ["90000 B", "PSRAM:", "ELF SHA-256:", "Allocation failures: 0", "Core dumps: 2", "reclaimed 70000 B"]:
        assert text in result.output
    assert client.instances[0].closed


@pytest.mark.parametrize("arguments", [
    ["--count", "2"], ["--watch", "--count", "0"], ["--interval", "nan"],
    ["--interval", "0"], ["--timeout", "inf"], ["--timeout", "-1"],
])
def test_invalid_watch_options_never_connect(arguments, client):
    result = runner.invoke(cli.app, ["health", *arguments])
    assert result.exit_code != 0
    assert not client.instances


def test_old_firmware_rpc_failure_is_clear_and_closes(client):
    client.failure = RuntimeError("RPC system.health failed: error_invalid_rpc")
    result = runner.invoke(cli.app, ["health", "--json"])
    assert result.exit_code == 1
    assert result.stdout == ""
    assert "error_invalid_rpc" in result.stderr
    assert client.instances[0].closed


def test_unsupported_schema_is_not_printed_as_valid_health(client):
    client.snapshot["schema"] = 2
    result = runner.invoke(cli.app, ["health", "--json"])
    assert result.exit_code == 1
    assert result.stdout == ""
    assert "Unsupported" in result.stderr
    assert client.instances[0].closed


def test_export_freezes_history_and_preserves_device_evidence(tmp_path, client):
    client.pages = [
        {"through": 10, "next": 8, "more": True, "events": [{"sequence": 8}]},
        {"through": 10, "next": 10, "more": False, "events": [{"sequence": 10}]},
    ]
    directory = tmp_path / "health"
    result = runner.invoke(cli.app, ["diagnostics", "export", "--output", str(directory)])
    assert result.exit_code == 0, result.output
    assert json.loads((directory / "manifest.json").read_text())["status"] == "complete"
    assert len(json.loads((directory / "events.json").read_text())) == 2
    assert client.instances[0].calls == [
        ("system.health", None),
        ("system.health.events", {"after": 0}),
        ("system.health.events", {"after": 8, "through": 10}),
    ]
    assert client.instances[0].closed


def test_incomplete_export_has_no_complete_manifest(tmp_path, client):
    client.pages = [{"through": 10, "next": 0, "more": True, "events": []}]
    directory = tmp_path / "health"
    result = runner.invoke(cli.app, ["diagnostics", "export", "--output", str(directory)])
    assert result.exit_code == 1
    assert not (directory / "manifest.json").exists()
    assert client.instances[0].closed


def test_export_never_overwrites_existing_results(tmp_path, client):
    directory = tmp_path / "existing"
    directory.mkdir()
    marker = directory / "keep.txt"
    marker.write_text("evidence")
    result = runner.invoke(cli.app, ["diagnostics", "export", "--output", str(directory)])
    assert result.exit_code != 0
    assert marker.read_text() == "evidence"
    assert not client.instances


def test_schema_and_cursor_reject_malformed_pages(client):
    sample = copy.deepcopy(client.snapshot)
    sample["memory"]["internal"]["free"] = True
    with pytest.raises(ValueError):
        health_snapshot(sample)
    cursor = HealthEventCursor()
    assert cursor.advance({"through": 10, "next": 8, "more": True, "events": []})
    for page in [
        {"through": 11, "next": 10, "more": False, "events": []},
        {"through": 10, "next": 8, "more": True, "events": []},
        {"through": 10, "next": 7, "more": False, "events": []},
    ]:
        with pytest.raises(ValueError):
            cursor.advance(page)
    assert not cursor.advance({"through": 10, "next": 10, "more": False, "events": []})
