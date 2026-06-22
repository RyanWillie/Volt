"""Shared deterministic manufacturing package assembly."""

from __future__ import annotations

import html
import json
import os
import shutil
import tempfile
import zipfile
from pathlib import Path
from typing import Any

from ._project_model_lookup import model_output_name
from .pcb import Board
from .project import (
    ManufacturingPackageError,
    ManufacturingPackageResult,
    ProjectResult,
    _diagnostics_payload,
)


def write_project_manufacturing_package(
    result: ProjectResult,
    *,
    output: Path,
    board_selector: str | None,
    manufacturing_profile: dict[str, str] | None,
    archive: bool,
) -> ManufacturingPackageResult:
    """Write a deterministic manufacturing package from a project result."""
    board = select_manufacturing_board(result, board_selector)
    board_record = manufacturing_board_record(board, result)
    diagnostics = _diagnostics_payload(result)

    if not result.ok:
        raise ManufacturingPackageError(
            "Manufacturing export refused because the project result is not ok.",
            status=result.status,
            output=output,
            board=board_record,
            diagnostics=diagnostics,
        )

    profile_payload = _required_profile_payload(
        board,
        manufacturing_profile=manufacturing_profile,
        output=output,
        board_record=board_record,
        diagnostics=diagnostics,
    )
    native_export = board.to_fabrication_files()
    native_payload = native_fabrication_payload(native_export)
    if native_payload["coverage"]["fab_critical_loss"]:
        raise ManufacturingPackageError(
            "Manufacturing export refused because native fabrication reported fab-critical loss.",
            status="native-fabrication-loss",
            output=output,
            board=board_record,
            diagnostics=diagnostics,
            native_fabrication=native_payload,
        )

    package = _write_manufacturing_package(
        result,
        board=board,
        board_record=board_record,
        output=output,
        native_export=native_export,
        native_payload=native_payload,
        profile_payload=profile_payload,
        board_selector=board_selector,
        archive=archive,
    )
    archive_path = package["archive"]
    return ManufacturingPackageResult(
        output=output,
        board=board_record,
        status=result.status,
        archive=None if archive_path is None else Path(archive_path),
        native_fabrication=native_payload,
    )


def select_manufacturing_board(result: ProjectResult, selector: str | None) -> Board:
    """Select the board to export using project-result projection lookup rules."""
    if selector is not None:
        return _select_projection(result.boards, result.boards, selector, "board")
    if not result.boards:
        raise LookupError("Project result has no boards to export for manufacturing.")
    if len(result.boards) > 1:
        candidates = _format_candidates(
            model_output_name(board, result.boards) for board in result.boards
        )
        raise LookupError(
            "Project result has multiple boards; pass --board or board=. "
            f"Candidates: {candidates}"
        )
    return result.boards[0]


def manufacturing_board_record(board: Board, result: ProjectResult) -> dict[str, str]:
    """Return stable board metadata used by package manifests and summaries."""
    return {
        "design": board._design.name,
        "name": board.name,
        "output_name": model_output_name(board, result.boards),
    }


def _required_profile_payload(
    board: Board,
    *,
    manufacturing_profile: dict[str, str] | None,
    output: Path,
    board_record: dict[str, str],
    diagnostics: dict[str, object],
) -> dict[str, object]:
    if manufacturing_profile is None:
        raise ManufacturingPackageError(
            "Manufacturing export requires manufacturing profile metadata.",
            status="missing-manufacturing-profile",
            output=output,
            board=board_record,
            diagnostics=diagnostics,
        )

    profile_config = _required_profile_config(
        manufacturing_profile,
        output=output,
        board=board_record,
        diagnostics=diagnostics,
    )
    board_profile = _board_capability_profile(board)
    if board_profile is None:
        raise ManufacturingPackageError(
            "Manufacturing export requires a board capability profile.",
            status="missing-board-capability-profile",
            output=output,
            board=board_record,
            diagnostics=diagnostics,
        )

    return {
        "config": profile_config,
        "board": board_profile,
    }


def _required_profile_config(
    manufacturing_profile: dict[str, str],
    *,
    output: Path,
    board: dict[str, str],
    diagnostics: dict[str, object],
) -> dict[str, str]:
    required_fields = ("path", "resolved_path")
    missing_fields = [
        field
        for field in required_fields
        if not isinstance(manufacturing_profile.get(field), str)
        or not manufacturing_profile[field].strip()
    ]
    if missing_fields:
        formatted = ", ".join(missing_fields)
        raise ManufacturingPackageError(
            f"Manufacturing export requires profile metadata field(s): {formatted}.",
            status="missing-manufacturing-profile",
            output=output,
            board=board,
            diagnostics=diagnostics,
        )
    return dict(manufacturing_profile)


