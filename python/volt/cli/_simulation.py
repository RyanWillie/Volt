"""Shared linear analysis execution and immutable artifact publication."""

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
_ARTIFACT_NGSPICE_ANALYSIS = "ngspice-analysis.json"
_ARTIFACT_NGSPICE_DECK = "deck.cir"
_ARTIFACT_NGSPICE_PROCESS = "ngspice-process.json"
_ARTIFACT_NGSPICE_OUTPUT = "ngspice-output.txt"


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


def _compile_outputs(input, request_path: Path, *, error_code: str):
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
        return artifacts, compile_payload, compile_report
    except CliError:
        raise
    except Exception as error:
        raise CliError(
            f"Native DC request binding or compilation failed: {error}",
            code=error_code,
        ) from error


def _native_outputs(
    input, request_path: Path
) -> tuple[dict[str, bytes], dict, dict | None, str, int]:
    artifacts, compile_payload, compile_report = _compile_outputs(
        input, request_path, error_code="native-dc-execution-failed"
    )
    if not compile_report.complete:
        return artifacts, compile_payload, None, "incomplete", EXIT_CHECK_FAILED

    try:
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


def _ngspice_outputs(
    input, request_path: Path, executable: Path
) -> tuple[dict[str, bytes], dict, dict | None, dict | None, dict | None, str, int]:
    from .. import prepare_ngspice_dc, solve_ngspice_dc
    from ._ngspice import run_ngspice

    process_payload = None
    artifacts, compile_payload, compile_report = _compile_outputs(
        input, request_path, error_code="ngspice-dc-execution-failed"
    )
    if not compile_report.complete:
        return (
            artifacts,
            compile_payload,
            None,
            None,
            None,
            "incomplete",
            EXIT_CHECK_FAILED,
        )

    try:
        analysis = prepare_ngspice_dc(compile_report.model)
        analysis_bytes = analysis.to_json().encode()
        analysis_payload = json.loads(analysis_bytes)
        artifacts[_ARTIFACT_NGSPICE_ANALYSIS] = analysis_bytes
        artifacts[_ARTIFACT_NGSPICE_DECK] = analysis.deck.encode()
        if not analysis.complete:
            return (
                artifacts,
                compile_payload,
                analysis_payload,
                None,
                None,
                "incomplete",
                EXIT_CHECK_FAILED,
            )

        output_bytes, process_payload = run_ngspice(
            executable,
            analysis.deck,
            max_output_bytes=analysis.maximum_output_bytes,
        )
        process_bytes = json.dumps(
            process_payload, separators=(",", ":"), sort_keys=True
        ).encode()
        artifacts[_ARTIFACT_NGSPICE_PROCESS] = process_bytes
        artifacts[_ARTIFACT_NGSPICE_OUTPUT] = output_bytes
        solve_report = solve_ngspice_dc(analysis, output_bytes)
        solve_bytes = solve_report.to_json().encode()
        solve_payload = json.loads(solve_bytes)
        artifacts[_ARTIFACT_SOLVE_REPORT] = solve_bytes
        if solve_report.success:
            artifacts[_ARTIFACT_SOLUTION] = solve_report.solution.to_json().encode()
            return (
                artifacts,
                compile_payload,
                analysis_payload,
                process_payload,
                solve_payload,
                "success",
                EXIT_SUCCESS,
            )
        return (
            artifacts,
            compile_payload,
            analysis_payload,
            process_payload,
            solve_payload,
            "failed",
            EXIT_CHECK_FAILED,
        )
    except CliError:
        raise
    except Exception as error:
        evidence = (
            ""
            if process_payload is None
            else "; process evidence: "
            + json.dumps(process_payload, separators=(",", ":"), sort_keys=True)
        )
        raise CliError(
            f"ngspice DC execution failed: {error}{evidence}",
            code="ngspice-dc-execution-failed",
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
    backend: str = "native",
    ngspice: Path | None = None,
) -> tuple[dict[str, object], int]:
    """Execute native linear DC and publish its canonical reports once."""

    output = Path(os.path.abspath(output))
    validate_output(output)
    backend_payload = None
    process_payload = None
    if backend == "native":
        artifacts, compile_payload, solve_payload, status, exit_code = _native_outputs(
            input, request_path
        )
    elif backend == "ngspice":
        if ngspice is None:
            raise CliError(
                "ngspice backend requires an explicit executable path.",
                code="ngspice-path-required",
            )
        (
            artifacts,
            compile_payload,
            backend_payload,
            process_payload,
            solve_payload,
            status,
            exit_code,
        ) = _ngspice_outputs(input, request_path, ngspice)
    else:
        raise CliError(
            f"Unsupported simulation backend: {backend}",
            code="unsupported-simulation-backend",
        )
    _write_and_publish(output, artifacts)
    artifact_paths = {name: str(output / name) for name in artifacts}
    payload = {
        "ok": status == "success",
        "status": status,
        "design": design,
        "source": source,
        "output": str(output),
        "written": True,
        "artifacts": artifact_paths,
        "compile_report": compile_payload,
        "solve_report": solve_payload,
    }
    if backend == "ngspice":
        payload.update(
            {
                "backend": backend,
                "backend_report": backend_payload,
                "process": process_payload,
            }
        )
    return payload, exit_code


