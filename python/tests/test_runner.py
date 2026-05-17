"""Small stdlib runner for Volt Python tests.

CTest owns the top-level test workflow because CMake also owns the Python
interpreter that builds the private ``_volt`` extension module. This runner keeps
Python test discovery dependency-free while still letting CTest register each
``test_*`` function as a separate test. It expects CTest to provide the build
tree's Python package directory through ``PYTHONPATH``.
"""

from __future__ import annotations

import argparse
import ast
import importlib.util
import sys
import traceback
from pathlib import Path
from types import ModuleType


TEST_ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = TEST_ROOT.parents[1]


def _ensure_import_paths() -> None:
    for path in (PROJECT_ROOT, TEST_ROOT):
        text = str(path)
        if text not in sys.path:
            sys.path.insert(0, text)


def _test_functions(path: Path) -> list[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    # Keep discovery intentionally narrow: module-level synchronous test functions only.
    return [
        node.name
        for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name.startswith("test_")
    ]


def _module_name(path: Path) -> str:
    relative = path.resolve().relative_to(TEST_ROOT)
    parts = list(relative.with_suffix("").parts)
    return "volt_python_tests." + ".".join(parts)


def _load_module(path: Path) -> ModuleType:
    _ensure_import_paths()
    spec = importlib.util.spec_from_file_location(_module_name(path), path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load Python test module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _labels_for(path: Path) -> str:
    relative = path.resolve().relative_to(TEST_ROOT)
    labels = ["python"]
    if relative.parts and relative.parts[0] == "examples":
        labels.extend(["example", "integration"])
    return ",".join(labels)


def _test_name(path: Path, function: str) -> str:
    relative = path.resolve().relative_to(TEST_ROOT).with_suffix("")
    path_name = ".".join(relative.parts)
    return f"python.{path_name}.{function.removeprefix('test_')}"


def discover_cmake(paths: list[Path]) -> int:
    for path in paths:
        for function in _test_functions(path):
            print(
                f"{_test_name(path, function)}|"
                f"{path.resolve()}|"
                f"{function}|"
                f"{_labels_for(path)}"
            )
    return 0


def run_one(path: Path, function: str) -> int:
    module = _load_module(path)
    test = getattr(module, function)
    try:
        test()
    except Exception:
        traceback.print_exc()
        return 1
    print(f"PASS {path.name}::{function}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    discover = subparsers.add_parser("discover-cmake")
    discover.add_argument("paths", nargs="+", type=Path)

    run = subparsers.add_parser("run")
    run.add_argument("path", type=Path)
    run.add_argument("function")

    args = parser.parse_args(argv)
    if args.command == "discover-cmake":
        return discover_cmake(args.paths)
    if args.command == "run":
        return run_one(args.path, args.function)
    raise AssertionError(args.command)


if __name__ == "__main__":
    raise SystemExit(main())