def native_fabrication_payload(native_export: Any) -> dict[str, object]:
    """Return native fabrication loss and coverage metadata for manifests."""
    warnings = [
        {
            "kind": warning.kind,
            "construct": warning.construct,
            "message": warning.message,
            "severity": warning.severity,
            "fabrication_impact": warning.fabrication_impact,
        }
        for warning in native_export.warnings
    ]
    fab_critical = any(
        warning["fabrication_impact"] == "fab-critical" for warning in warnings
    )
    return {
        "coverage": {
            "classification": "fab-critical-loss" if fab_critical else "complete",
            "fab_critical_loss": fab_critical,
        },
        "warnings": warnings,
        "diagnostics": _native_diagnostics_payload(native_export.diagnostics),
        "exporter": native_export.exporter,
    }


def _write_manufacturing_package(
    result: ProjectResult,
    *,
    board: Any,
    board_record: dict[str, str],
    output: Path,
    native_export: Any,
    native_payload: dict[str, object],
    profile_payload: dict[str, object],
    board_selector: str | None,
    archive: bool,
) -> dict[str, str | None]:
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    try:
        result.write(staging)
        _write_manufacturing_contents(
            result,
            board=board,
            board_record=board_record,
            output=staging,
            native_export=native_export,
            native_payload=native_payload,
            profile_payload=profile_payload,
            board_selector=board_selector,
            archive=archive,
        )
        _replace_directory(staging, output)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise

    archive_path = None
    if archive:
        archive_path = _write_deterministic_archive(output)
    else:
        _remove_deterministic_archive(output)
    return {"archive": None if archive_path is None else str(archive_path)}


def _write_manufacturing_contents(
    result: ProjectResult,
    *,
    board: Any,
    board_record: dict[str, str],
    output: Path,
    native_export: Any,
    native_payload: dict[str, object],
    profile_payload: dict[str, object],
    board_selector: str | None,
    archive: bool,
) -> None:
    manufacturing_root = output / "manufacturing"

    native_files = []
    for file in native_export.files:
        subdir = "drill" if file.function.startswith("drill") else "gerber"
        relative = Path("manufacturing") / "fabrication" / subdir / file.filename
        _write_text(output / relative, file.text)
        native_files.append(
            {
                "filename": file.filename,
                "function": file.function,
                "path": relative.as_posix(),
                "media_type": (
                    "application/x-excellon"
                    if subdir == "drill"
                    else "application/x-gerber"
                ),
            }
        )

    _write_json(manufacturing_root / "profile.json", profile_payload)

    native_report = {
        **native_payload,
        "files": native_files,
    }
    _write_json(manufacturing_root / "native-fabrication.json", native_report)

    inspection_path = Path("manufacturing") / "inspection.html"
    _write_text(
        output / inspection_path,
        _manufacturing_inspection_html(
            project_name=result.project.name,
            board=board_record,
            native_files=native_files,
        ),
    )

    artifacts = _manufacturing_artifact_records(output, board_record)
    artifacts.extend(
        [
            {
                "kind": "profile",
                "name": "Manufacturing profile metadata",
                "path": "manufacturing/profile.json",
                "media_type": "application/json",
            },
            {
                "kind": "native_fabrication_report",
                "name": "Native fabrication loss and coverage report",
                "path": "manufacturing/native-fabrication.json",
                "media_type": "application/json",
            },
            {
                "kind": "inspection_html",
                "name": "Native fabrication inspection index",
                "path": inspection_path.as_posix(),
                "media_type": "text/html",
            },
        ]
    )

    manifest = {
        "format": "volt.manufacturing_package",
        "schema_version": 1,
        "project": {
            "name": result.project.name,
            "version": result.project.version,
            "description": result.project.description,
        },
        "command": {
            "name": "volt export manufacturing",
            "board": board_selector,
            "archive": archive,
        },
        "board": board_record,
        "profile": profile_payload,
        "exporter": native_payload["exporter"],
        "diagnostics": {
            "path": "diagnostics/diagnostics.json",
            "status": result.status,
            "summary": _diagnostics_payload(result)["summary"],
        },
        "native_fabrication": {
            **native_payload,
            "files": native_files,
        },
        "artifacts": artifacts,
    }
    _write_json(manufacturing_root / "manifest.json", manifest)


def _select_projection(
    models: tuple[Board, ...],
    all_models: tuple[Board, ...],
    selector: str,
    kind: str,
) -> Board:
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
        raise LookupError(
            f"Ambiguous {kind} selector {selector!r}. Candidates: {candidates}"
        )
    raise LookupError(f"No {kind} named {selector!r}. Candidates: {candidates}")


def _format_candidates(candidates) -> str:
    names = tuple(candidates)
    if not names:
        return "<none>"
    return ", ".join(names)


