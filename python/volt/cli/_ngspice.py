"""Bounded subprocess transport for the explicitly selected ngspice 46 binary."""

from __future__ import annotations

import hashlib
import json
import os
import re
import stat
import subprocess
import tempfile
import time
from pathlib import Path

from . import CliError


VERSION_TIMEOUT_SECONDS = 5.0
RUN_TIMEOUT_SECONDS = 30.0
LOG_LIMIT_BYTES = 256 * 1024
DEFAULT_OUTPUT_LIMIT_BYTES = 1024 * 1024
DECK_FILENAME = "deck.cir"
OUTPUT_FILENAME = "volt-dc-output.txt"
_VERSION_PATTERN = re.compile(
    rb"(?m)^\*\* ngspice-(\d+) : Circuit level simulation program\r?$"
)


def _environment(workdir: Path) -> dict[str, str]:
    private_configuration = workdir / "configuration"
    private_configuration.mkdir(exist_ok=True)
    private_configuration.joinpath("spinit").write_bytes(b"")
    environment = {
        "HOME": str(workdir),
        "LANG": "C",
        "LC_ALL": "C",
        "NGSPICE_INPUT_DIR": str(private_configuration),
        "NGSPICE_OSDI_DIR": str(private_configuration),
        "SPICE_SCRIPTS": str(private_configuration),
        "SPICE_USERINIT_DIR": str(private_configuration),
        "TMPDIR": str(workdir),
        "TZ": "UTC",
        "USERPROFILE": str(workdir),
    }
    for name in ("SystemRoot", "WINDIR"):
        if value := os.environ.get(name):
            environment[name] = value
    return environment


