"""Command-line plumbing for Volt project discovery and entrypoint loading."""

from __future__ import annotations

import argparse
import importlib
import importlib.machinery
import json
import os
import sys
import tomllib
from collections.abc import Iterable, Mapping, Sequence
from contextlib import contextmanager, redirect_stdout
from dataclasses import dataclass
from io import StringIO
from pathlib import Path
from typing import Any, Callable

from .._project_model_lookup import model_output_name
from ..design import Design
from ..pcb import Board
from ..project import (
    Project,
    ProjectDiagnostics,
    ProjectResult,
    _diagnostics_payload,
)
from ..schematic import Schematic


EXIT_SUCCESS = 0
EXIT_CHECK_FAILED = 1
EXIT_COMMAND_FAILED = 2
DIAGNOSTIC_SEVERITIES = ("error", "warning", "info")
DIAGNOSTIC_STAGES = ("library", "design", "schematic", "board")
BUILD_PROFILES = ("default", "viewer")
INIT_PROFILES = ("jlcpcb", "oshpark", "pcbway", "generic")
_SEVERITY_ORDER = {"error": 0, "warning": 1, "info": 2}


class CliError(Exception):
    """User-facing CLI failure with a deterministic process exit code."""

    def __init__(self, message: str, *, exit_code: int = EXIT_COMMAND_FAILED):
        super().__init__(message)
        self.exit_code = exit_code


@dataclass(frozen=True)
class ProjectConfig:
    """Parsed ``volt.toml`` with project-root-relative paths normalized."""

    root: Path
    config_path: Path
    entrypoint: str
    paths: Mapping[str, Path]
    manufacturing_profile_path: str | None
    manufacturing_profile: Path | None
    raw: Mapping[str, Any]


def main(argv: Sequence[str] | None = None) -> int:
    """Run the ``volt`` CLI and return a process exit code."""

    parser = _build_parser()
    args = parser.parse_args(argv)
    handler = getattr(args, "handler", None)
    if handler is None:
        parser.print_help()
        return EXIT_SUCCESS

    try:
        result = handler(args)
    except CliError as error:
        print(f"volt: error: {error}", file=sys.stderr)
        return error.exit_code
    if isinstance(result, int):
        return result
    return EXIT_SUCCESS


def discover_project(
    *, cwd: str | Path | None = None, project: str | Path | None = None
) -> ProjectConfig:
    """Find and parse ``volt.toml`` from ``cwd`` or an explicit override."""

    if project is not None:
        return load_project_config(_project_config_path(project))

    start = Path.cwd() if cwd is None else Path(cwd)
    current = start.resolve()
    if current.is_file():
        current = current.parent

    for directory in (current, *current.parents):
        candidate = directory / "volt.toml"
        if candidate.is_file():
            return load_project_config(candidate)

    raise CliError(
        "No volt.toml found. Run from a Volt project directory or pass --project PATH."
    )


def load_project_config(path: str | Path) -> ProjectConfig:
    """Parse a ``volt.toml`` file into shared CLI configuration."""

    config_path = Path(path).resolve()
    if not config_path.is_file():
        raise CliError(f"Volt project config not found: {config_path}")

    try:
        with config_path.open("rb") as handle:
            payload = tomllib.load(handle)
    except tomllib.TOMLDecodeError as error:
        raise CliError(f"Failed to parse {config_path}: {error}") from error
    except OSError as error:
        raise CliError(f"Failed to read {config_path}: {error}") from error

    project_table = _required_table(payload, "project", config_path)
    entrypoint = project_table.get("entrypoint")
    if not isinstance(entrypoint, str) or not entrypoint.strip():
        raise CliError(
            f"{config_path} must define [project].entrypoint as module:function"
        )

    root = config_path.parent
    paths_table = _optional_table(payload, "paths", config_path)
    paths = {
        name: _resolve_project_path(root, name, value, config_path)
        for name, value in paths_table.items()
    }
    manufacturing_profile_path, manufacturing_profile = _manufacturing_profile(
        payload,
        root,
        config_path,
    )
    return ProjectConfig(
        root=root,
        config_path=config_path,
        entrypoint=entrypoint.strip(),
        paths=paths,
        manufacturing_profile_path=manufacturing_profile_path,
        manufacturing_profile=manufacturing_profile,
        raw=payload,
    )


