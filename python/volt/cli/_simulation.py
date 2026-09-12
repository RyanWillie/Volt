"""Shared native linear-DC execution and immutable artifact publication."""

from __future__ import annotations

import ctypes
import errno
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

from .. import DcRequest, compile_electrical, solve_dc
from . import CliError, EXIT_CHECK_FAILED, EXIT_SUCCESS


_ARTIFACT_REQUEST = "request.json"
_ARTIFACT_COMPILE_REPORT = "compile-report.json"
_ARTIFACT_SOLVE_REPORT = "solve-report.json"
_ARTIFACT_SOLUTION = "solution.json"


def validate_output(output: Path) -> None:
    """Reject every existing destination kind without changing the filesystem."""

    if os.path.lexists(output):
        raise CliError(
            f"Simulation output already exists: {output}",
            code="simulation-output-exists",
        )


def _publish_directory(stage: Path, output: Path) -> None:
    source = os.fsencode(stage)
    destination = os.fsencode(output)

    if sys.platform == "darwin":
        renamex_np = ctypes.CDLL(None, use_errno=True).renamex_np
        renamex_np.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
        renamex_np.restype = ctypes.c_int
        if renamex_np(source, destination, 0x00000004) != 0:  # RENAME_EXCL
            value = ctypes.get_errno()
            raise OSError(value, os.strerror(value), output)
        return

    if sys.platform.startswith("linux"):
        library = ctypes.CDLL(None, use_errno=True)
        try:
            renameat2 = library.renameat2
        except AttributeError as error:
            raise OSError(errno.ENOTSUP, "renameat2 is unavailable", output) from error
        renameat2.argtypes = (
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_uint,
        )
        renameat2.restype = ctypes.c_int
        if renameat2(-100, source, -100, destination, 1) != 0:  # RENAME_NOREPLACE
            value = ctypes.get_errno()
            raise OSError(value, os.strerror(value), output)
        return

    if os.name == "nt":
        os.rename(stage, output)
        return

    raise OSError(
        errno.ENOTSUP, "atomic no-replace directory rename is unavailable", output
    )


def _native_outputs(
    input, request_path: Path
) -> tuple[dict[str, bytes], dict, dict | None, str, int]:
    try:
        request_data = request_path.read_bytes()
    except OSError as error:
        raise CliError(
            f"Failed to read DC request {request_path}: {error}",
            code="dc-request-read-failed",
        ) from error

    try:
        request = DcRequest.from_json(input, request_data)
        request_bytes = request.to_json().encode()
        compile_report = compile_electrical(request)
        compile_bytes = compile_report.to_json().encode()
        compile_payload = json.loads(compile_bytes)

        artifacts = {
            _ARTIFACT_REQUEST: request_bytes,
            _ARTIFACT_COMPILE_REPORT: compile_bytes,
        }
        if not compile_report.complete:
            return artifacts, compile_payload, None, "incomplete", EXIT_CHECK_FAILED

        solve_report = solve_dc(compile_report.model)
        solve_bytes = solve_report.to_json().encode()
        solve_payload = json.loads(solve_bytes)
        artifacts[_ARTIFACT_SOLVE_REPORT] = solve_bytes
        if solve_report.success:
            artifacts[_ARTIFACT_SOLUTION] = solve_report.solution.to_json().encode()
            return artifacts, compile_payload, solve_payload, "success", EXIT_SUCCESS
        return artifacts, compile_payload, solve_payload, "failed", EXIT_CHECK_FAILED
    except CliError:
        raise
    except Exception as error:
        raise CliError(
            f"Native DC execution failed: {error}",
            code="native-dc-execution-failed",
        ) from error


def _write_and_publish(output: Path, artifacts: dict[str, bytes]) -> None:
    stage: Path | None = None
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
        for name, data in artifacts.items():
            (stage / name).write_bytes(data)
        _publish_directory(stage, output)
        stage = None
    except Exception as error:
        if stage is not None:
            try:
                shutil.rmtree(stage)
            except Exception as cleanup_error:
                raise CliError(
                    f"Simulation publication failed: {error}; failed to clean retained "
                    f"staging directory {stage}: {cleanup_error}",
                    code="simulation-publication-failed",
                ) from error
        if isinstance(error, CliError):
            raise
        raise CliError(
            f"Failed to publish simulation output {output}: {error}",
            code=(
                "simulation-output-exists"
                if isinstance(error, FileExistsError)
                else "simulation-publication-failed"
            ),
        ) from error


def execute_dc(
    input,
    request_path: Path,
    output: Path,
    *,
    design: str,
    source: dict,
) -> tuple[dict[str, object], int]:
    """Execute native linear DC and publish its canonical reports once."""

    output = Path(os.path.abspath(output))
    validate_output(output)
    artifacts, compile_payload, solve_payload, status, exit_code = _native_outputs(
        input, request_path
    )
    _write_and_publish(output, artifacts)
    artifact_paths = {name: str(output / name) for name in artifacts}
    return (
        {
            "ok": status == "success",
            "status": status,
            "design": design,
            "source": source,
            "output": str(output),
            "written": True,
            "artifacts": artifact_paths,
            "compile_report": compile_payload,
            "solve_report": solve_payload,
        },
        exit_code,
    )
