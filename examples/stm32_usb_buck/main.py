"""Generate the Volt-native STM32 USB buck benchmark artifacts."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import volt

from .schematic_output import build_schematic
from .stm32_board import build_board


@dataclass(frozen=True)
class BenchmarkArtifacts:
    logical_json: Path
    schematic_json: Path
    schematic_svg: Path
    schematic_body_svg: Path
    schematic_svg_pages: tuple[Path, ...]
    validation_report: Path


def validation_report_json(report: volt.DiagnosticReport) -> str:
    counts = {"errors": 0, "warnings": 0, "infos": 0}
    diagnostics = []
    for diagnostic in report:
        if diagnostic.severity == "error":
            counts["errors"] += 1
        elif diagnostic.severity == "warning":
            counts["warnings"] += 1
        else:
            counts["infos"] += 1
        diagnostics.append(
            {
                "severity": diagnostic.severity,
                "code": diagnostic.code,
                "message": diagnostic.message,
                "entities": [
                    {"kind": entity.kind, "index": entity.index}
                    for entity in diagnostic.entities
                ],
            }
        )
    return json.dumps(
        {
            "summary": counts,
            "diagnostics": diagnostics,
        },
        indent=2,
        sort_keys=True,
    ) + "\n"


def require_schematic_ready(schematic: volt.Schematic) -> None:
    report = schematic.validate()
    if not report.has_errors:
        return

    codes = ", ".join(diagnostic.code for diagnostic in report)
    raise RuntimeError(f"STM32 USB buck schematic readiness failed: {codes}")


def write_artifacts(output_dir: Path | str | None = None) -> BenchmarkArtifacts:
    if output_dir is None:
        output_dir = Path(__file__).resolve().parent / "artifacts"
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    board = build_board()
    design = board.design
    schematic = build_schematic(board)
    logical_json = output_path / "stm32_usb_buck.volt.json"
    schematic_json = output_path / "stm32_usb_buck.volt.schematic.json"
    schematic_svg = output_path / "stm32_usb_buck.svg"
    schematic_body_svg = output_path / "stm32_usb_buck.body.svg"
    schematic_svg_pages_dir = output_path / "stm32_usb_buck.pages"
    validation_report = output_path / "stm32_usb_buck.validation.json"

    require_schematic_ready(schematic)
    if schematic_svg_pages_dir.exists():
        for page_path in schematic_svg_pages_dir.glob("*.svg"):
            page_path.unlink()
    design.write(logical_json)
    schematic_json.write_text(schematic.to_json(), encoding="utf-8")
    schematic.write_svg(schematic_svg)
    # Content-tight body SVG is for docs/previews; full sheet/page SVGs remain document artifacts.
    schematic.write_body_svg(schematic_body_svg)
    schematic_svg_pages = schematic.write_svg_pages(
        schematic_svg_pages_dir,
        prefix="stm32_usb_buck",
    )
    validation_report.write_text(validation_report_json(design.validate()), encoding="utf-8")
    return BenchmarkArtifacts(
        logical_json=logical_json,
        schematic_json=schematic_json,
        schematic_svg=schematic_svg,
        schematic_body_svg=schematic_body_svg,
        schematic_svg_pages=schematic_svg_pages,
        validation_report=validation_report,
    )


if __name__ == "__main__":
    write_artifacts()
