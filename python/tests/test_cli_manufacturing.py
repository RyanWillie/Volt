import json
import sys
import zipfile
from pathlib import Path

from volt.cli import main


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


def _read_stdout_json(capsys):
    captured = capsys.readouterr()
    assert captured.err == ""
    return json.loads(captured.out)


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


def _write_manufacturing_entrypoint(root: Path, *, lossy: bool = False) -> None:
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
    _write_entrypoint(
        root,
        f"""from pathlib import Path

import volt


def _rect_0603(ref):
    return volt.FootprintDefinition(
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
    return volt.FootprintDefinition(
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
    profile = volt.CapabilityProfile.from_file(Path("profiles/generic.volt.json"))
    project = volt.Project("status-led")

    @project.design
    def design():
        design = volt.Design("status-led")
        vcc = design.net("VCC", kind="power")
        led_a = design.net("LED_A")
        gnd = design.net("GND", kind="ground")
        j1 = design.connector_1x02(ref="J1")
        r1 = design.R("330", ref="R1")
        d1 = design.LED(ref="D1")
        vcc += j1[1], r1[1]
        led_a += r1[2], d1["A"]
        gnd += d1["K"], j1[2]
        j1.select_part(
            manufacturer="Generic",
            part_number="HDR-1x02",
            package="2.54mm-1x02",
            footprint=volt.FootprintDefinition(
                ("test", "Header1x02"),
                pads=_header_1x02().pads,
            ),
            pin_pads={{1: "1", 2: "2"}},
        )
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=_rect_0603(("test", "RectR0603")),
            pin_pads={{1: "1", 2: "2"}},
        )
        d1.select_part(
            manufacturer="Lite-On",
            part_number="LTST-C190KRKT",
            package="0603",
            footprint=_rect_0603(("test", "RectD0603")),
            pin_pads={{"A": "1", "K": "2"}},
        )
        for component in (j1, r1, d1):
            component.dnp(False)
        return design

    @project.board
    def board(context):
        [design] = context.designs
        board = design.board("Control")
        board.set_capability_profile(profile)
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


def _write_manufacturing_project(root: Path, *, lossy: bool = False) -> None:
    _write_project(
        root,
        extra_config="""
[manufacturing]
profile = "profiles/generic.volt.json"
""",
    )
    _write_manufacturing_profile(root)
    _write_manufacturing_entrypoint(root, lossy=lossy)


def test_export_manufacturing_writes_deterministic_native_package(tmp_path, capsys):
    root = tmp_path / "board"
    output = tmp_path / "manufacturing-package"
    _write_manufacturing_project(root)

    assert (
        main(
            [
                "export",
                "manufacturing",
                "--json",
                "--archive",
                "--output",
                str(output),
                "--project",
                str(root),
            ]
        )
        == 0
    )

    payload = _read_stdout_json(capsys)
    archive = output.with_suffix(".zip")
    assert payload["status"] == "clean"
    assert payload["written"] is True
    assert payload["output"] == str(output)
    assert payload["archive"] == str(archive)
    assert payload["board"] == {
        "design": "status-led",
        "name": "Control",
        "output_name": "Control",
    }

    manifest_path = output / "manufacturing" / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert manifest["format"] == "volt.manufacturing_package"
    assert manifest["schema_version"] == 1
    assert manifest["project"] == {
        "name": "status-led",
        "version": None,
        "description": None,
    }
    assert manifest["command"] == {
        "name": "volt export manufacturing",
        "board": None,
        "archive": True,
    }
    assert manifest["profile"]["config"] == {
        "path": "profiles/generic.volt.json",
        "resolved_path": str(root / "profiles" / "generic.volt.json"),
    }
    assert manifest["profile"]["board"]["name"] == "Generic 2-layer manufacturing test profile"
    assert manifest["diagnostics"]["summary"] == {"errors": 0, "infos": 0, "warnings": 0}
    assert manifest["native_fabrication"]["coverage"] == {
        "classification": "complete",
        "fab_critical_loss": False,
    }
    assert manifest["native_fabrication"]["warnings"] == []
    assert [item["filename"] for item in manifest["native_fabrication"]["files"]] == [
        "Control.GTL",
        "Control.GBL",
        "Control.GTS",
        "Control.GBS",
        "Control.GTO",
        "Control.GTP",
        "Control.GKO",
        "Control-PTH.TXT",
        "Control-NPTH.TXT",
    ]

    artifact_paths = {item["kind"]: item["path"] for item in manifest["artifacts"]}
    for kind in ("project_manifest", "bom", "bom_csv", "cpl", "cpl_csv", "diagnostics"):
        assert output.joinpath(artifact_paths[kind]).is_file()

    native_paths = {
        item["filename"]: output.joinpath(item["path"])
        for item in manifest["native_fabrication"]["files"]
    }
    assert "%TF.FileFunction,Copper,L1,Top*%" in native_paths["Control.GTL"].read_text(
        encoding="utf-8"
    )
    assert "%TF.FileFunction,Soldermask,Top*%" in native_paths["Control.GTS"].read_text(
        encoding="utf-8"
    )
    assert "%TF.FileFunction,Paste,Top*%" in native_paths["Control.GTP"].read_text(
        encoding="utf-8"
    )
    assert "%TF.FileFunction,Legend,Top*%" in native_paths["Control.GTO"].read_text(
        encoding="utf-8"
    )
    assert "%TF.FileFunction,Profile,NP*%" in native_paths["Control.GKO"].read_text(
        encoding="utf-8"
    )
    assert ";TYPE=PLATED" in native_paths["Control-PTH.TXT"].read_text(encoding="utf-8")
    assert ";TYPE=NON_PLATED" in native_paths["Control-NPTH.TXT"].read_text(encoding="utf-8")

    inspection = output / "manufacturing" / "inspection.html"
    inspection_text = inspection.read_text(encoding="utf-8")
    assert "Native fabrication inspection" in inspection_text
    assert "Control.GTL" in inspection_text
    assert "Control-PTH.TXT" in inspection_text

    with zipfile.ZipFile(archive) as package:
        assert "manufacturing/manifest.json" in package.namelist()
        assert "manufacturing/fabrication/gerber/Control.GTL" in package.namelist()
        assert "manufacturing/fabrication/drill/Control-PTH.TXT" in package.namelist()

    first = _directory_bytes(output)
    first_archive = archive.read_bytes()
    output.joinpath("stale-order-file.txt").write_text("stale", encoding="utf-8")
    output.joinpath("manufacturing", "old-gerber.gbr").write_text("stale", encoding="utf-8")

    assert (
        main(
            [
                "export",
                "manufacturing",
                "--json",
                "--archive",
                "--output",
                str(output),
                "--project",
                str(root),
            ]
        )
        == 0
    )
    _read_stdout_json(capsys)
    assert _directory_bytes(output) == first
    assert archive.read_bytes() == first_archive
    with zipfile.ZipFile(archive) as package:
        assert "stale-order-file.txt" not in package.namelist()
        assert "manufacturing/old-gerber.gbr" not in package.namelist()


def test_export_manufacturing_refuses_fab_critical_native_loss_without_writing(
    tmp_path, capsys
):
    root = tmp_path / "board"
    output = tmp_path / "manufacturing-package"
    _write_manufacturing_project(root, lossy=True)

    assert (
        main(
            [
                "export",
                "manufacturing",
                "--json",
                "--output",
                str(output),
                "--project",
                str(root),
            ]
        )
        == 1
    )

    payload = _read_stdout_json(capsys)
    assert payload["status"] == "native-fabrication-loss"
    assert payload["written"] is False
    assert payload["native_fabrication"]["coverage"] == {
        "classification": "fab-critical-loss",
        "fab_critical_loss": True,
    }
    assert [warning["construct"] for warning in payload["native_fabrication"]["warnings"]] == [
        "board.feature.hole.finished_diameter"
    ]
    assert not output.exists()


def test_export_manufacturing_reports_board_selector_errors_without_writing(
    tmp_path, capsys
):
    root = tmp_path / "board"
    output = tmp_path / "manufacturing-package"
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
            board = design.board("Main")
            board.set_rectangular_outline(origin=(0, 0), size=(20, 10))
            boards.append(board)
        return tuple(boards)

    return project.run()
""",
    )

    assert (
        main(
            [
                "export",
                "manufacturing",
                "--output",
                str(output),
                "--project",
                str(root),
            ]
        )
        == 2
    )
    ambiguous = capsys.readouterr()
    assert ambiguous.out == ""
    assert "Project result has multiple boards; pass --board" in ambiguous.err
    assert not output.exists()

    assert (
        main(
            [
                "export",
                "manufacturing",
                "--output",
                str(output),
                "--board",
                "missing",
                "--project",
                str(root),
            ]
        )
        == 2
    )
    missing = capsys.readouterr()
    assert missing.out == ""
    assert "No board named 'missing'" in missing.err
    assert not output.exists()
