import gc
import hashlib
import json

import pytest

import volt

from project_framework_helpers import _board_ready_design, _stage_schematic


BOARD_FORWARDING_METHODS = (
    "board",
    "board_design_rules",
    "board_set_design_rules",
    "board_set_capability_profile",
    "board_add_layer",
    "board_set_layer_stack",
    "board_set_rectangular_outline",
    "board_set_polygon_outline",
    "board_outline_vertices",
    "board_add_hole",
    "board_add_slot",
    "board_add_cutout",
    "board_add_circle",
    "board_cache_footprint_definition",
    "board_place_component",
    "board_placement_refs",
    "board_stackup",
    "board_component_footprint_pads",
    "board_add_track",
    "board_add_track_for_route",
    "board_track_net",
    "board_add_via",
    "board_assisted_connect",
    "board_escape",
    "board_add_zone",
    "board_add_keepout",
    "board_add_room",
    "board_add_text",
    "board_resolve_pads",
    "board_validate",
    "board_validate_assembly",
    "board_cpl_json",
    "board_cpl_csv",
    "board_to_json",
    "board_to_svg",
    "board_to_kicad_pcb",
    "board_to_fabrication_files",
)


def _digest(board) -> str:
    return hashlib.sha256(board.to_json().encode()).hexdigest()


def test_native_board_registry_has_strict_typed_names_lookup_and_ordering():
    circuit = volt._volt.Circuit()
    registry = volt._volt.BoardRegistry(circuit)

    with pytest.raises(volt.InvalidStateError, match="has no Boards") as empty_selection:
        registry.board()
    assert empty_selection.value.code == "InvalidState"

    with pytest.raises(volt.InvalidArgumentError, match="must not be empty") as empty_name:
        registry.add("")
    assert empty_name.value.code == "InvalidArgument"
    with pytest.raises(volt.InvalidArgumentError, match="must not be empty") as empty_lookup:
        registry.board("")
    assert empty_lookup.value.code == "InvalidArgument"

    compact = registry.add("Compact")
    assert isinstance(compact, volt._volt.Board)
    assert registry.board() is compact
    assert registry.board("Compact") is compact

    with pytest.raises(volt.DuplicateNameError, match="already exists") as duplicate:
        registry.add("Compact")
    assert duplicate.value.code == "DuplicateName"

    with pytest.raises(volt.UnknownEntityError, match="no Board named Missing") as missing:
        registry.board("Missing")
    assert missing.value.code == "UnknownEntity"

    for name in ("Production", "z", "é", "Ω", "A"):
        registry.add(name)
    assert registry.names() == ("A", "Compact", "Production", "z", "é", "Ω")

    with pytest.raises(volt.InvalidStateError, match="has 6 Boards") as ambiguous:
        registry.board()
    assert ambiguous.value.code == "InvalidState"
    assert all(not hasattr(circuit, method) for method in BOARD_FORWARDING_METHODS)


def test_direct_native_board_retains_registry_and_circuit_for_either_destruction_order():
    circuit = volt._volt.Circuit()
    registry = volt._volt.BoardRegistry(circuit)
    board = registry.add("Compact")

    del circuit
    del registry
    gc.collect()
    assert board.add_layer("F.Cu", "copper", "top", 0.035, True, 1.0) == 0

    circuit = volt._volt.Circuit()
    registry = volt._volt.BoardRegistry(circuit)
    board = registry.add("Production")
    del board
    gc.collect()
    assert registry.names() == ("Production",)
    assert circuit.add_net("SIG") == 0


def test_python_board_retains_its_design_owner():
    design = volt.Design("retained-design")
    board = design.add_board("Main")

    del design
    gc.collect()
    assert board.add_layer("F.Cu", role="copper", side="top") == 0
    assert json.loads(board.to_json())["board"]["name"] == "Main"


def test_compact_and_production_are_independent_over_one_unchanged_logical_design():
    design = _board_ready_design("named-board-fixture")
    schematic = _stage_schematic(design)
    logical_before = design.to_json().encode()
    schematic_before = schematic.to_json().encode()
    bom_before = json.dumps(design.bom(), sort_keys=True).encode()

    production = design.add_board("Production")
    compact = design.add_board("Compact")
    assert design.boards() == (compact, production)
    assert design.board("Compact") is compact
    vcc = next(net for net in design.nets() if net.name == "VCC")

    compact_layer = compact.add_layer("F.Cu", role="copper", side="top")
    compact.set_rectangular_outline(origin=(0, 0), size=(20, 10))
    compact.place(design.component("R1"), at=(5, 4))
    compact.add_track(vcc, layer=compact_layer, points=((1, 1), (5, 4)), width=0.2)

    production_layer = production.add_layer("F.Cu", role="copper", side="top")
    production.set_rectangular_outline(origin=(0, 0), size=(80, 50))
    production.place(design.component("R1"), at=(40, 25), rotation=90)
    production.add_track(
        vcc,
        layer=production_layer,
        points=((2, 2), (40, 25)),
        width=0.5,
    )

    compact_document = json.loads(compact.to_json())
    production_document = json.loads(production.to_json())
    assert compact_document["board"]["name"] == "Compact"
    assert production_document["board"]["name"] == "Production"
    assert compact_document["board"]["outline"] != production_document["board"]["outline"]
    assert compact_document["board"]["placements"] != production_document["board"]["placements"]
    assert compact_document["board"]["tracks"] != production_document["board"]["tracks"]
    assert _digest(compact) != _digest(production)

    assert design.to_json().encode() == logical_before
    assert schematic.to_json().encode() == schematic_before
    assert json.dumps(design.bom(), sort_keys=True).encode() == bom_before


def test_named_board_project_persistence_is_ordered_and_composite_selectable(tmp_path):
    project = volt.Project("named-board-project")

    @project.design
    def design():
        return _board_ready_design("product")

    @project.board
    def boards(context):
        product = context.design()
        production = product.add_board("Production")
        production.set_rectangular_outline(origin=(0, 0), size=(80, 50))
        compact = product.add_board("Compact")
        compact.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        return tuple(reversed(product.boards()))

    result = project.run()
    assert [board.name for board in result.boards] == ["Compact", "Production"]
    assert result.board("product:Compact").name == "Compact"
    assert result.board("product:Production").name == "Production"
    with pytest.raises(LookupError, match="has 2 board models"):
        result.board()

    result.write(tmp_path / "result.volt")
    manifest = json.loads(
        (tmp_path / "result.volt" / "manifest.volt.json").read_text(encoding="utf-8")
    )
    board_groups = [
        artifact["group"]
        for artifact in manifest["artifacts"]
        if artifact["kind"] == "pcb"
    ]
    assert board_groups == [
        {"design": "product", "board": "Compact"},
        {"design": "product", "board": "Production"},
    ]
