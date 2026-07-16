import gc

import pytest

import volt
from volt.diagnostics import _diagnostic_from_dict


SCHEMATIC_FORWARDING_METHODS = (
    "schematic_sheet",
    "schematic_region",
    "register_schematic_symbol",
    "place_schematic_symbol",
    "schematic_symbol_orientation",
    "schematic_symbol_pin_anchor",
    "schematic_symbol_pin_refs",
    "add_schematic_wire",
    "add_schematic_wire_for_endpoints",
    "add_schematic_net_label",
    "add_schematic_net_label_for_endpoint",
    "add_schematic_junction",
    "add_schematic_junction_for_endpoint",
    "add_schematic_terminal_marker",
    "add_schematic_terminal_marker_for_endpoint",
    "add_schematic_no_connect_marker",
    "add_schematic_sheet_port",
    "add_schematic_sheet_port_for_endpoint",
    "add_schematic_symbol_field",
    "schematic_to_json",
    "schematic_to_svg",
    "schematic_to_body_svg",
    "schematic_svg_pages",
    "load_schematic_json",
    "schematic_sheet_names",
    "validate_schematic",
    "validate_schematic_readability",
)


def test_native_schematic_document_is_a_separate_owner_without_circuit_forwarding():
    circuit = volt._volt.Circuit()

    assert isinstance(volt._volt.SchematicDocument(circuit), volt._volt.SchematicDocument)
    assert all(not hasattr(circuit, name) for name in SCHEMATIC_FORWARDING_METHODS)


def test_native_schematic_document_retains_circuit_for_either_destruction_order():
    circuit = volt._volt.Circuit()
    schematic = volt._volt.SchematicDocument(circuit)

    del circuit
    gc.collect()
    assert schematic.schematic_sheet("Main", {}) == 0
    assert schematic.schematic_sheet_names() == ["Main"]

    circuit = volt._volt.Circuit()
    schematic = volt._volt.SchematicDocument(circuit)
    del schematic
    gc.collect()
    assert circuit.add_net("SIG") == 0


def test_foreign_schematic_ids_reject_typed_and_leave_both_owners_byte_identical():
    local = volt.Design("local-schematic-owner")
    local_component = local.R(ref="R1")
    local_net = local.net("SIG")
    schematic = local.schematic("Main")

    foreign = volt.Design("foreign-schematic-owner")
    foreign_component = foreign.R(ref="R1")
    foreign_net = foreign.net("SIG")
    assert foreign_component.index == local_component.index
    assert foreign_net.index == local_net.index

    before_schematic = schematic.to_json().encode()
    before_local = local.to_json().encode()
    before_foreign = foreign.to_json().encode()

    with pytest.raises(
        volt.CrossReferenceError,
        match="belongs to a different design",
    ) as component_error:
        schematic.place(foreign_component, at=(40, 20), symbol="resistor")
    assert component_error.value.code == "CrossReferenceViolation"
    assert component_error.value.entity is None

    with pytest.raises(
        volt.CrossReferenceError,
        match="belongs to a different design",
    ) as net_error:
        schematic.wire(foreign_net, [(20, 20), (40, 20)])
    assert net_error.value.code == "CrossReferenceViolation"
    assert net_error.value.entity is None

    assert schematic.to_json().encode() == before_schematic
    assert local.to_json().encode() == before_local
    assert foreign.to_json().encode() == before_foreign


def test_direct_owner_preserves_public_serialized_bytes_and_diagnostics():
    circuit = volt._volt.Circuit()
    definition = circuit.define_resistor()
    component = circuit.instantiate_ref(definition, "R1", {})
    net = circuit.add_net("SIG")
    direct = volt._volt.SchematicDocument(circuit)
    sheet = direct.schematic_sheet("Main", {})
    direct.place_schematic_symbol(sheet, component, "resistor", 40, 20, "Right")
    direct.add_schematic_wire(sheet, net, [(20, 20), (40, 20)], "Direct")

    design = volt.Design("public-schematic-owner")
    public_component = design.R(ref="R1")
    public_net = design.net("SIG")
    public = design.schematic("Main")
    public.place(public_component, at=(40, 20), symbol="resistor")
    public.wire(public_net, [(20, 20), (40, 20)])

    assert direct.schematic_to_json().encode() == public.to_json().encode()
    assert [
        _diagnostic_from_dict(diagnostic) for diagnostic in direct.validate_schematic()
    ] == list(public.validate())
    assert [
        _diagnostic_from_dict(diagnostic)
        for diagnostic in direct.validate_schematic_readability()
    ] == list(public.validate_readability())