def _terminate(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        process.wait()
        return
    process.terminate()
    try:
        process.wait(timeout=1.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def _file_size(path: Path) -> int:
    try:
        return path.stat().st_size
    except FileNotFoundError:
        return 0


def _bounded_log_evidence(stdout_path: Path, stderr_path: Path) -> str:
    evidence = {}
    for name, path in (("stdout", stdout_path), ("stderr", stderr_path)):
        try:
            data = _bounded_read(path, LOG_LIMIT_BYTES)[:LOG_LIMIT_BYTES]
        except OSError:
            continue
        evidence[name] = _text(data)
    return json.dumps(evidence, separators=(",", ":"), sort_keys=True)


def _bounded_process(
    command: list[str],
    *,
    cwd: Path,
    timeout: float,
    timeout_code: str,
    output_path: Path | None = None,
    output_limit_bytes: int = DEFAULT_OUTPUT_LIMIT_BYTES,
) -> tuple[int, bytes, bytes]:
    stdout_path = cwd / "stdout.log"
    stderr_path = cwd / "stderr.log"
    process: subprocess.Popen[bytes] | None = None
    try:
        with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
            process = subprocess.Popen(
                command,
                cwd=cwd,
                env=_environment(cwd),
                stdin=subprocess.DEVNULL,
                stdout=stdout,
                stderr=stderr,
                shell=False,
            )
            deadline = time.monotonic() + timeout
            while process.poll() is None:
                if time.monotonic() >= deadline:
                    _terminate(process)
                    raise CliError(
                        f"ngspice exceeded its {timeout:g} second timeout: "
                        + _bounded_log_evidence(stdout_path, stderr_path),
                        code=timeout_code,
                    )
                if (
                    _file_size(stdout_path) > LOG_LIMIT_BYTES
                    or _file_size(stderr_path) > LOG_LIMIT_BYTES
                ):
                    _terminate(process)
                    raise CliError(
                        "ngspice console output exceeded the adapter limit: "
                        + _bounded_log_evidence(stdout_path, stderr_path),
                        code="ngspice-output-too-large",
                    )
                if output_path is not None and _file_size(output_path) > output_limit_bytes:
                    _terminate(process)
                    raise CliError(
                        "ngspice result data exceeded the adapter limit: "
                        + _bounded_log_evidence(stdout_path, stderr_path),
                        code="ngspice-data-too-large",
                    )
                time.sleep(0.01)
            return_code = process.wait()
    except CliError:
        raise
    except OSError as error:
        if process is not None:
            _terminate(process)
        evidence = _bounded_log_evidence(stdout_path, stderr_path)
        raise CliError(
            f"ngspice process transport failed: {error}; evidence: {evidence}",
            code="ngspice-process-failed",
        ) from error
    finally:
        if process is not None and process.poll() is None:
            _terminate(process)

    try:
        stdout_bytes = _bounded_read(stdout_path, LOG_LIMIT_BYTES)
        stderr_bytes = _bounded_read(stderr_path, LOG_LIMIT_BYTES)
    except OSError as error:
        raise CliError(
            f"Failed to read bounded ngspice process evidence: {error}",
            code="ngspice-process-failed",
        ) from error
    if len(stdout_bytes) > LOG_LIMIT_BYTES or len(stderr_bytes) > LOG_LIMIT_BYTES:
        raise CliError(
            "ngspice console output exceeded the adapter limit: "
            + json.dumps(
                {
                    "stdout": _text(stdout_bytes[:LOG_LIMIT_BYTES]),
                    "stderr": _text(stderr_bytes[:LOG_LIMIT_BYTES]),
                },
                separators=(",", ":"),
                sort_keys=True,
            ),
            code="ngspice-output-too-large",
        )
    if output_path is not None and _file_size(output_path) > output_limit_bytes:
        raise CliError(
            "ngspice result data exceeded the adapter limit: "
            + _bounded_log_evidence(stdout_path, stderr_path),
            code="ngspice-data-too-large",
        )
    return return_code, stdout_bytes, stderr_bytes


def _bounded_read(path: Path, limit: int) -> bytes:
    with path.open("rb") as handle:
        return handle.read(limit + 1)


def _bounded_regular_read(path: Path, limit: int) -> bytes:
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as error:
        raise CliError(
            f"Failed to open ngspice output {OUTPUT_FILENAME}: {error}",
            code="invalid-ngspice-output",
        ) from error
    with os.fdopen(descriptor, "rb") as handle:
        if not stat.S_ISREG(os.fstat(handle.fileno()).st_mode):
            raise CliError(
                f"ngspice output {OUTPUT_FILENAME} is not a regular file.",
                code="invalid-ngspice-output",
            )
        return handle.read(limit + 1)


def _text(data: bytes) -> str:
    return data.decode("utf-8", errors="replace")


def _resolve_executable(executable: Path) -> Path:
    path = executable.expanduser().resolve()
    if not path.is_file():
        raise CliError(f"ngspice executable not found: {path}", code="ngspice-not-found")
    if os.name != "nt" and not os.access(path, os.X_OK):
        raise CliError(
            f"ngspice path is not executable: {path}", code="ngspice-not-executable"
        )
    return path


def run_ngspice(
    executable: Path,
    deck: str,
    *,
    max_output_bytes: int = DEFAULT_OUTPUT_LIMIT_BYTES,
) -> tuple[bytes, dict[str, object]]:
    """Run one generated deck with ngspice 46 and return bytes plus bounded evidence."""

    if not isinstance(max_output_bytes, int) or isinstance(max_output_bytes, bool):
        raise TypeError("ngspice max_output_bytes must be an integer")
    if max_output_bytes <= 0:
        raise ValueError("ngspice max_output_bytes must be positive")
    path = _resolve_executable(executable)
    with tempfile.TemporaryDirectory(prefix="volt-ngspice-") as temporary:
        workdir = Path(temporary).resolve()
        version_command = [str(path), "-n", "--version"]
        code, version_stdout, version_stderr = _bounded_process(
            version_command,
            cwd=workdir,
            timeout=VERSION_TIMEOUT_SECONDS,
            timeout_code="ngspice-version-timeout",
        )
        if code != 0:
            raise CliError(
                f"ngspice version check exited with status {code}: "
                + json.dumps(
                    {
                        "stdout": _text(version_stdout),
                        "stderr": _text(version_stderr),
                    },
                    separators=(",", ":"),
                    sort_keys=True,
                ),
                code="ngspice-version-failed",
            )
        match = _VERSION_PATTERN.search(version_stdout + b"\n" + version_stderr)
        if match is None or match.group(1) != b"46":
            raise CliError(
                "Selected executable is not the supported ngspice 46: "
                + json.dumps(
                    {
                        "stdout": _text(version_stdout),
                        "stderr": _text(version_stderr),
                    },
                    separators=(",", ":"),
                    sort_keys=True,
                ),
                code="unsupported-ngspice-version",
            )

        deck_path = workdir / DECK_FILENAME
        output_path = workdir / OUTPUT_FILENAME
        deck_path.write_text(deck, encoding="utf-8", newline="\n")
        command = [str(path), "-n", DECK_FILENAME]
        code, stdout, stderr = _bounded_process(
            command,
            cwd=workdir,
            timeout=RUN_TIMEOUT_SECONDS,
            timeout_code="ngspice-timeout",
            output_path=output_path,
            output_limit_bytes=max_output_bytes,
        )
        evidence: dict[str, object] = {
            "format": "volt.ngspice-process-evidence",
            "schema_version": 1,
            "executable": str(path),
            "version": "ngspice-46",
            "version_command": version_command,
            "version_stdout": _text(version_stdout),
            "version_stderr": _text(version_stderr),
            "command": command,
            "exit_code": code,
            "policy": {
                "version_timeout_seconds": VERSION_TIMEOUT_SECONDS,
                "run_timeout_seconds": RUN_TIMEOUT_SECONDS,
                "stdout_maximum_bytes": LOG_LIMIT_BYTES,
                "stderr_maximum_bytes": LOG_LIMIT_BYTES,
                "result_maximum_bytes": max_output_bytes,
                "locale": "C",
                "startup_mode": "no-user-init-private-system-init",
            },
            "stdout": _text(stdout),
            "stderr": _text(stderr),
        }
        if code != 0:
            raise CliError(
                "ngspice execution failed: "
                + json.dumps(evidence, separators=(",", ":"), sort_keys=True),
                code="ngspice-execution-failed",
            )
        try:
            file_status = output_path.lstat()
        except FileNotFoundError as error:
            raise CliError(
                f"ngspice did not produce {OUTPUT_FILENAME}.",
                code="ngspice-output-missing",
            ) from error
        if not stat.S_ISREG(file_status.st_mode):
            raise CliError(
                f"ngspice output {OUTPUT_FILENAME} is not a regular file.",
                code="invalid-ngspice-output",
            )
        output = _bounded_regular_read(output_path, max_output_bytes)
        if len(output) > max_output_bytes:
            raise CliError(
                "ngspice result data exceeded the adapter limit.",
                code="ngspice-data-too-large",
            )
        evidence["output_filename"] = OUTPUT_FILENAME
        evidence["output_byte_size"] = len(output)
        evidence["output_content_digest"] = "sha256:" + hashlib.sha256(output).hexdigest()
        return output, evidence
