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
from ..project import Project, ProjectResult, _diagnostics_payload
from ..schematic import Schematic


EXIT_SUCCESS = 0
EXIT_CHECK_FAILED = 1
EXIT_COMMAND_FAILED = 2


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
        handler(args)
    except CliError as error:
        print(f"volt: error: {error}", file=sys.stderr)
        return error.exit_code
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
    return ProjectConfig(
        root=root,
        config_path=config_path,
        entrypoint=entrypoint.strip(),
        paths=paths,
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


def _forward_project_stdout(stream: StringIO) -> None:
    text = stream.getvalue()
    if text:
        print(text, file=sys.stderr, end="")


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
        "volt.ProjectResult or volt.Project for 'volt model --json'."
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