def load_entrypoint(config: ProjectConfig) -> Callable[[], object]:
    """Load the configured project entrypoint from ``module:function``."""

    module_name, function_name = _split_entrypoint(config.entrypoint)
    with _project_runtime(config.root):
        _evict_project_entrypoint_modules(module_name, config.root)
        importlib.invalidate_caches()
        try:
            module = importlib.import_module(module_name)
        except Exception as error:
            raise CliError(
                "Failed to import project entrypoint module "
                f"{module_name!r} from project root {config.root}: {error}"
            ) from error

    try:
        entrypoint = getattr(module, function_name)
    except AttributeError as error:
        raise CliError(
            f"Project entrypoint {config.entrypoint!r} has no attribute "
            f"{function_name!r}."
        ) from error
    if not callable(entrypoint):
        raise CliError(f"Project entrypoint {config.entrypoint!r} is not callable.")
    return entrypoint


def run_entrypoint(config: ProjectConfig) -> object:
    """Run the configured entrypoint with cwd and import path rooted at the project."""

    entrypoint = load_entrypoint(config)
    with _project_runtime(config.root):
        try:
            return entrypoint()
        except CliError:
            raise
        except Exception as error:
            raise CliError(
                f"Project entrypoint {config.entrypoint!r} failed: {error}"
            ) from error


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="volt")
    subparsers = parser.add_subparsers(dest="command", metavar="command")

    run_parser = subparsers.add_parser(
        "run",
        help="load and run the configured project entrypoint",
        description="Load and run the configured project entrypoint.",
    )
    _add_project_argument(run_parser)
    run_parser.set_defaults(handler=_handle_run)

    model_parser = subparsers.add_parser(
        "model",
        help="emit an in-memory project model stream",
        description="Load and run the configured project entrypoint, then emit model JSON.",
    )
    _add_project_argument(model_parser)
    model_parser.add_argument(
        "--json",
        dest="emit_json",
        action="store_true",
        help="Emit the project model stream as JSON.",
    )
    model_parser.add_argument("--design", help="Filter to one logical design by name.")
    model_parser.add_argument(
        "--schematic",
        help="Filter to one schematic by name or design:schematic output name.",
    )
    model_parser.add_argument(
        "--board",
        help="Filter to one PCB by name or design:board output name.",
    )
    model_parser.set_defaults(handler=_handle_model)

    diagnostics_parser = subparsers.add_parser(
        "diagnostics",
        help="report project diagnostics",
        description="Load and run the configured project entrypoint, then report diagnostics.",
    )
    _add_project_argument(diagnostics_parser)
    diagnostics_parser.add_argument(
        "--json",
        dest="emit_json",
        action="store_true",
        help="Emit diagnostics using the project bundle diagnostics.json schema.",
    )
    diagnostics_parser.add_argument(
        "--check",
        action="store_true",
        help="Exit 1 when the project result is not ok.",
    )
    diagnostics_parser.add_argument(
        "--stage",
        help="Filter diagnostics to one project stage.",
    )
    diagnostics_parser.add_argument(
        "--severity",
        dest="severities",
        action="append",
        default=None,
        metavar="LEVEL",
        help="Filter diagnostics to a severity; may be repeated.",
    )
    diagnostics_parser.set_defaults(handler=_handle_diagnostics)

    build_parser = subparsers.add_parser(
        "build",
        help="run a project and write build artifacts",
        description="Run the configured project and write project-result artifacts.",
    )
    _add_project_argument(build_parser)
    build_parser.add_argument(
        "--output",
        type=Path,
        help="Output directory. Defaults to [paths].artifacts or ./build.",
    )
    build_parser.add_argument(
        "--profile",
        choices=BUILD_PROFILES,
        default="default",
        help="ProjectResult.write bundle profile.",
    )
    build_parser.add_argument(
        "--flat",
        action="store_true",
        help="Write legacy flat artifacts instead of the project-result bundle.",
    )
    build_parser.add_argument(
        "--json",
        dest="emit_json",
        action="store_true",
        help="Emit a stable build summary as JSON.",
    )
    build_parser.set_defaults(handler=_handle_build)

    info_parser = subparsers.add_parser(
        "info",
        help="report project metadata and run summary",
        description="Run the configured project and report metadata without gating.",
    )
    _add_project_argument(info_parser)
    info_parser.add_argument(
        "--json",
        dest="emit_json",
        action="store_true",
        help="Emit a stable project info summary as JSON.",
    )
    info_parser.set_defaults(handler=_handle_info)

    init_parser = subparsers.add_parser(
        "init",
        help="scaffold a new Volt project",
        description="Create volt.toml, main.py, and a starter capability profile.",
    )
    init_parser.add_argument(
        "name",
        nargs="?",
        help="Project directory and name. Defaults to the current directory.",
    )
    init_parser.add_argument(
        "--profile",
        choices=INIT_PROFILES,
        default="generic",
        help="Starter manufacturing capability profile to write.",
    )
    init_parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite existing starter files.",
    )
    init_parser.set_defaults(handler=_handle_init)

    return parser


