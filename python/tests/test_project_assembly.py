import json

import volt
from project_framework_helpers import _passive_0603


def _select_resistor(component, *, part_number="RC0603FR-07330RL"):
    component.select_part(
        manufacturer="Yageo",
        part_number=part_number,
        package="0603",
        footprint=_passive_0603(("passives", "R_0603_1608Metric")),
        pin_pads={1: "1", 2: "2"},
    )
    return component


def _assembly_ready_design():
    design = volt.Design("assembly-demo")
    r1 = design.R("330", ref="R1").dnp(False)
    r2 = design.R("1k", ref="R2").dnp(False)
    _select_resistor(r1)
    _select_resistor(r2, part_number="RC0603FR-071KL")
    return design


def test_board_cpl_projection_exposes_kernel_json_and_csv():
    design = _assembly_ready_design()
    board = design.add_board("Main")
    board.place(design.component("R2"), at=(20.5, 11), rotation=270, side="bottom")
    board.place(design.component("R1"), at=(10, 5.25), rotation=90)
    offsets = {("passives", "R_0603_1608Metric"): 10}

    cpl = board.cpl(rotation_offsets=offsets)

    assert json.loads(board.cpl_json(rotation_offsets=offsets)) == cpl
    assert cpl["format"] == "volt.cpl"
    assert cpl["metadata"]["origin"]["convention"] == "board_origin"
    assert cpl["rows"][0]["designator"] == "R1"
    assert cpl["rows"][0]["position_mm"] == [10, 5.25]
    assert cpl["rows"][0]["rotation_deg"] == 100
    assert cpl["rows"][0]["part"] == {
        "manufacturer": "Yageo",
        "mpn": "RC0603FR-07330RL",
        "package": "0603",
    }
    assert board.cpl_csv(rotation_offsets=offsets) == (
        "Designator,Mid X,Mid Y,Layer,Rotation\n"
        "R1,10,5.25,Top,100\n"
        "R2,20.5,11,Bottom,280\n"
    )


def test_board_assembly_diagnostics_are_explicit():
    design = volt.Design("assembly-gaps")
    placed_without_part = design.R("330", ref="R1").dnp(False)
    unplaced = design.R("1k", ref="R2").dnp(False)
    design.R("10k", ref="R3").dnp(False)
    _select_resistor(unplaced)
    board = design.add_board("Main")
    board.place(placed_without_part, at=(1, 2))

    report = board.validate_assembly()

    assert [diagnostic.code for diagnostic in report] == [
        "ASSEMBLY_COMPONENT_MISSING_SELECTED_PART",
        "ASSEMBLY_PART_IDENTITY_MISSING",
        "ASSEMBLY_COMPONENT_UNPLACED",
        "ASSEMBLY_COMPONENT_MISSING_SELECTED_PART",
        "ASSEMBLY_PART_IDENTITY_MISSING",
        "ASSEMBLY_COMPONENT_UNPLACED",
    ]
    assert all(diagnostic.category == "assembly" for diagnostic in report)


def test_project_bundle_emits_cpl_with_bom_and_board_exports(tmp_path):
    project = volt.Project("assembly-demo")

    @project.design
    def design():
        return _assembly_ready_design()

    @project.board
    def board(context):
        pcb = context.design().add_board("Main")
        pcb.place(context.design().component("R2"), at=(20.5, 11), rotation=270, side="bottom")
        pcb.place(context.design().component("R1"), at=(10, 5.25), rotation=90)
        return pcb

    bundle = tmp_path / "bundle"
    project.run().write(bundle)

    cpl_json = json.loads((bundle / "pcb" / "Main.cpl.json").read_text(encoding="utf-8"))
    cpl_csv = (bundle / "pcb" / "Main.cpl.csv").read_text(encoding="utf-8")
    manifest = json.loads((bundle / "manifest.volt.json").read_text(encoding="utf-8"))

    assert cpl_json["rows"][0]["designator"] == "R1"
    assert cpl_csv.startswith("Designator,Mid X,Mid Y,Layer,Rotation\n")
    assert (bundle / "bom" / "bom.json").exists()
    assert (bundle / "pcb" / "Main.kicad_pcb").exists()
    assert {
        (artifact["kind"], artifact["path"])
        for artifact in manifest["artifacts"]
        if artifact["kind"] in {"bom", "bom_csv", "cpl", "cpl_csv", "kicad_pcb"}
    } == {
        ("bom", "bom/bom.json"),
        ("bom_csv", "bom/bom.csv"),
        ("cpl", "pcb/Main.cpl.json"),
        ("cpl_csv", "pcb/Main.cpl.csv"),
        ("kicad_pcb", "pcb/Main.kicad_pcb"),
    }
