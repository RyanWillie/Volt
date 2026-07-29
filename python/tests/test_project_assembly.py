import json

import volt
from project_framework_helpers import _delivery_profile, _passive_0603


def _resistor_parts(*specs):
    library = volt.Library("volt.tests.assembly", version="1.0.0")
    return tuple(
        library.part(
            name,
            pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
            value=value,
            manufacturer="Yageo",
            mpn=mpn,
            package="0603",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            pads={1: "1", 2: "2"},
            prefix="R",
        )
        for name, value, mpn in specs
    )


def _assembly_ready_design(*, include_extra=False):
    specs = [
        ("R-330", "330", "RC0603FR-07330RL"),
        ("R-1K", "1k", "RC0603FR-071KL"),
    ]
    if include_extra:
        specs.append(("R-10K", "10k", "RC0603FR-0710KL"))
    parts = _resistor_parts(*specs)
    design = volt.Design("assembly-demo")
    design.instantiate(parts[0], ref="R1").dnp(False)
    design.instantiate(parts[1], ref="R2").dnp(False)
    if include_extra:
        design.instantiate(parts[2], ref="R3").dnp(False)
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
    (unplaced_part,) = _resistor_parts(("R-1K", "1k", "RC0603FR-071KL"))
    unplaced = design.instantiate(unplaced_part, ref="R2").dnp(False)
    design.R("10k", ref="R3").dnp(False)
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


def test_project_bundle_default_graph_does_not_implicitly_export_assembly_files(tmp_path):
    project = volt.Project("assembly-demo")

    @project.design
    def design():
        return _assembly_ready_design(include_extra=True)

    @project.board
    def board(context):
        pcb = context.design().add_board("Main")
        pcb.set_capability_profile(_delivery_profile())
        pcb.place(context.design().component("R2"), at=(20.5, 11), rotation=270, side="bottom")
        pcb.place(context.design().component("R1"), at=(10, 5.25), rotation=90)
        return pcb

    bundle = tmp_path / "bundle"
    project.run().write(bundle)

    manifest = json.loads((bundle / "manifest.volt.json").read_text(encoding="utf-8"))

    assert manifest["export_selection"] == []
    assert len(manifest["dependency_lock"]["selected_parts"]) == 3
    assert volt.ProjectBundle.open(bundle).graph.dependency_lock == manifest["dependency_lock"]
    kinds = {artifact["kind"] for artifact in manifest["artifacts"]}
    assert {"bom", "cpl", "kicad_pcb"}.isdisjoint(kinds)
    assert {"board_model", "compiled_board", "board_scene"} <= kinds
