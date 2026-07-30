import importlib
import json
import os
import sys
from pathlib import Path

import pytest
import volt


def _write_project(
    root: Path,
    entrypoint: str = "project_entry:main",
    extra_config: str = "",
) -> None:
    root.mkdir(parents=True, exist_ok=True)
    root.joinpath("volt.toml").write_text(
        f"""[project]
entrypoint = "{entrypoint}"

[paths]
{extra_config}
""",
        encoding="utf-8",
    )


def _write_entrypoint(root: Path, body: str) -> None:
    root.joinpath("project_entry.py").write_text(body, encoding="utf-8")
    sys.modules.pop("project_entry", None)


def _run_project_direct(root: Path) -> volt.ProjectResult:
    previous_cwd = Path.cwd()
    sys.path.insert(0, str(root))
    sys.modules.pop("project_entry", None)
    os.chdir(root)
    try:
        module = importlib.import_module("project_entry")
        result = module.main()
    finally:
        os.chdir(previous_cwd)
        sys.modules.pop("project_entry", None)
        sys.path.remove(str(root))
    assert isinstance(result, volt.ProjectResult)
    return result


def _manufacturing_profile_metadata(root: Path) -> dict[str, str]:
    path = root / "profiles" / "generic.volt.json"
    return {
        "path": "profiles/generic.volt.json",
        "resolved_path": str(path),
    }


