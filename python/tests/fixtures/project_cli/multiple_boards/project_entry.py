import os
from pathlib import Path

import volt


def _record_source_execution():
    sentinel = os.environ.get("VOLT_TEST_SOURCE_SENTINEL")
    if sentinel:
        with Path(sentinel).open("a", encoding="utf-8") as handle:
            handle.write("executed\n")


def _profile():
    return volt.CapabilityProfile(
        name="Verified project CLI fixture",
        source="Volt test fixture",
        as_of="2026-07-28",
        minimum_track_width=0.01,
        minimum_via_drill=0.01,
        minimum_via_annular=0.02,
    )


def _board(design, name, size):
    result = design.add_board(name)
    result.set_capability_profile(_profile())
    result.set_rectangular_outline(origin=(0, 0), size=size)
    return result


def main():
    _record_source_execution()
    project = volt.Project(
        "verified-cli-multiple",
        version="1.0.0",
        description="Multiple-board verified CLI fixture",
    )

    @project.design
    def design():
        return volt.Design("controller")

    @project.board
    def board(context):
        design = context.design()
        return (
            _board(design, "Main", (20, 10)),
            _board(design, "Compact", (16, 8)),
            _board(design, "Extended", (24, 12)),
        )

    return project
