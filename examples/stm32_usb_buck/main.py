"""Generate the Volt-native STM32 USB buck benchmark artifacts."""

from __future__ import annotations

from pathlib import Path

import volt

from .schematic_connectors import _author_connectors_region
from .schematic_mcu import _author_mcu_region
from .schematic_output import SHEET_FILE, SHEET_OPTIONS
from .schematic_power import _author_power_region
from .stm32_board import Stm32UsbBuckBoard, build_board


def build_project() -> volt.Project:
    project = volt.Project("stm32-usb-buck", description="STM32 USB buck reference design")
    project.expect_diagnostic(code="SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED", severity="warning")
    project.expect_diagnostic(code="SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW", severity="warning")
    project.expect_diagnostic(code="SCHEMATIC_SYMBOL_FIELD_FAR_FROM_SYMBOL", severity="warning")
    project.expect_diagnostic(code="SCHEMATIC_LABEL_CROWDS_SYMBOL", severity="warning")
    project.expect_diagnostic(code="SCHEMATIC_DENSE_PORT_TAGS", severity="warning")

    @project.design
    def design():
        board = build_board()
        return board.design, volt.ProjectResource("stm32_board", board)

    @project.schematic
    def schematic(context: volt.BuildContext) -> volt.Schematic:
        board = context.resource("stm32_board", Stm32UsbBuckBoard)
        nets = {net.name: net for net in board.design.nets()}

        sheet = board.design.schematic(
            "STM32 USB Buck",
            title="STM32 USB Buck Reference Schematic",
            number=1,
            page_count=1,
            file=SHEET_FILE,
            **SHEET_OPTIONS,
        )

        power_region = sheet.region(
            "Power Circuitry",
            x=18,
            y=18,
            w=558,
            h=116,
            style={"border": "dashed"},
        )
        mcu_region = sheet.region(
            "STM32 Microcontroller",
            x=18,
            y=140,
            w=346,
            h=266,
            title="STM32 MCU",
            style={"border": "dashed"},
        )
        connectors_region = sheet.region(
            "Connectors and USB",
            x=370,
            y=140,
            w=208,
            h=216,
            style={"border": "dashed"},
        )

        _author_power_region(power_region, board, nets)
        _author_mcu_region(mcu_region, board, nets)
        _author_connectors_region(connectors_region, board, nets)
        return sheet

    @project.schematic.test
    def schematic_places_primary_components(check) -> None:
        check.places("VIN_SRC", "U1", "J2", "J3")

    return project


def run_project() -> volt.ProjectResult:
    return build_project().run()


def write_artifacts(output_dir: Path | str | None = None) -> volt.ProjectArtifactPaths:
    if output_dir is None:
        output_dir = Path(__file__).resolve().parent / "artifacts"
    output_path = Path(output_dir)

    result = run_project()
    if not result.ok:
        diagnostics = [
            f"{diagnostic.report}:{diagnostic.code}"
            for diagnostic in result.unexpected_diagnostics
        ]
        diagnostics.extend(
            f"missing:{expectation.code}"
            for expectation in result.missing_expected_diagnostics
        )
        raise RuntimeError("STM32 USB buck validation failed: " + ", ".join(diagnostics))
    output_path.mkdir(parents=True, exist_ok=True)
    project_bundle = output_path / "stm32_usb_buck.volt"
    result.write(project_bundle)
    artifacts = result.write_artifacts(output_path, slug="stm32_usb_buck")
    return artifacts


if __name__ == "__main__":
    write_artifacts()