def _write_manufacturing_profile(root: Path) -> None:
    profile = {
        "format": "volt.capability_profile",
        "version": 1,
        "profile": {
            "name": "Generic 2-layer manufacturing test profile",
            "provenance": {
                "source": "Volt CLI manufacturing export test fixture",
                "as_of": "2026-06-21",
            },
            "minimum_track_width_mm": 0.2,
            "minimum_via_drill_mm": 0.3,
            "minimum_via_annular_mm": 0.6,
            "supported_copper_layer_counts": [2],
            "board_thickness_range_mm": {"minimum_mm": 0.8, "maximum_mm": 2.0},
            "available_copper_weights_oz": [1.0],
            "drill_diameter_range_mm": {"minimum_mm": 0.3, "maximum_mm": 6.0},
            "minimum_clearances": [
                {"first": "track", "second": "track", "clearance_mm": 0.2},
                {"first": "track", "second": "pad", "clearance_mm": 0.2},
            ],
        },
    }
    profile_path = root / "profiles" / "generic.volt.json"
    profile_path.parent.mkdir(parents=True, exist_ok=True)
    profile_path.write_text(
        json.dumps(profile, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _write_manufacturing_entrypoint(
    root: Path,
    *,
    lossy: bool = False,
    board_profile: bool = True,
) -> None:
    lossy_feature = (
        """
        board.add(
            volt.Hole(
                center=(8.0, 24.0),
                diameter=2.4,
                role="mounting",
                label="FH1",
                finished_diameter=2.0,
            )
        )
"""
        if lossy
        else ""
    )
    profile_setup = (
        '    profile = volt.CapabilityProfile.from_file(Path("profiles/generic.volt.json"))\n'
        if board_profile
        else ""
    )
    board_profile_call = "        board.set_capability_profile(profile)\n" if board_profile else ""
    _write_entrypoint(
        root,
        f"""from pathlib import Path

import volt


def _rect_0603(ref):
    return volt.Footprint(
        ref,
        pads=(
            volt.FootprintPad.surface_mount(
                "1",
                at=(-0.75, 0.0),
                size=(0.8, 0.95),
                shape="rectangle",
            ),
            volt.FootprintPad.surface_mount(
                "2",
                at=(0.75, 0.0),
                size=(0.8, 0.95),
                shape="rectangle",
            ),
        ),
    )


def _header_1x02():
    return volt.Footprint(
        ("connectors", "PinHeader_1x02_P2.54mm"),
        pads=(
            volt.FootprintPad.through_hole(
                "1",
                at=(0.0, -1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
            ),
            volt.FootprintPad.through_hole(
                "2",
                at=(0.0, 1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
            ),
        ),
    )


def _net(design, name):
    return next(net for net in design.nets() if net.name == name)


def main():
{profile_setup}
    project = volt.Project("status-led")

    @project.design
    def design():
        design = volt.Design("status-led")
        library = volt.Library("volt.tests.cli.manufacturing", version="1.0.0")
        header = library.part(
            "Header-1x02",
            pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
            manufacturer="Generic",
            mpn="HDR-1x02",
            package="2.54mm-1x02",
            footprint=volt.Footprint(
                ("test", "Header1x02"),
                pads=_header_1x02().pads,
            ),
            pads={{1: "1", 2: "2"}},
            prefix="J",
        )
        resistor = library.part(
            "Resistor-330R",
            pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
            manufacturer="Yageo",
            mpn="RC0603FR-07330RL",
            package="0603",
            footprint=_rect_0603(("test", "RectR0603")),
            pads={{1: "1", 2: "2"}},
            prefix="R",
            value="330",
        )
        led = library.part(
            "Status-LED",
            pins=(volt.PinSpec("A", 1), volt.PinSpec("K", 2)),
            manufacturer="Lite-On",
            mpn="LTST-C190KRKT",
            package="0603",
            footprint=_rect_0603(("test", "RectD0603")),
            pads={{"A": "1", "K": "2"}},
            prefix="D",
        )
        vcc = design.net("VCC", kind="power")
        led_a = design.net("LED_A")
        gnd = design.net("GND", kind="ground")
        j1 = design.instantiate(header, ref="J1")
        r1 = design.instantiate(resistor, ref="R1")
        d1 = design.instantiate(led, ref="D1")
        vcc += j1[1], r1[1]
        led_a += r1[2], d1["A"]
        gnd += d1["K"], j1[2]
        for component in (j1, r1, d1):
            component.dnp(False)
        return design

    @project.board
    def board(context):
        [design] = context.designs
        board = design.add_board("Control")
{board_profile_call}
        front = board.add_layer("F.Cu", role="copper", side="top")
        back = board.add_layer("B.Cu", role="copper", side="bottom")
        silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
        board.set_layer_stack((front, back), thickness=1.6)
        board.set_design_rules(
            copper_clearance=0.25,
            min_track_width=0.25,
            min_via_drill=0.35,
            min_via_annular=0.7,
        )
        board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))
        board.add(volt.Hole(center=(3.0, 3.0), diameter=3.2, role="mounting", label="MH1"))
        board.place(design.component("J1"), at=(6.0, 15.0), locked=True)
        board.place(design.component("R1"), at=(18.0, 15.0))
        board.place(design.component("D1"), at=(28.0, 15.0), rotation=180.0)
        vcc = _net(design, "VCC")
        led_a = _net(design, "LED_A")
        gnd = _net(design, "GND")
        board.add_track(
            vcc,
            layer=front,
            points=((6.0, 13.73), (12.0, 12.0), (17.25, 15.0)),
            width=0.25,
        )
        board.add_track(
            led_a,
            layer=front,
            points=((18.75, 15.0), (23.0, 12.0), (28.75, 12.0), (28.75, 15.0)),
            width=0.25,
        )
        board.add_track(
            gnd,
            layer=back,
            points=((6.0, 16.27), (23.0, 20.0)),
            width=0.25,
        )
        board.add_track(
            gnd,
            layer=front,
            points=((23.0, 20.0), (27.25, 15.0)),
            width=0.25,
        )
        board.add_via(
            gnd,
            at=(23.0, 20.0),
            start_layer=front,
            end_layer=back,
            drill=0.35,
            annular=0.75,
        )
        board.add_text("REV A", at=(4.0, 27.0), layer=silk, size=1.0)
{lossy_feature}
        return board

    return project.run()
""",
    )


def _directory_bytes(root: Path) -> dict[str, bytes]:
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def _write_manufacturing_project(
    root: Path,
    *,
    lossy: bool = False,
    board_profile: bool = True,
    config_profile: bool = True,
) -> None:
    _write_project(
        root,
        extra_config=(
            """
[manufacturing]
profile = "profiles/generic.volt.json"
"""
            if config_profile
            else ""
        ),
    )
    _write_manufacturing_profile(root)
    _write_manufacturing_entrypoint(root, lossy=lossy, board_profile=board_profile)


def test_project_result_manufacturing_package_writes_exact_native_handoff(tmp_path):
    root = tmp_path / "board"
    direct_output = tmp_path / "direct-package"
    _write_manufacturing_project(root)

    result = _run_project_direct(root)
    written = result.write_manufacturing_package(
        direct_output,
        manufacturing_profile=_manufacturing_profile_metadata(root),
        archive=True,
    )

    assert written.status == "clean"
    assert written.output == direct_output
    assert written.archive == direct_output.with_suffix(".zip")
    assert {key: written.board[key] for key in ("design", "name", "output_name")} == {
        "design": "status-led",
        "name": "Control",
        "output_name": "Control",
    }
    assert written.board["compiled_board_provenance_digest"].startswith("sha256:")


def test_project_result_manufacturing_package_rerun_is_deterministic(tmp_path):
    root = tmp_path / "board"
    output = tmp_path / "direct-package"
    _write_manufacturing_project(root)
    result = _run_project_direct(root)

    result.write_manufacturing_package(
        output,
        manufacturing_profile=_manufacturing_profile_metadata(root),
        archive=True,
    )
    first = _directory_bytes(output)
    first_archive = output.with_suffix(".zip").read_bytes()
    output.joinpath("stale-order-file.txt").write_text("stale", encoding="utf-8")
    output.joinpath("manufacturing", "old-gerber.gbr").write_text("stale", encoding="utf-8")

    repeated = result.write_manufacturing_package(
        output,
        manufacturing_profile=_manufacturing_profile_metadata(root),
        archive=True,
    )

    assert repeated.archive == output.with_suffix(".zip")
    assert _directory_bytes(output) == first
    assert output.with_suffix(".zip").read_bytes() == first_archive


def test_project_result_manufacturing_package_archive_false_removes_stale_archive(
    tmp_path,
):
    root = tmp_path / "board"
    output = tmp_path / "direct-package"
    _write_manufacturing_project(root)
    result = _run_project_direct(root)

    result.write_manufacturing_package(
        output,
        manufacturing_profile=_manufacturing_profile_metadata(root),
        archive=True,
    )
    archive = output.with_suffix(".zip")
    assert archive.exists()

    without_archive = result.write_manufacturing_package(
        output,
        manufacturing_profile=_manufacturing_profile_metadata(root),
        archive=False,
    )

    assert without_archive.archive is None
    assert output.exists()
    assert not archive.exists()


def test_project_result_manufacturing_package_refuses_fab_critical_loss(tmp_path):
    root = tmp_path / "board"
    output = tmp_path / "direct-package"
    _write_manufacturing_project(root, lossy=True)
    result = _run_project_direct(root)

    with pytest.raises(volt.ManufacturingPackageError) as error:
        result.write_manufacturing_package(
            output,
            manufacturing_profile=_manufacturing_profile_metadata(root),
        )

    assert error.value.status == "native-fabrication-loss"
    assert error.value.native_fabrication["coverage"] == {
        "classification": "fab-critical-loss",
        "fab_critical_loss": True,
    }
    assert [warning["construct"] for warning in error.value.native_fabrication["warnings"]] == [
        "board.feature.hole.finished_diameter"
    ]
    assert not output.exists()


def test_project_result_manufacturing_package_refuses_missing_required_profile_data(
    tmp_path,
):
    missing_config_root = tmp_path / "missing-config"
    missing_config_output = tmp_path / "missing-config-package"
    _write_manufacturing_project(missing_config_root, config_profile=False)
    missing_config = _run_project_direct(missing_config_root)

    with pytest.raises(volt.ManufacturingPackageError) as config_error:
        missing_config.write_manufacturing_package(missing_config_output)

    assert config_error.value.status == "missing-manufacturing-profile"
    assert config_error.value.board == {
        "design": "status-led",
        "name": "Control",
        "output_name": "Control",
    }
    assert not missing_config_output.exists()

    partial_config_output = tmp_path / "partial-config-package"
    with pytest.raises(volt.ManufacturingPackageError) as partial_error:
        missing_config.write_manufacturing_package(
            partial_config_output,
            manufacturing_profile={"path": "profiles/generic.volt.json"},
        )

    assert partial_error.value.status == "missing-manufacturing-profile"
    assert "resolved_path" in str(partial_error.value)
    assert not partial_config_output.exists()

    missing_board_profile_root = tmp_path / "missing-board-profile"
    missing_board_profile_output = tmp_path / "missing-board-profile-package"
    _write_manufacturing_project(missing_board_profile_root, board_profile=False)
    missing_board_profile = _run_project_direct(missing_board_profile_root)

    with pytest.raises(volt.ManufacturingPackageError) as board_error:
        missing_board_profile.write_manufacturing_package(
            missing_board_profile_output,
            manufacturing_profile=_manufacturing_profile_metadata(missing_board_profile_root),
        )

    assert board_error.value.status == "missing-board-capability-profile"
    assert not missing_board_profile_output.exists()


def test_project_result_manufacturing_package_reports_selector_errors_without_writing(
    tmp_path,
):
    root = tmp_path / "board"
    output = tmp_path / "direct-package"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

def main():
    project = volt.Project("control-panel")

    @project.design
    def design():
        return (volt.Design("main-controller"), volt.Design("front-panel"))

    @project.board
    def board(context):
        boards = []
        for design in context.designs:
            board = design.add_board("Main")
            board.set_rectangular_outline(origin=(0, 0), size=(20, 10))
            boards.append(board)
        return tuple(boards)

    return project.run()
""",
    )
    result = _run_project_direct(root)

    with pytest.raises(LookupError, match="Project result has multiple boards"):
        result.write_manufacturing_package(output)
    assert not output.exists()

    with pytest.raises(LookupError, match="No board named 'missing'"):
        result.write_manufacturing_package(output, board="missing")
    assert not output.exists()