def _replace_directory(source: Path, destination: Path) -> None:
    if destination.exists():
        if destination.is_dir():
            shutil.rmtree(destination)
        else:
            destination.unlink()
    source.replace(destination)


def _write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _write_json(path: Path, payload: object) -> None:
    _write_text(path, json.dumps(payload, indent=2, sort_keys=True) + "\n")


def _board_capability_profile(board: Any) -> dict[str, object] | None:
    return json.loads(board.to_json())["board"].get("capability_profile")


def _native_diagnostics_payload(diagnostics: Any) -> dict[str, object]:
    items = [_native_diagnostic_payload(diagnostic) for diagnostic in diagnostics]
    return {
        "summary": {
            "errors": sum(1 for item in items if item["severity"] == "error"),
            "warnings": sum(1 for item in items if item["severity"] == "warning"),
            "infos": sum(1 for item in items if item["severity"] == "info"),
        },
        "diagnostics": items,
    }


def _native_diagnostic_payload(diagnostic: Any) -> dict[str, object]:
    return {
        "severity": diagnostic.severity,
        "category": diagnostic.category,
        "code": diagnostic.code,
        "message": diagnostic.message,
        "entities": [
            {"kind": entity.kind, "index": entity.index}
            for entity in diagnostic.entities
        ],
        "overlays": [
            {
                "kind": overlay.kind,
                "points": [list(point) for point in overlay.points],
                "entities": [
                    {"kind": entity.kind, "index": entity.index}
                    for entity in overlay.entities
                ],
                "layers": [
                    {"kind": entity.kind, "index": entity.index}
                    for entity in overlay.layers
                ],
            }
            for overlay in diagnostic.overlays
        ],
        "measurement": (
            None
            if diagnostic.measurement is None
            else {
                "actual_mm": diagnostic.measurement.actual_mm,
                "required_mm": diagnostic.measurement.required_mm,
            }
        ),
        "rule": diagnostic.rule,
    }


def _manufacturing_artifact_records(
    output: Path,
    board_record: dict[str, str],
) -> list[dict[str, object]]:
    project_manifest = json.loads(
        (output / "manifest.volt.json").read_text(encoding="utf-8")
    )
    records: list[dict[str, object]] = [
        {
            "kind": "project_manifest",
            "name": "Volt project result manifest",
            "path": "manifest.volt.json",
            "media_type": "application/json",
        }
    ]
    for artifact in project_manifest["artifacts"]:
        kind = artifact["kind"]
        group = artifact.get("group", {})
        if kind in {"bom", "bom_csv"} and group.get("design") == board_record["design"]:
            records.append(artifact)
        elif (
            kind in {"cpl", "cpl_csv"}
            and group.get("design") == board_record["design"]
            and group.get("board") == board_record["name"]
        ):
            records.append(artifact)
        elif kind == "diagnostics":
            records.append(artifact)
    return records


def _manufacturing_inspection_html(
    *,
    project_name: str,
    board: dict[str, str],
    native_files: list[dict[str, str]],
) -> str:
    file_items = "\n".join(
        "      <li>"
        f"<a href=\"{html.escape(_relative_href('manufacturing/inspection.html', item['path']))}\">"
        f"{html.escape(item['filename'])}</a> "
        f"<span>{html.escape(item['function'])}</span>"
        "</li>"
        for item in native_files
    )
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Native fabrication inspection</title>
  <style>
    body {{ font-family: system-ui, sans-serif; max-width: 760px; margin: 40px auto; line-height: 1.5; }}
    h1 {{ font-size: 28px; margin-bottom: 4px; }}
    p {{ color: #334155; }}
    li {{ margin: 6px 0; }}
    span {{ color: #475569; margin-left: 8px; }}
  </style>
</head>
<body>
  <h1>Native fabrication inspection</h1>
  <p>{html.escape(project_name)} / {html.escape(board["output_name"])}</p>
  <p>Open these RS-274X Gerber and Excellon drill files in a standards-based viewer for manual review. KiCad is not required for this smoke inspection path.</p>
  <ul>
{file_items}
  </ul>
</body>
</html>
"""


def _relative_href(from_path: str, to_path: str) -> str:
    return os.path.relpath(to_path, start=str(Path(from_path).parent)).replace(os.sep, "/")


def _write_deterministic_archive(root: Path) -> Path:
    archive_path = _deterministic_archive_path(root)
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            info = zipfile.ZipInfo(path.relative_to(root).as_posix())
            info.date_time = (1980, 1, 1, 0, 0, 0)
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, path.read_bytes())
    return archive_path


def _remove_deterministic_archive(root: Path) -> None:
    archive_path = _deterministic_archive_path(root)
    if archive_path.is_file():
        archive_path.unlink()


def _deterministic_archive_path(root: Path) -> Path:
    return root.with_suffix(".zip") if root.suffix else Path(f"{root}.zip")
