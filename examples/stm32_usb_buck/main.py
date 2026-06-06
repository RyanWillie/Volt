"""Generate the Volt-native STM32 USB buck benchmark artifacts."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import volt

from .schematic_output import build_schematic
from .stm32_board import Stm32UsbBuckBoard, build_board


@dataclass(frozen=True)
class BenchmarkArtifacts:
    logical_json: Path
    schematic_json: Path
    schematic_svg: Path
    schematic_body_svg: Path
    schematic_svg_pages: tuple[Path, ...]
    validation_report: Path


def write_artifacts(output_dir: Path | str | None = None) -> BenchmarkArtifacts:
    if output_dir is None:
        output_dir = Path(__file__).resolve().parent / "artifacts"
    output_path = Path(output_dir)
    result = PROJECT.run()
    if not result.ok:
        diagnostics = [
            f"{diagnostic.report}:{diagnostic.code}"
            for diagnostic in result.unexpected_diagnostics
        ]
        raise RuntimeError("STM32 USB buck validation failed: " + ", ".join(diagnostics))
    artifacts = result.write_artifacts(output_path, slug="stm32_usb_buck")
    return BenchmarkArtifacts(
        logical_json=artifacts.logical_json,
        schematic_json=artifacts.schematic_json,
        schematic_svg=artifacts.schematic_svg,
        schematic_body_svg=artifacts.schematic_body_svg,
        schematic_svg_pages=artifacts.schematic_svg_pages,
        validation_report=artifacts.diagnostics_json,
    )


PROJECT = volt.Project("stm32_usb_buck")
PROJECT.expect_diagnostic(code="POWER_INPUT_WITHOUT_SOURCE", severity="error")
PROJECT.expect_diagnostic(code="SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED", severity="warning")
PROJECT.expect_diagnostic(code="SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW", severity="warning")
PROJECT.expect_diagnostic(code="SCHEMATIC_SYMBOL_FIELD_FAR_FROM_SYMBOL", severity="warning")
PROJECT.expect_diagnostic(code="SCHEMATIC_LABEL_CROWDS_SYMBOL", severity="warning")
PROJECT.expect_diagnostic(code="SCHEMATIC_DENSE_PORT_TAGS", severity="warning")


@PROJECT.design
def project_design():
    board = build_board()
    return board.design, volt.ProjectResource("stm32_board", board)


@PROJECT.schematic
def project_schematic(context: volt.BuildContext) -> volt.Schematic:
    return build_schematic(context.resource("stm32_board", Stm32UsbBuckBoard))


if __name__ == "__main__":
    write_artifacts()