def execute_ac(
    input, request_path: Path, output: Path, *, design: str, source: dict,
    backend: str = "native", ngspice: Path | None = None,
) -> tuple[dict[str, object], int]:
    """Explicitly execute a native AC sweep and atomically publish its reports."""
    from .. import AcRequest, solve_ac

    if backend != "native":
        raise CliError("AC analysis requires --backend native.", code="unsupported-ac-backend")
    if ngspice is not None:
        raise CliError("Native simulation does not accept an ngspice executable.",
                       code="native-backend-rejects-ngspice-path")
    output = Path(os.path.abspath(output))
    validate_output(output)
    try:
        data = request_path.read_bytes()
    except OSError as error:
        raise CliError(f"Failed to read AC request {request_path}: {error}",
                       code="ac-request-read-failed") from error
    try:
        request = AcRequest.from_json(input, data)
        report = compile_electrical(request)
        compile_bytes = report.to_json().encode()
        compile_payload = json.loads(compile_bytes)
        artifacts = {_ARTIFACT_REQUEST: request.to_json().encode(),
                     _ARTIFACT_COMPILE_REPORT: compile_bytes}
        solve_payload = None
        status, exit_code = "incomplete", EXIT_CHECK_FAILED
        if report.complete:
            solved = solve_ac(report.model)
            solve_bytes = solved.to_json().encode()
            solve_payload = json.loads(solve_bytes)
            artifacts[_ARTIFACT_SOLVE_REPORT] = solve_bytes
            status = "success" if solved.success else "failed"
            if solved.success:
                artifacts[_ARTIFACT_SOLUTION] = solved.solution.to_json().encode()
                exit_code = EXIT_SUCCESS
    except Exception as error:
        raise CliError(f"Native AC execution failed: {error}",
                       code="native-ac-execution-failed") from error
    _write_and_publish(output, artifacts)
    return {"ok": status == "success", "status": status, "analysis": "ac",
            "backend": "native", "design": design, "source": source,
            "output": str(output), "written": True,
            "artifacts": {name: str(output / name) for name in artifacts},
            "compile_report": compile_payload, "solve_report": solve_payload}, exit_code



def transient_options(args):
    from .. import Quantity, UnitDimension, TransientSolveOptions
    fields = ('h_min', 'h_initial', 'h_max', 'max_trials', 'max_accepted_steps')
    temporal = ('relative_tolerance', 'absolute_voltage_tolerance', 'absolute_current_tolerance')
    if args.analysis != 'transient':
        if any(getattr(args, name) is not None for name in fields + temporal):
            raise CliError('Transient numerical options require --analysis transient.',
                           code='unexpected-transient-options')
        return None
    if any(getattr(args, name) is None for name in fields):
        raise CliError('Transient analysis requires --h-min, --h-initial, --h-max, '
                       '--max-trials and --max-accepted-steps.',
                       code='missing-transient-options')
    try:
        # Native constructors validate dimensions, positivity, ordering, finite values and budgets.
        if args.max_trials <= 0 or args.max_accepted_steps <= 0:
            raise ValueError('Transient work budgets must be positive integers')
        return TransientSolveOptions(
            Quantity(UnitDimension.TIME, args.h_min),
            Quantity(UnitDimension.TIME, args.h_initial),
            Quantity(UnitDimension.TIME, args.h_max),
            args.max_trials, args.max_accepted_steps,
            1e-4 if args.relative_tolerance is None else args.relative_tolerance,
            Quantity(UnitDimension.VOLTAGE, 1e-6 if args.absolute_voltage_tolerance is None
                     else args.absolute_voltage_tolerance),
            Quantity(UnitDimension.CURRENT, 1e-9 if args.absolute_current_tolerance is None
                     else args.absolute_current_tolerance))
    except Exception as error:
        raise CliError(f'Invalid transient numerical options: {error}',
                       code='invalid-transient-options') from error


def execute_transient(input, request_path, output, *, design, source,
                      options, backend='native', ngspice=None):
    from .. import TransientRequest, compile_electrical, solve_transient
    if backend != 'native':
        raise CliError('Transient analysis requires --backend native.',
                       code='unsupported-transient-backend')
    if ngspice is not None:
        raise CliError('Native simulation does not accept an ngspice executable.',
                       code='native-backend-rejects-ngspice-path')
    output = Path(os.path.abspath(output))
    validate_output(output)
    try:
        data = request_path.read_bytes()
    except OSError as error:
        raise CliError(f'Failed to read transient request {request_path}: {error}',
                       code='transient-request-read-failed') from error
    try:
        request = TransientRequest.from_json(input, data)
        report = compile_electrical(request)
        compile_bytes = report.to_json().encode()
        compile_payload = json.loads(compile_bytes)
        artifacts = {_ARTIFACT_REQUEST: request.to_json().encode(),
                     _ARTIFACT_COMPILE_REPORT: compile_bytes}
        solve_payload = None
        status, exit_code = 'incomplete', EXIT_CHECK_FAILED
        if report.complete:
            solved = solve_transient(report.model, options)
            solve_bytes = solved.to_json().encode()
            solve_payload = json.loads(solve_bytes)
            artifacts[_ARTIFACT_SOLVE_REPORT] = solve_bytes
            status = 'success' if solved.success else 'failed'
            if solved.success:
                artifacts[_ARTIFACT_SOLUTION] = solved.solution.to_json().encode()
                exit_code = EXIT_SUCCESS
    except Exception as error:
        raise CliError(f'Native transient execution failed: {error}',
                       code='native-transient-execution-failed') from error
    _write_and_publish(output, artifacts)
    return {'ok': status == 'success', 'status': status, 'analysis': 'transient',
            'backend': 'native', 'design': design, 'source': source,
            'output': str(output), 'written': True,
            'artifacts': {name: str(output / name) for name in artifacts},
            'compile_report': compile_payload, 'solve_report': solve_payload}, exit_code
