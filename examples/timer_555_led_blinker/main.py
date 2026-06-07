"""Generate the Volt-native 555 LED blinker example artifacts."""

from __future__ import annotations

from pathlib import Path

import volt

from .board import build_board
from .components import build_design
from .project_tests import register_project_tests
from .schematic import build_schematic

EXAMPLE_SLUG = "timer_555_led_blinker"


def _require_clean(result: volt.ProjectResult) -> None:
    diagnostics = [
        f"{diagnostic.source}:{diagnostic.code}"
        for diagnostic in result.diagnostics
    ]
    failures = [
        f"{failure.stage}:{failure.name}"
        for failure in result.test_failures()
    ]
    if diagnostics or failures:
        details = ", ".join((*diagnostics, *failures))
        raise RuntimeError("555 LED blinker validation failed: " + details)


def build_project() -> volt.Project:
    project = volt.Project(
        "timer-555-led-blinker",
        description="555 LED blinker reference design",
    )

    @project.design
    def design():
        project_design, nets, parts = build_design()
        return (
            project_design,
            volt.ProjectResource("nets", nets),
            volt.ProjectResource("parts", parts),
        )

    project.schematic(build_schematic)
    project.board(build_board)
    register_project_tests(project)
    return project


def run_project() -> volt.ProjectResult:
    return build_project().run()


def main() -> None:
    output_path = Path(__file__).resolve().parent / "artifacts"
    output_path.mkdir(parents=True, exist_ok=True)

    result = run_project()
    _require_clean(result)

    result.write(output_path / f"{EXAMPLE_SLUG}.volt")
    result.write_artifacts(
        output_path,
        slug=EXAMPLE_SLUG,
        pcb_svg_options={"pad_net_overlays": False, "ratsnest_edges": False},
    )


if __name__ == "__main__":
    main()