def _add_project_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--project",
        type=Path,
        help="Path to a Volt project root or volt.toml file.",
    )


def _handle_run(args: argparse.Namespace) -> None:
    config = discover_project(project=args.project)
    run_entrypoint(config)


def _handle_model(args: argparse.Namespace) -> None:
    if not args.emit_json:
        raise CliError("volt model currently requires --json.")
    config = discover_project(project=args.project)
    project_stdout = StringIO()
    try:
        with redirect_stdout(project_stdout):
            result = _project_result_from_entrypoint(config)
    except Exception:
        _forward_project_stdout(project_stdout)
        raise
    _forward_project_stdout(project_stdout)
    payload = _model_json_payload(
        result,
        design_selector=args.design,
        schematic_selector=args.schematic,
        board_selector=args.board,
    )
    print(json.dumps(payload, separators=(",", ":"), sort_keys=True))


def _handle_diagnostics(args: argparse.Namespace) -> int | None:
    stage = _validated_diagnostic_stage(args.stage)
    severities = _validated_diagnostic_severities(args.severities)
    config = discover_project(project=args.project)
    project_stdout = StringIO()
    try:
        with redirect_stdout(project_stdout):
            result = _project_result_from_entrypoint(config)
    except Exception:
        _forward_project_stdout(project_stdout)
        raise
    _forward_project_stdout(project_stdout)

    diagnostics = _filtered_diagnostics(
        result,
        stage=stage,
        severities=severities,
    )
    if args.emit_json:
        payload = _diagnostics_payload(result, diagnostics=diagnostics)
        print(json.dumps(payload, separators=(",", ":"), sort_keys=True))
    else:
        _print_diagnostics_report(result, diagnostics)
    if args.check and not result.ok:
        return EXIT_CHECK_FAILED
    return None


def _handle_build(args: argparse.Namespace) -> int | None:
    config = discover_project(project=args.project)
    result = _project_result_with_forwarded_stdout(config)
    output = _build_output_path(config, args.output)
    mode = "flat" if args.flat else "bundle"

    if not result.ok:
        if args.emit_json:
            print(
                json.dumps(
                    _build_payload(
                        result,
                        output=output,
                        mode=mode,
                        profile=args.profile,
                        written=False,
                    ),
                    separators=(",", ":"),
                    sort_keys=True,
                )
            )
        else:
            payload = _diagnostics_payload(result)
            summary = _diagnostic_summary_text(payload["summary"])
            print(
                f"Build refused: {payload['status']} ({summary})",
                file=sys.stderr,
            )
        return EXIT_CHECK_FAILED

    if args.flat:
        result.write_artifacts(output)
    else:
        result.write(output, profile=args.profile)

    if args.emit_json:
        print(
            json.dumps(
                _build_payload(
                    result,
                    output=output,
                    mode=mode,
                    profile=args.profile,
                    written=True,
                ),
                separators=(",", ":"),
                sort_keys=True,
            )
        )
    else:
        print(f"Build: {result.status} -> {output}")
    return None


def _handle_info(args: argparse.Namespace) -> None:
    config = discover_project(project=args.project)
    result = _project_result_with_forwarded_stdout(config)
    payload = _info_payload(config, result)
    if args.emit_json:
        print(json.dumps(payload, separators=(",", ":"), sort_keys=True))
        return
    _print_info_report(payload)


