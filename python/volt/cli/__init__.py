"""Command-line plumbing for Volt project discovery and entrypoint loading."""

from __future__ import annotations

import argparse
import importlib
import importlib.machinery
import os
import sys
import tomllib
from collections.abc import Mapping, Sequence
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable


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
