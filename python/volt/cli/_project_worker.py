"""Isolated subprocess worker for project-source execution."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Sequence

from ..project import _diagnostics_payload, _tests_payload
from ..project_bundle import ProjectBundle
from . import (
    CliError,
    EXIT_CHECK_FAILED,
    EXIT_COMMAND_FAILED,
    EXIT_SUCCESS,
    _project_result_with_forwarded_stdout,
    load_project_config,
)


_WORKER_FORMAT = "volt.project-worker-response"


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="volt-project-worker")
    parser.add_argument(
        "action",
        choices=("check", "build"),
    )
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--profile", default="default")
    parser.add_argument("--exports-json", default="[]")
    parser.add_argument("--design")
    parser.add_argument("--schematic")
    parser.add_argument("--board")
    return parser


def _outcome(result) -> dict[str, object]:
    tests = _tests_payload(result._test_results())
    return {
        "ok": result.ok,
        "status": result.status,
        "structurally_valid": True,
        "target_ready": result.expected_diagnostics_ok,
        "diagnostics": _diagnostics_payload(result),
        "tests": tests,
        "stages": [
            {
                "name": stage.name,
                "model_count": stage.model_count,
                "tests": _tests_payload(stage.tests),
            }
            for stage in result.stages
        ],
        "models": {
            "logical": len(result.designs),
            "schematics": len(result.schematics),
            "boards": len(result.boards),
        },
    }


def _success(payload: dict[str, object], exit_code: int) -> dict[str, object]:
    return {
        "format": _WORKER_FORMAT,
        "schema_version": 1,
        "ok": True,
        "exit_code": exit_code,
        "result": payload,
    }


def _one(items: tuple[object, ...], selector: str | None, kind: str):
    if selector is None:
        if len(items) == 1:
            return items[0]
        raise CliError(
            f"{kind} selector is required. Candidates: "
            + (", ".join(item.name for item in items) if items else "<none>"),
            code=f"{kind.lower()}-selector-required",
        )
    matches = tuple(item for item in items if item.name == selector)
    if len(matches) == 1:
        return matches[0]
    raise CliError(
        f"No exact {kind} selector {selector!r}. Candidates: "
        + (", ".join(item.name for item in items) if items else "<none>"),
        code=f"invalid-{kind.lower()}-selector",
    )


def _board(result, selector: str | None):
    boards = result.boards
    candidates = tuple(f"{board._design.name}:{board.name}" for board in boards)
    if not boards:
        raise CliError(
            "Project has no Boards. Candidates: <none>",
            code="invalid-board-selector",
        )
    if len(boards) == 1:
        board = boards[0]
        if selector in (None, board.name, candidates[0]):
            return board
    elif selector is None or ":" not in selector:
        raise CliError(
            "Multiple named Boards require an exact design:Board selector. Candidates: "
            + (", ".join(candidates) if candidates else "<none>"),
            code="exact-board-selector-required",
        )
    matches = tuple(
        board
        for board, candidate in zip(boards, candidates, strict=True)
        if candidate == selector
    )
    if len(matches) == 1:
        return matches[0]
    raise CliError(
        f"No exact Board selector {selector!r}. Candidates: "
        + (", ".join(candidates) if candidates else "<none>"),
        code="invalid-board-selector",
    )


def _schematic(result, selector: str | None):
    schematics = result.schematics
    candidates = tuple(
        f"{schematic._design.name}:{schematic.name}" for schematic in schematics
    )
    if len(schematics) == 1:
        schematic = schematics[0]
        if selector in (None, schematic.name, candidates[0]):
            return schematic
    matches = tuple(
        schematic
        for schematic, candidate in zip(schematics, candidates, strict=True)
        if candidate == selector
    )
    if len(matches) == 1:
        return matches[0]
    raise CliError(
        "Schematic export requires an exact design:Schematic selector. Candidates: "
        + (", ".join(candidates) if candidates else "<none>"),
        code="invalid-schematic-selector",
    )


def _resolved_export_requests(
    result, requests: list[dict[str, object]]
) -> tuple[dict[str, str], ...]:
    resolved: list[dict[str, str]] = []
    for request in requests:
        kind = request.get("kind")
        if not isinstance(kind, str):
            raise CliError(
                "Selected export kind must be a string.",
                code="invalid-export-selection",
            )
        if kind == "bom":
            design = _one(result.designs, request.get("design"), "Design")
            resolved.append({"kind": kind, "design": design.name})
            continue
        if kind == "schematic_svg":
            schematic = _schematic(result, request.get("schematic"))
            resolved.append(
                {
                    "kind": kind,
                    "design": schematic._design.name,
                    "name": schematic.name,
                }
            )
            continue
        if kind == "step":
            part = request.get("part")
            if part is not None and not isinstance(part, str):
                raise CliError(
                    "STEP part selector must be a string.",
                    code="invalid-part-selector",
                )
            item = {"kind": kind}
            if part is not None:
                item["part"] = part
            resolved.append(item)
            continue
        board = _board(result, request.get("board"))
        item = {
            "kind": kind,
            "design": board._design.name,
            "name": board.name,
        }
        if kind == "board_layer_image":
            layer = request.get("layer")
            if not isinstance(layer, str) or not layer:
                raise CliError(
                    "board-layer-image requires --layer with an exact layer name.",
                    code="board-layer-selector-required",
                )
            item["layer"] = layer
        resolved.append(item)
    return tuple(resolved)


def _failure(error: Exception) -> dict[str, object]:
    if isinstance(error, CliError):
        code = error.code
        exit_code = error.exit_code
    else:
        code = "project-execution-failed"
        exit_code = EXIT_COMMAND_FAILED
    return {
        "format": _WORKER_FORMAT,
        "schema_version": 1,
        "ok": False,
        "exit_code": exit_code,
        "error": {
            "type": type(error).__name__,
            "code": code,
            "message": str(error),
        },
    }


def _run(args: argparse.Namespace) -> tuple[dict[str, object], int]:
    config = load_project_config(args.config)
    result = _project_result_with_forwarded_stdout(config)
    if args.action == "check":
        payload = _outcome(result)
        return payload, EXIT_SUCCESS if result.ok else EXIT_CHECK_FAILED
    if args.action == "build":
        if args.output is None:
            raise CliError("Build worker requires an output path.", code="missing-output")
        exports = json.loads(args.exports_json)
        if not isinstance(exports, list) or not all(
            isinstance(item, dict) for item in exports
        ):
            raise CliError(
                "Build selected exports must be a JSON array of objects.",
                code="invalid-export-selection",
            )
        export_requests = _resolved_export_requests(result, exports)
        try:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            if export_requests:
                result._write_selected_bundle(
                    args.output,
                    profile=args.profile,
                    selected_exports=list(export_requests),
                )
            else:
                result.write(args.output, profile=args.profile)
        except Exception as error:
            raise CliError(
                f"ProjectBundle write failed: {error}",
                code=(
                    "selected-export-failed"
                    if export_requests
                    else "bundle-write-failed"
                ),
            ) from error
        graph = ProjectBundle.open(args.output).v2
        payload = {
            **_outcome(result),
            "output": str(args.output),
            "written": True,
            "build_id": graph.build_id,
            "bundle_digest": graph.bundle_digest,
            "artifact_count": len(graph.artifacts),
            "selected_export_count": len(graph.loaded_project.selected_exports),
        }
        return payload, EXIT_SUCCESS if result.ok else EXIT_CHECK_FAILED
    raise AssertionError(f"Unsupported project worker action: {args.action}")


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        payload, exit_code = _run(args)
        response = _success(payload, exit_code)
    except Exception as error:
        response = _failure(error)
        exit_code = int(response["exit_code"])
    print(json.dumps(response, separators=(",", ":"), sort_keys=True))
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