def _handle_init(args: argparse.Namespace) -> None:
    root = _init_root(args.name)
    project_name = root.name or "volt-project"
    profile_path = Path("profiles") / f"{args.profile}.volt.json"
    files = (
        root / "volt.toml",
        root / "main.py",
        root / profile_path,
    )
    existing = tuple(path for path in files if path.exists())
    if existing and not args.force:
        raise CliError(
            f"Refusing to overwrite existing file: {existing[0]}. Pass --force to replace it."
        )
    if root.exists() and not root.is_dir():
        raise CliError(f"Project target exists and is not a directory: {root}")

    root.mkdir(parents=True, exist_ok=True)
    (root / "profiles").mkdir(parents=True, exist_ok=True)
    (root / "volt.toml").write_text(
        _starter_config(profile_path=profile_path),
        encoding="utf-8",
    )
    (root / "main.py").write_text(
        _starter_entrypoint(project_name),
        encoding="utf-8",
    )
    (root / profile_path).write_text(
        json.dumps(_starter_profile(args.profile), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"Initialized Volt project at {root}")


def _forward_project_stdout(stream: StringIO) -> None:
    text = stream.getvalue()
    if text:
        print(text, file=sys.stderr, end="")


def _project_result_with_forwarded_stdout(config: ProjectConfig) -> ProjectResult:
    project_stdout = StringIO()
    try:
        with redirect_stdout(project_stdout):
            result = _project_result_from_entrypoint(config)
    except Exception:
        _forward_project_stdout(project_stdout)
        raise
    _forward_project_stdout(project_stdout)
    return result


def _project_result_from_entrypoint(config: ProjectConfig) -> ProjectResult:
    value = run_entrypoint(config)
    if isinstance(value, ProjectResult):
        return value
    if isinstance(value, Project):
        with _project_runtime(config.root):
            try:
                return value.run()
            except CliError:
                raise
            except Exception as error:
                raise CliError(
                    f"Project entrypoint {config.entrypoint!r} failed: {error}"
                ) from error
    raise CliError(
        f"Project entrypoint {config.entrypoint!r} must return "
        "volt.ProjectResult or volt.Project."
    )


def _model_json_payload(
    result: ProjectResult,
    *,
    design_selector: str | None,
    schematic_selector: str | None,
    board_selector: str | None,
) -> dict[str, object]:
    all_designs = result.designs
    all_schematics = result.schematics
    all_boards = result.boards

    designs = all_designs
    schematics = all_schematics
    boards = all_boards

    if design_selector is not None:
        design = _select_design(designs, design_selector)
        designs = (design,)
        schematics = tuple(model for model in schematics if model._design is design)
        boards = tuple(model for model in boards if model._design is design)
    if schematic_selector is not None:
        schematics = (
            _select_projection(
                schematics,
                all_schematics,
                schematic_selector,
                "schematic",
            ),
        )
    if board_selector is not None:
        boards = (
            _select_projection(
                boards,
                all_boards,
                board_selector,
                "board",
            ),
        )

    return {
        "status": result.status,
        "diagnostics": _diagnostics_payload(result),
        "models": {
            "designs": [_design_payload(model) for model in designs],
            "schematics": [
                _schematic_payload(model, all_schematics) for model in schematics
            ],
            "boards": [_board_payload(model, all_boards) for model in boards],
        },
    }


def _build_output_path(config: ProjectConfig, output: Path | None) -> Path:
    if output is None:
        return config.paths.get("artifacts", config.root / "build")
    path = output.expanduser()
    if path.is_absolute():
        return path
    return Path.cwd() / path


def _build_payload(
    result: ProjectResult,
    *,
    output: Path,
    mode: str,
    profile: str,
    written: bool,
) -> dict[str, object]:
    return {
        "ok": result.ok,
        "status": result.status,
        "output": str(output),
        "mode": mode,
        "profile": profile,
        "written": written,
        "diagnostics": _diagnostics_payload(result),
        "tests": _tests_summary(_project_tests(result)),
    }


def _info_payload(config: ProjectConfig, result: ProjectResult) -> dict[str, object]:
    diagnostics = _diagnostics_payload(result)
    return {
        "project": {
            "name": result.project.name,
            "version": result.project.version,
            "description": result.project.description,
        },
        "status": result.status,
        "ok": result.ok,
        "config": {
            "root": str(config.root),
            "path": str(config.config_path),
            "entrypoint": config.entrypoint,
        },
        "manufacturing_profile": _manufacturing_profile_payload(config),
        "stages": [
            {
                "name": stage.name,
                "model_count": stage.model_count,
                "tests": _tests_summary(stage.tests),
            }
            for stage in result.stages
        ],
        "models": {
            "designs": len(result.designs),
            "schematics": len(result.schematics),
            "boards": len(result.boards),
        },
        "diagnostics": {
            "status": diagnostics["status"],
            "summary": diagnostics["summary"],
        },
        "tests": _tests_summary(_project_tests(result)),
    }


def _print_info_report(payload: Mapping[str, object]) -> None:
    project = payload["project"]
    diagnostics = payload["diagnostics"]
    models = payload["models"]
    tests = payload["tests"]
    profile = payload["manufacturing_profile"]
    assert isinstance(project, Mapping)
    assert isinstance(diagnostics, Mapping)
    assert isinstance(models, Mapping)
    assert isinstance(tests, Mapping)
    print(f"Project: {project['name']}")
    print(f"Status: {payload['status']}")
    print(
        "Models: "
        f"{models['designs']} designs, "
        f"{models['schematics']} schematics, "
        f"{models['boards']} boards"
    )
    print(f"Diagnostics: {_diagnostic_summary_text(diagnostics['summary'])}")
    print(f"Tests: {tests['total']} total, {tests['failed']} failed")
    if isinstance(profile, Mapping):
        print(f"Manufacturing profile: {profile['path']}")
    else:
        print("Manufacturing profile: <none>")


def _project_tests(result: ProjectResult) -> tuple[object, ...]:
    return tuple(test for stage in result.stages for test in stage.tests)


def _tests_summary(tests: Iterable[object]) -> dict[str, int]:
    items = tuple(tests)
    return {
        "total": len(items),
        "failed": sum(1 for item in items if not getattr(item, "ok", False)),
    }


def _manufacturing_profile_payload(
    config: ProjectConfig,
) -> dict[str, str] | None:
    if config.manufacturing_profile_path is None or config.manufacturing_profile is None:
        return None
    return {
        "path": config.manufacturing_profile_path,
        "resolved_path": str(config.manufacturing_profile),
    }


def _select_design(designs: tuple[Design, ...], selector: str) -> Design:
    matches = tuple(model for model in designs if model.name == selector)
    if len(matches) == 1:
        return matches[0]
    candidates = _format_candidates(model.name for model in designs)
    if len(matches) > 1:
        raise CliError(
            f"Ambiguous design selector {selector!r}. Candidates: {candidates}"
        )
    raise CliError(f"No design named {selector!r}. Candidates: {candidates}")


def _select_projection(
    models: tuple[Schematic, ...] | tuple[Board, ...],
    all_models: tuple[Schematic, ...] | tuple[Board, ...],
    selector: str,
    kind: str,
) -> Schematic | Board:
    output_matches = tuple(
        model for model in models if model_output_name(model, all_models) == selector
    )
    if len(output_matches) == 1:
        return output_matches[0]

    name_matches = tuple(model for model in models if model.name == selector)
    if len(name_matches) == 1:
        return name_matches[0]

    candidates = _format_candidates(
        model_output_name(model, all_models) for model in models
    )
    if len(output_matches) > 1 or len(name_matches) > 1:
        raise CliError(
            f"Ambiguous {kind} selector {selector!r}. Candidates: {candidates}"
        )
    raise CliError(f"No {kind} named {selector!r}. Candidates: {candidates}")


def _design_payload(model: Design) -> dict[str, object]:
    return {"name": model.name, "model": json.loads(model.to_json())}


def _schematic_payload(
    model: Schematic,
    all_schematics: tuple[Schematic, ...],
) -> dict[str, object]:
    return {
        "name": model_output_name(model, all_schematics),
        "design": model._design.name,
        "schematic": model.name,
        "model": json.loads(model.to_json()),
    }


def _board_payload(
    model: Board,
    all_boards: tuple[Board, ...],
) -> dict[str, object]:
    return {
        "name": model_output_name(model, all_boards),
        "design": model._design.name,
        "board": model.name,
        "model": json.loads(model.to_json()),
    }


def _validated_diagnostic_stage(stage: str | None) -> str | None:
    if stage is None:
        return None
    normalized = stage.strip().lower()
    if normalized in DIAGNOSTIC_STAGES:
        return normalized
    expected = ", ".join(DIAGNOSTIC_STAGES)
    raise CliError(
        f"Invalid diagnostic stage {stage!r}. Expected one of: {expected}."
    )


def _validated_diagnostic_severities(
    severities: Sequence[str] | None,
) -> tuple[str, ...]:
    if severities is None:
        return ()
    normalized: list[str] = []
    for severity in severities:
        value = severity.strip().lower()
        if value not in DIAGNOSTIC_SEVERITIES:
            expected = ", ".join(DIAGNOSTIC_SEVERITIES)
            raise CliError(
                f"Invalid diagnostic severity {severity!r}. Expected one of: {expected}."
            )
        if value not in normalized:
            normalized.append(value)
    return tuple(normalized)


def _filtered_diagnostics(
    result: ProjectResult,
    *,
    stage: str | None,
    severities: tuple[str, ...],
) -> ProjectDiagnostics:
    diagnostics = tuple(result.diagnostics)
    if stage is not None:
        diagnostics = tuple(
            diagnostic for diagnostic in diagnostics if diagnostic.stage == stage
        )
    if severities:
        diagnostics = tuple(
            diagnostic
            for diagnostic in diagnostics
            if diagnostic.severity in severities
        )
    return ProjectDiagnostics(diagnostics)


def _print_diagnostics_report(
    result: ProjectResult,
    diagnostics: ProjectDiagnostics,
) -> None:
    payload = _diagnostics_payload(result, diagnostics=diagnostics)
    summary = _diagnostic_summary_text(payload["summary"])
    print(f"Diagnostics: {payload['status']} ({summary})")
    grouped = _group_diagnostics(diagnostics)
    if not grouped:
        print("No diagnostics.")
        return
    styled = _style_stdout()
    for report, source in sorted(grouped):
        print(report)
        print(f"  {source}")
        for diagnostic in sorted(grouped[(report, source)], key=_diagnostic_sort_key):
            severity = _format_severity(diagnostic.severity, styled=styled)
            print(
                f"    {severity} {diagnostic.code} "
                f"[{diagnostic.stage}] {diagnostic.message}"
            )


def _diagnostic_summary_text(summary: Mapping[str, int]) -> str:
    return ", ".join(
        (
            _count_text(summary["errors"], "error", "errors"),
            _count_text(summary["warnings"], "warning", "warnings"),
            _count_text(summary["infos"], "info", "infos"),
        )
    )


def _count_text(count: int, singular: str, plural: str) -> str:
    noun = singular if count == 1 else plural
    return f"{count} {noun}"


def _group_diagnostics(diagnostics: ProjectDiagnostics):
    grouped = {}
    for diagnostic in diagnostics:
        key = (diagnostic.report, diagnostic.source)
        grouped.setdefault(key, []).append(diagnostic)
    return grouped


def _diagnostic_sort_key(diagnostic) -> tuple[object, ...]:
    return (
        diagnostic.stage,
        _SEVERITY_ORDER.get(diagnostic.severity, len(_SEVERITY_ORDER)),
        diagnostic.code,
        diagnostic.message,
    )


def _style_stdout() -> bool:
    return sys.stdout.isatty() and "NO_COLOR" not in os.environ


def _format_severity(severity: str, *, styled: bool) -> str:
    label = severity.upper()
    if not styled:
        return label
    colors = {"error": "31", "warning": "33", "info": "36"}
    glyphs = {"error": "✖", "warning": "▲", "info": "ℹ"}
    color = colors.get(severity, "0")
    glyph = glyphs.get(severity, "-")
    return f"\x1b[{color}m{glyph} {label}\x1b[0m"


def _format_candidates(candidates: Iterable[str]) -> str:
    names = tuple(candidates)
    if not names:
        return "<none>"
    return ", ".join(names)


def _project_config_path(project: str | Path) -> Path:
    path = Path(project).expanduser()
    if path.is_dir():
        return path / "volt.toml"
    return path


def _required_table(
    payload: Mapping[str, Any], name: str, config_path: Path
) -> Mapping[str, Any]:
    table = payload.get(name)
    if isinstance(table, dict):
        return table
    raise CliError(f"{config_path} must define a [{name}] table.")


def _optional_table(
    payload: Mapping[str, Any], name: str, config_path: Path
) -> Mapping[str, Any]:
    table = payload.get(name, {})
    if isinstance(table, dict):
        return table
    raise CliError(f"{config_path} [{name}] must be a table.")


def _manufacturing_profile(
    payload: Mapping[str, Any],
    root: Path,
    config_path: Path,
) -> tuple[str | None, Path | None]:
    table = payload.get("manufacturing")
    if table is None:
        return None, None
    if not isinstance(table, dict):
        raise CliError(f"{config_path} [manufacturing] must be a table.")
    value = table.get("profile")
    if value is None:
        return None, None
    if not isinstance(value, str) or not value.strip():
        raise CliError(
            f"{config_path} [manufacturing].profile must be a non-empty string path."
        )
    profile_path = value.strip()
    path = Path(profile_path).expanduser()
    if not path.is_absolute():
        path = root / path
    return profile_path, path


def _resolve_project_path(root: Path, name: str, value: object, config_path: Path) -> Path:
    if not isinstance(value, str) or not value.strip():
        raise CliError(f"{config_path} [paths].{name} must be a non-empty string path.")
    path = Path(value).expanduser()
    if path.is_absolute():
        return path
    return root / path


def _split_entrypoint(entrypoint: str) -> tuple[str, str]:
    module_name, separator, function_name = entrypoint.partition(":")
    if separator != ":" or not module_name or not function_name:
        raise CliError(f"Project entrypoint {entrypoint!r} must use module:function.")
    return module_name, function_name


def _init_root(name: str | None) -> Path:
    if name is None:
        return Path.cwd()
    path = Path(name).expanduser()
    if path.is_absolute():
        return path
    return Path.cwd() / path


def _starter_config(*, profile_path: Path) -> str:
    return f"""[project]
entrypoint = "main:main"

[paths]
artifacts = "build"

[manufacturing]
profile = "{profile_path.as_posix()}"
"""


def _starter_entrypoint(project_name: str) -> str:
    project_literal = json.dumps(project_name)
    return f"""from pathlib import Path
import tomllib

import volt


def _configured_profile_path():
    with Path("volt.toml").open("rb") as handle:
        config = tomllib.load(handle)
    return Path(config["manufacturing"]["profile"])


def main():
    project = volt.Project({project_literal})

    @project.design
    def design():
        return volt.Design({project_literal})

    @project.board
    def board(context):
        design = context.design()
        board = design.board("Main")
        front = board.add_layer("F.Cu", role="copper", side="top")
        back = board.add_layer("B.Cu", role="copper", side="bottom")
        board.set_layer_stack((front, back), thickness=1.6)
        board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))
        profile = volt.CapabilityProfile.from_file(_configured_profile_path())
        board.set_design_rules(
            copper_clearance=0.25,
            min_track_width=0.20,
            min_via_drill=0.30,
            min_via_annular=0.60,
            board_outline_clearance=0.30,
        )
        board.set_capability_profile(profile)
        return board

    return project.run()
"""


def _starter_profile(profile: str) -> dict[str, object]:
    profiles = {
        "jlcpcb": _capability_profile_payload(
            name="JLCPCB 2-layer FR-4 capability snapshot",
            source="https://jlcpcb.com/capabilities/pcb-capabilities",
            as_of="2026-06-13",
            minimum_track_width=0.10,
            minimum_via_drill=0.15,
            minimum_via_annular=0.25,
            minimum_clearances=(
                ("track", "track", 0.10),
                ("track", "pad", 0.15),
                ("track", "via", 0.20),
            ),
            copper_weight_refinements=((1.0, 0.10, 0.10), (2.0, 0.16, 0.16)),
            supported_copper_layer_counts=(2,),
            board_thickness_range=(0.4, 2.0),
            available_copper_weights=(1.0, 2.0, 2.5, 3.5, 4.5),
            drill_diameter_range=(0.15, 6.3),
        ),
        "oshpark": _capability_profile_payload(
            name="OSH Park 2-layer starter capability template",
            source="Starter template; verify against OSH Park capabilities before fabrication",
            as_of="2026-06-20",
            minimum_track_width=0.1524,
            minimum_via_drill=0.254,
            minimum_via_annular=0.508,
            minimum_clearances=(("track", "track", 0.1524), ("track", "pad", 0.1524)),
            supported_copper_layer_counts=(2,),
            board_thickness_range=(1.55, 1.65),
            available_copper_weights=(1.0,),
            drill_diameter_range=(0.254, 6.3),
        ),
        "pcbway": _capability_profile_payload(
            name="PCBWay 2-layer starter capability template",
            source="Starter template; verify against PCBWay capabilities before fabrication",
            as_of="2026-06-20",
            minimum_track_width=0.15,
            minimum_via_drill=0.30,
            minimum_via_annular=0.60,
            minimum_clearances=(("track", "track", 0.15), ("track", "pad", 0.15)),
            supported_copper_layer_counts=(2,),
            board_thickness_range=(0.4, 2.0),
            available_copper_weights=(1.0, 2.0),
            drill_diameter_range=(0.30, 6.3),
        ),
        "generic": _capability_profile_payload(
            name="Generic 2-layer starter capability template",
            source="Volt starter template; replace with fabricator-published capabilities",
            as_of="2026-06-20",
            minimum_track_width=0.20,
            minimum_via_drill=0.30,
            minimum_via_annular=0.60,
            minimum_clearances=(("track", "track", 0.20), ("track", "pad", 0.20)),
            supported_copper_layer_counts=(2,),
            board_thickness_range=(0.8, 1.6),
            available_copper_weights=(1.0,),
            drill_diameter_range=(0.30, 6.0),
        ),
    }
    return profiles[profile]


def _capability_profile_payload(
    *,
    name: str,
    source: str,
    as_of: str,
    minimum_track_width: float,
    minimum_via_drill: float,
    minimum_via_annular: float,
    minimum_clearances: tuple[tuple[str, str, float], ...],
    copper_weight_refinements: tuple[tuple[float, float, float], ...] = (),
    supported_copper_layer_counts: tuple[int, ...] = (),
    board_thickness_range: tuple[float, float] | None = None,
    available_copper_weights: tuple[float, ...] = (),
    drill_diameter_range: tuple[float, float] | None = None,
) -> dict[str, object]:
    profile: dict[str, object] = {
        "name": name,
        "provenance": {"source": source, "as_of": as_of},
        "minimum_track_width_mm": minimum_track_width,
        "minimum_via_drill_mm": minimum_via_drill,
        "minimum_via_annular_mm": minimum_via_annular,
        "minimum_clearances": [
            {"first": first, "second": second, "clearance_mm": clearance}
            for first, second, clearance in minimum_clearances
        ],
    }
    if copper_weight_refinements:
        profile["copper_weight_refinements"] = [
            {
                "copper_weight_oz": copper_weight,
                "minimum_track_width_mm": track_width,
                "minimum_clearance_mm": clearance,
            }
            for copper_weight, track_width, clearance in copper_weight_refinements
        ]
    if supported_copper_layer_counts:
        profile["supported_copper_layer_counts"] = list(supported_copper_layer_counts)
    if board_thickness_range is not None:
        profile["board_thickness_range_mm"] = {
            "minimum_mm": board_thickness_range[0],
            "maximum_mm": board_thickness_range[1],
        }
    if available_copper_weights:
        profile["available_copper_weights_oz"] = list(available_copper_weights)
    if drill_diameter_range is not None:
        profile["drill_diameter_range_mm"] = {
            "minimum_mm": drill_diameter_range[0],
            "maximum_mm": drill_diameter_range[1],
        }
    return {
        "format": "volt.capability_profile",
        "version": 1,
        "profile": profile,
    }


def _evict_project_entrypoint_modules(module_name: str, root: Path) -> None:
    top_level_module = module_name.partition(".")[0]
    if not _root_provides_module(root, top_level_module):
        return

    package_prefix = f"{top_level_module}."
    for cached_name in list(sys.modules):
        if cached_name == top_level_module or cached_name.startswith(package_prefix):
            del sys.modules[cached_name]


def _root_provides_module(root: Path, module_name: str) -> bool:
    return (
        importlib.machinery.PathFinder.find_spec(module_name, [str(root)]) is not None
    )


@contextmanager
def _project_runtime(root: Path):
    previous_cwd = Path.cwd()
    previous_sys_path = list(sys.path)
    root_text = str(root)
    sys.path.insert(0, root_text)
    os.chdir(root)
    try:
        yield
    finally:
        os.chdir(previous_cwd)
        sys.path[:] = previous_sys_path
