import hashlib
import json
import os
import sys
from pathlib import Path

import pytest

from volt.cli import CliError
from volt.cli import _ngspice


def _executable(tmp_path: Path, run_body: str, *, version_body: str | None = None) -> Path:
    if os.name == "nt":
        pytest.skip("fake executable uses a POSIX shebang")
    directory = tmp_path / "fake ngspice installation"
    directory.mkdir(exist_ok=True)
    path = directory / "ngspice"
    version = version_body or 'print("** ngspice-46 : Circuit level simulation program")'
    path.write_text(
        f"""#!{sys.executable}
import os
from pathlib import Path
import sys
import time

if "--version" in sys.argv:
    {version}
    raise SystemExit(0)

{run_body}
""",
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def test_runs_exact_bounded_argument_vector_in_private_clean_environment(tmp_path, monkeypatch):
    executable = _executable(
        tmp_path,
        """assert sys.argv[1:] == ["-n", "deck.cir"]
assert os.environ["HOME"] == os.getcwd()
assert os.environ["LC_ALL"] == "C"
assert "VOLT_POISON_NGSPICE" not in os.environ
configuration = Path(os.environ["SPICE_SCRIPTS"])
assert configuration == Path.cwd() / "configuration"
assert Path(os.environ["SPICE_USERINIT_DIR"]) == configuration
assert Path(os.environ["NGSPICE_INPUT_DIR"]) == configuration
assert Path(os.environ["NGSPICE_OSDI_DIR"]) == configuration
assert (configuration / "spinit").read_bytes() == b""
assert Path("deck.cir").read_text() == "test deck\\n"
print(os.getcwd())
print("bounded stderr", file=sys.stderr)
Path("volt-dc-output.txt").write_bytes(b"native parser input")
""",
    )
    monkeypatch.setenv("VOLT_POISON_NGSPICE", "must not be inherited")

    output, evidence = _ngspice.run_ngspice(executable, "test deck\n")

    assert output == b"native parser input"
    assert evidence["format"] == "volt.ngspice-process-evidence"
    assert evidence["schema_version"] == 1
    assert evidence["version"] == "ngspice-46"
    assert evidence["version_command"] == [str(executable.resolve()), "-n", "--version"]
    assert evidence["command"] == [str(executable.resolve()), "-n", "deck.cir"]
    assert evidence["exit_code"] == 0
    assert evidence["stderr"] == "bounded stderr\n"
    assert evidence["output_filename"] == "volt-dc-output.txt"
    assert evidence["output_byte_size"] == len(output)
    assert evidence["output_content_digest"] == "sha256:" + hashlib.sha256(output).hexdigest()
    private_workdir = Path(str(evidence["stdout"]).strip())
    assert private_workdir.name.startswith("volt-ngspice-")
    assert not private_workdir.exists()
    json.dumps(evidence)


@pytest.mark.parametrize(
    "version_body",
    [
        'print("** ngspice-45 : Circuit level simulation program")',
        'print("ngspice 46-ish")',
        'print("totally unrelated program")',
    ],
)
def test_rejects_wrong_or_unrecognized_versions(tmp_path, version_body):
    executable = _executable(
        tmp_path,
        'raise AssertionError("run must not start")',
        version_body=version_body,
    )

    with pytest.raises(CliError) as error:
        _ngspice.run_ngspice(executable, "deck")

    assert error.value.code == "unsupported-ngspice-version"


def test_nonzero_version_check_retains_bounded_console_evidence(tmp_path):
    executable = _executable(
        tmp_path,
        'raise AssertionError("run must not start")',
        version_body='print("version failed", file=sys.stderr); raise SystemExit(3)',
    )

    with pytest.raises(CliError) as error:
        _ngspice.run_ngspice(executable, "deck")

    assert error.value.code == "ngspice-version-failed"
    assert "version failed" in str(error.value)


def test_rejects_missing_path(tmp_path):
    with pytest.raises(CliError) as missing:
        _ngspice.run_ngspice(tmp_path / "missing", "deck")
    assert missing.value.code == "ngspice-not-found"


def test_rejects_nonexecutable_path(tmp_path):
    executable = _executable(
        tmp_path, 'Path("volt-dc-output.txt").write_bytes(b"data")'
    )
    executable.chmod(0o644)
    with pytest.raises(CliError) as denied:
        _ngspice.run_ngspice(executable, "deck")
    assert denied.value.code == "ngspice-not-executable"


def test_nonzero_exit_retains_bounded_evidence_in_error(tmp_path):
    executable = _executable(
        tmp_path,
        'print("run stdout"); print("run stderr", file=sys.stderr); raise SystemExit(7)',
    )

    with pytest.raises(CliError) as error:
        _ngspice.run_ngspice(executable, "deck")

    assert error.value.code == "ngspice-execution-failed"
    assert '"exit_code":7' in str(error.value)
    assert "run stdout" in str(error.value)
    assert "run stderr" in str(error.value)


def test_timeout_terminates_and_reaps_owned_process(tmp_path, monkeypatch):
    executable = _executable(tmp_path, "time.sleep(10)")
    monkeypatch.setattr(_ngspice, "RUN_TIMEOUT_SECONDS", 0.05)

    with pytest.raises(CliError) as error:
        _ngspice.run_ngspice(executable, "deck")

    assert error.value.code == "ngspice-timeout"


def test_version_timeout_stops_before_deck_execution(tmp_path, monkeypatch):
    executable = _executable(
        tmp_path,
        'raise AssertionError("run must not start")',
        version_body="time.sleep(10)",
    )
    monkeypatch.setattr(_ngspice, "VERSION_TIMEOUT_SECONDS", 0.05)

    with pytest.raises(CliError) as error:
        _ngspice.run_ngspice(executable, "deck")

    assert error.value.code == "ngspice-version-timeout"


@pytest.mark.parametrize("stream", ["stdout", "stderr"])
def test_rejects_oversized_console_output(tmp_path, monkeypatch, stream):
    target = "sys.stdout" if stream == "stdout" else "sys.stderr"
    executable = _executable(
        tmp_path,
        f'print("x" * 4096, file={target}); time.sleep(0.2)',
    )
    monkeypatch.setattr(_ngspice, "LOG_LIMIT_BYTES", 64)

    with pytest.raises(CliError) as error:
        _ngspice.run_ngspice(executable, "deck")

    assert error.value.code == "ngspice-output-too-large"


def test_fast_oversized_output_is_bounded_when_read_after_exit(tmp_path, monkeypatch):
    executable = _executable(tmp_path, 'print("x" * 4096)')
    monkeypatch.setattr(_ngspice, "LOG_LIMIT_BYTES", 64)

    with pytest.raises(CliError) as error:
        _ngspice.run_ngspice(executable, "deck")

    assert error.value.code == "ngspice-output-too-large"


def test_monitor_failure_still_terminates_owned_process(tmp_path, monkeypatch):
    executable = _executable(
        tmp_path,
        'raise AssertionError("run must not start")',
        version_body="time.sleep(10)",
    )

    def fail_monitor(_path):
        raise OSError("monitor failed")

    monkeypatch.setattr(_ngspice, "_file_size", fail_monitor)
    with pytest.raises(CliError) as error:
        _ngspice.run_ngspice(executable, "deck")

    assert error.value.code == "ngspice-process-failed"
    assert "monitor failed" in str(error.value)


def test_rejects_oversized_result_while_process_is_running(tmp_path, monkeypatch):
    executable = _executable(
        tmp_path,
        'Path("volt-dc-output.txt").write_bytes(b"x" * 4096); time.sleep(0.2)',
    )

    with pytest.raises(CliError) as error:
        _ngspice.run_ngspice(executable, "deck", max_output_bytes=64)

    assert error.value.code == "ngspice-data-too-large"


@pytest.mark.parametrize(
    "run_body,code",
    [
        ("pass", "ngspice-output-missing"),
        ('Path("volt-dc-output.txt").mkdir()', "invalid-ngspice-output"),
        (
            'Path("target").write_bytes(b"data"); '
            'Path("volt-dc-output.txt").symlink_to("target")',
            "invalid-ngspice-output",
        ),
    ],
)
def test_rejects_missing_or_nonregular_result(tmp_path, run_body, code):
    executable = _executable(tmp_path, run_body)

    with pytest.raises(CliError) as error:
        _ngspice.run_ngspice(executable, "deck")

    assert error.value.code == code
