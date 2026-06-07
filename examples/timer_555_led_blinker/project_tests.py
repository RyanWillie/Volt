"""Project checks for the 555 LED blinker example."""

from __future__ import annotations

import volt


def register_project_tests(project: volt.Project) -> None:
    @project.design.test
    def power_and_ground_are_separate(check) -> None:
        check.net("+5V").connects("J1.1", "U1.VCC", "U1.RESET")
        check.net("GND").connects("J1.2", "U1.GND", "D1.K")
        check.no_connection("+5V", "GND")

    @project.schematic.test
    def schematic_places_design_parts(check) -> None:
        check.places("J1", "U1", "R1", "R2", "C1", "C2", "C3", "R3", "D1")

    @project.board.test
    def board_places_design_parts(check) -> None:
        check.has_outline()
        check.places("J1", "U1", "R1", "R2", "C1", "C2", "C3", "R3", "D1")
