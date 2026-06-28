"""Generate the Volt-native STM32 USB buck benchmark artifacts."""

from __future__ import annotations

from pathlib import Path

import volt

from .board import (
    BOARD_NAME,
    JLCPCB_PROFILE_PATH,
    JLCPCB_PROFILE_PROJECT_PATH,
    build_pcb,
)
from .project_tests import register_project_tests
from .schematic_connectors import _author_connectors_region
from .schematic_mcu import _author_mcu_region
from .schematic_output import SHEET_FILE, SHEET_OPTIONS
from .schematic_power import _author_power_region
from .stm32_board import Stm32UsbBuckBoard, build_board


ARTIFACTS_DIR = Path(__file__).resolve().parent / "artifacts"
JLCPCB_MANUFACTURING_DIRNAME = "stm32_usb_buck_jlcpcb_manufacturing"


def build_project() -> volt.Project:
    project = volt.Project("stm32-usb-buck", description="STM32 USB buck reference design")
    project.expect_diagnostic(code="SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED", severity="warning")
    project.expect_diagnostic(code="SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW", severity="warning")
    project.expect_diagnostic(code="SCHEMATIC_SYMBOL_FIELD_FAR_FROM_SYMBOL", severity="warning")
    project.expect_diagnostic(code="SCHEMATIC_LABEL_CROWDS_SYMBOL", severity="warning")
    project.expect_diagnostic(code="SCHEMATIC_DENSE_PORT_TAGS", severity="warning")
    project.expect_diagnostic(code="PCB_VISUAL_REFERENCE_DESIGNATOR_HIDDEN", severity="warning")

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
            h=126,
            style={"border": "dashed"},
        )
        mcu_region = sheet.region(
            "STM32 Microcontroller",
            x=18,
            y=150,
            w=346,
            h=256,
            title="STM32 MCU",
            style={"border": "dashed"},
        )
        connectors_region = sheet.region(
            "Connectors and USB",
            x=350,
            y=160,
            w=228,
            h=196,
            style={"border": "dashed"},
        )

        _author_power_region(power_region, board, nets)
        _author_mcu_region(mcu_region, board, nets)
        _author_connectors_region(connectors_region, board, nets)
        return sheet

    @project.board
    def board(context: volt.BuildContext) -> volt.Board:
        return build_pcb(context)

    register_project_tests(project)
    return project


def run_project() -> volt.ProjectResult:
    return build_project().run()


def _raise_if_not_ok(result: volt.ProjectResult) -> None:
    if result.ok:
        return
    diagnostics = [
        f"{diagnostic.report}:{diagnostic.code}"
        for diagnostic in result.unexpected_diagnostics
    ]
    diagnostics.extend(
        f"missing:{expectation.code}"
        for expectation in result.missing_expected_diagnostics
    )
    raise RuntimeError("STM32 USB buck validation failed: " + ", ".join(diagnostics))


def jlcpcb_manufacturing_profile_metadata() -> dict[str, str]:
    return {
        "path": JLCPCB_PROFILE_PROJECT_PATH,
        "resolved_path": str(JLCPCB_PROFILE_PATH),
    }


def write_artifacts(output_dir: Path | str | None = None) -> volt.ProjectArtifactPaths:
    if output_dir is None:
        output_dir = ARTIFACTS_DIR
    output_path = Path(output_dir)

    result = run_project()
    _raise_if_not_ok(result)
    output_path.mkdir(parents=True, exist_ok=True)
    project_bundle = output_path / "stm32_usb_buck.volt"
    result.write(project_bundle)
    artifacts = result.write_artifacts(
        output_path,
        slug="stm32_usb_buck",
        pcb_svg_options={
            "diagnostic_overlays": False,
            "pad_net_overlays": False,
            "separate_layers": True,
        },
    )
    return artifacts


def write_jlcpcb_manufacturing_package(
    output_dir: Path | str | None = None,
) -> volt.ManufacturingPackageResult:
    if output_dir is None:
        output_dir = ARTIFACTS_DIR / JLCPCB_MANUFACTURING_DIRNAME
    output_path = Path(output_dir)

    result = run_project()
    _raise_if_not_ok(result)
    return result.write_manufacturing_package(
        output_path,
        board=BOARD_NAME,
        manufacturing_profile=jlcpcb_manufacturing_profile_metadata(),
        archive=True,
    )


if __name__ == "__main__":
    write_artifacts()
    write_jlcpcb_manufacturing_package()
