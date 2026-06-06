"""Generate the Volt-native STM32 USB buck benchmark artifacts."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import volt

from .schematic_output import build_schematic
from .stm32_board import Stm32UsbBuckBoard, build_board


@dataclass(frozen=True)
class BenchmarkArtifacts:
    project_bundle: Path
    logical_json: Path
    schematic_json: Path
    schematic_svg: Path
    schematic_body_svg: Path
    schematic_svg_pages: tuple[Path, ...]
    validation_report: Path


def require_schematic_ready(result: volt.ProjectResult) -> None:
    errors = result.diagnostics.errors(stage="schematic")
    failures = result.test_failures()
    if not errors and not failures:
        return

    codes = ", ".join(
        (*[diagnostic.code for diagnostic in errors], *[failure.name for failure in failures])
    )
    raise RuntimeError(f"STM32 USB buck schematic readiness failed: {codes}")


def build_project() -> volt.Project:
    project = volt.Project("stm32-usb-buck", description="STM32 USB buck reference design")
    context: dict[str, Stm32UsbBuckBoard] = {}

    @project.design
    def design() -> volt.Design:
        context["board"] = build_board()
        return context["board"].design

    @project.schematic
    def schematic(_design: volt.Design) -> volt.Schematic:
        return build_schematic(context["board"])

    @project.schematic.test
    def schematic_places_primary_components(check) -> None:
        check.places("VIN_SRC", "U1", "J2", "J3")

    return project


def run_project() -> volt.ProjectResult:
    return build_project().run()


def write_artifacts(output_dir: Path | str | None = None) -> BenchmarkArtifacts:
    if output_dir is None:
        output_dir = Path(__file__).resolve().parent / "artifacts"
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    result = run_project()
    require_schematic_ready(result)
    design = result.design()
    schematic = result.schematic()
    project_bundle = output_path / "stm32_usb_buck.volt"
    logical_json = output_path / "stm32_usb_buck.volt.json"
    schematic_json = output_path / "stm32_usb_buck.volt.schematic.json"
    schematic_svg = output_path / "stm32_usb_buck.svg"
    schematic_body_svg = output_path / "stm32_usb_buck.body.svg"
    schematic_svg_pages_dir = output_path / "stm32_usb_buck.pages"
    validation_report = output_path / "stm32_usb_buck.validation.json"

    result.write(project_bundle)
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
    validation_report.write_text(
        (project_bundle / "diagnostics" / "diagnostics.json").read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    return BenchmarkArtifacts(
        project_bundle=project_bundle,
        logical_json=logical_json,
        schematic_json=schematic_json,
        schematic_svg=schematic_svg,
        schematic_body_svg=schematic_body_svg,
        schematic_svg_pages=schematic_svg_pages,
        validation_report=validation_report,
    )


if __name__ == "__main__":
    write_artifacts()
