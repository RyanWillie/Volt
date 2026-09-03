import hashlib
import json
from pathlib import Path

import pytest
import volt

from helpers import (
    _common_catalog_components,
    _definition_for_component,
    _two_pin_test_symbol,
)
from project_framework_helpers import _overvoltage_exact_part_design


def test_library_public_symbol_classes_stay_on_public_import_surface():
    import volt.library as library

    assert volt.SchematicSymbolSpec is library.SchematicSymbolSpec
    assert volt.SchematicBlockPinSpec is library.SchematicBlockPinSpec
    assert volt.SchematicSymbolSpec.__module__ == "volt.library"
    assert isinstance(
        library._default_two_terminal_symbol_spec("volt.passives:resistor"),
        library.SchematicSymbolSpec,
    )


def test_pin_spec_role_preset_rejects_contradictory_explicit_semantics():
    design = volt.Design("pin-preset-contradiction")

    try:
        design.define_component(
            "Broken",
            pins=[volt.PinSpec("VDD", 1, role="power", terminal="ground")],
        )
    except ValueError as exc:
        assert "PinSpec role preset contradicts explicit terminal kind" in str(exc)
    else:
        raise AssertionError("expected contradictory pin preset to be rejected")

    component = design.define_component(
        "Valid",
        pins=[volt.PinSpec("VDD", 1, role="power")],
    )
    design.instantiate(component, ref="U1")
    circuit = json.loads(design.to_json())
    assert "role" not in circuit["pin_definitions"][0]
    assert circuit["pin_definitions"][0]["terminal_kind"] == "Power"
    assert circuit["pin_definitions"][0]["direction"] == "Input"


def test_library_part_build_emits_kernel_owned_artifact_without_role_sugar():
    library = volt.Library("volt.test", version="1.2.3")
    library.part(
        "AP1117-15",
        pins=[
            volt.PinSpec("GND", 1, role="ground"),
            volt.PinSpec("VO", 2, role="power_output", voltage_range=(1.5, 1.5)),
            volt.PinSpec("VI", 3, role="power_input", voltage_range=(2.5, 18.0)),
        ],
        symbol=volt.SchematicSymbolSpec(
            "volt.power:regulator_3pin",
            pins=(
                volt.SchematicSymbolSpec.pin("GND", 1, (0, 0)),
                volt.SchematicSymbolSpec.pin("VO", 2, (10, -5)),
                volt.SchematicSymbolSpec.pin("VI", 3, (10, 5)),
            ),
            primitives=(),
        ),
        manufacturer="Diodes Incorporated",
        mpn="AP1117E15G-13",
        package="SOT-223-3",
        footprint=volt.Footprint(
            ("Package_TO_SOT_SMD", "SOT-223-3_TabPin2"),
            pads=(
                volt.FootprintPad.surface_mount("1", at=(-1.0, 0.0), size=(0.6, 0.6)),
                volt.FootprintPad.surface_mount("2", at=(0.0, 0.0), size=(0.6, 0.6)),
                volt.FootprintPad.surface_mount("3", at=(1.0, 0.0), size=(0.6, 0.6)),
                volt.FootprintPad.surface_mount("4", at=(0.0, 2.0), size=(1.8, 1.8)),
            ),
            courtyard=((-2.4, -1.2), (2.4, -1.2), (2.4, 3.2), (-2.4, 3.2)),
            body=((-1.9, -0.8), (1.9, -0.8), (1.9, 2.8), (-1.9, 2.8)),
            fabrication_outline=(
                (-1.7, -0.6),
                (1.7, -0.6),
                (1.7, 2.6),
                (-1.7, 2.6),
            ),
            assembly_outline=(
                (-2.0, -0.9),
                (2.0, -0.9),
                (2.0, 2.9),
                (-2.0, 2.9),
            ),
            markings=(
                volt.FootprintMarking.pin_1(
                    ((-1.9, -0.8), (-1.65, -0.8), (-1.9, -0.55))
                ),
            ),
        ),
        pads={1: "1", 2: ("2", "4"), 3: "3"},
    )

    artifact = library.build().part("AP1117-15").artifact
    assert artifact is not None
    document = json.loads(artifact.bytes)

    assert artifact.sha256 == "sha256:" + hashlib.sha256(artifact.bytes).hexdigest()
    assert volt._volt.content_hash(artifact.bytes) == artifact.sha256
    assert artifact.bytes == library.build().part("AP1117-15").artifact.bytes
    assert document["format"] == "volt.part"
    assert document["version"] == 6
    assert document["identity"] == {
        "namespace": "volt.test",
        "name": "AP1117-15",
        "version": "1.2.3",
    }
    assert b'"role"' not in artifact.bytes
    assert document["implements"].startswith("sha256:")
    assert document["content_identity"].startswith("sha256:")
    assert document["electrical_records"]["pin_count"] == 3
    assert document["electrical_records"]["records"] == []
    assert document["pin_terminal_mappings"] == [
        {"pin_key": "pin/0", "terminals": ["1"]},
        {"pin_key": "pin/1", "terminals": ["2"]},
        {"pin_key": "pin/2", "terminals": ["3"]},
    ]
    assert document["orderable_part"]["mpn"] == "AP1117E15G-13"
    assert document["orderable_part"]["terminal_pad_mappings"] == [
        {"terminal": "1", "pads": ["1"]},
        {"terminal": "2", "pads": ["2", "4"]},
        {"terminal": "3", "pads": ["3"]},
    ]
    assert document["schematic_assets"][0]["name"] == "volt.power:regulator_3pin"
    assert document["schematic_assets"][0]["hash"].startswith("sha256:")
    assert [pad["label"] for pad in document["orderable_part"]["footprint"]["pads"]] == [
        "1",
        "2",
        "3",
        "4",
    ]
    assert document["orderable_part"]["footprint"]["courtyard"] == [
        {"x_mm": -2.4, "y_mm": -1.2},
        {"x_mm": 2.4, "y_mm": -1.2},
        {"x_mm": 2.4, "y_mm": 3.2},
        {"x_mm": -2.4, "y_mm": 3.2},
    ]
    assert document["orderable_part"]["footprint"]["body"] == [
        {"x_mm": -1.9, "y_mm": -0.8},
        {"x_mm": 1.9, "y_mm": -0.8},
        {"x_mm": 1.9, "y_mm": 2.8},
        {"x_mm": -1.9, "y_mm": 2.8},
    ]
    assert document["orderable_part"]["footprint"]["fabrication_outline"] == [
        {"x_mm": -1.7, "y_mm": -0.6},
        {"x_mm": 1.7, "y_mm": -0.6},
        {"x_mm": 1.7, "y_mm": 2.6},
        {"x_mm": -1.7, "y_mm": 2.6},
    ]
    assert document["orderable_part"]["footprint"]["assembly_outline"] == [
        {"x_mm": -2.0, "y_mm": -0.9},
        {"x_mm": 2.0, "y_mm": -0.9},
        {"x_mm": 2.0, "y_mm": 2.9},
        {"x_mm": -2.0, "y_mm": 2.9},
    ]
    assert document["orderable_part"]["footprint"]["markings"] == [
        {
            "kind": "pin_1",
            "polygon": [
                {"x_mm": -1.9, "y_mm": -0.8},
                {"x_mm": -1.65, "y_mm": -0.8},
                {"x_mm": -1.9, "y_mm": -0.55},
            ],
        }
    ]


def test_library_part_build_reports_pin_role_contradictions():
    library = volt.Library("volt.test")
    library.part(
        "Broken",
        pins=[volt.PinSpec("VDD", 1, role="power", terminal="ground")],
        manufacturer="Example",
        mpn="BROKEN-1",
        package="SOT-23",
        footprint=volt.Footprint(
            ("Package_TO_SOT_SMD", "SOT-23"),
            pads=(volt.FootprintPad.surface_mount("1", at=(0.0, 0.0), size=(0.6, 0.6)),),
        ),
        pads={1: "1"},
    )

    with pytest.raises(volt.InvalidArgumentError, match="contradicts explicit terminal"):
        library.build()

def test_schematic_symbol_text_metadata_is_kernel_owned():
    symbol = volt.SchematicSymbolSpec(
        "volt.test:Styled",
        pins=(volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),),
        primitives=(
            volt.SchematicSymbolSpec.text(
                "DATA",
                (2, 3),
                align="start",
                baseline="middle",
                font_size=3.25,
            ),
        ),
    )
    design = volt.Design("symbol-text-metadata")
    component = design.define_component(
        "Sensor",
        pins=[volt.PinSpec("1", 1)],
        schematic_symbol=symbol,
    )
    u1 = design.instantiate(component, ref="U1")
    schematic = design.schematic("Main")
    schematic.place(u1, at=(10, 20))

    projection = json.loads(schematic.to_json())
    primitive = projection["symbol_definitions"][0]["primitives"][0]

    assert primitive["horizontal_alignment"] == "Start"
    assert primitive["vertical_alignment"] == "Middle"
    assert primitive["font_size"] == 3.25

def test_common_catalog_components_have_namespaced_default_symbol_refs():
    design = volt.Design("common-default-symbols")
    cases = _common_catalog_components(design)

    circuit = json.loads(design.to_json())

    for reference, component, expected_symbol, _expected_numbers in cases:
        assert component.schematic_symbol == expected_symbol
        definition = _definition_for_component(circuit, reference)
        assert definition["schematic_symbols"] == [
            {"name": expected_symbol, "variant": "default"}
        ]

def test_common_catalog_symbols_place_through_drawing_and_render():
    design = volt.Design("common-default-symbol-drawing")
    cases = _common_catalog_components(design)
    schematic = design.schematic("Main")

    placed = []
    with schematic.drawing(at=(20, 20), unit=20) as drawing:
        for index, (_reference, component, _symbol, _numbers) in enumerate(cases):
            placed.append(
                drawing.place(
                    component,
                    at=(20 + (index % 4) * 45, 20 + (index // 4) * 35),
                )
            )

    projection = json.loads(schematic.to_json())
    svg = schematic.to_svg()

    assert [symbol["name"] for symbol in projection["symbol_definitions"]] == [
        expected_symbol for _reference, _component, expected_symbol, _numbers in cases
    ]
    assert len(projection["symbol_instances"]) == len(cases)
    assert all(symbol["primitives"] for symbol in projection["symbol_definitions"])
    assert all(
        tuple(anchor.number for anchor in element.pin_anchors()) == expected_numbers
        for element, (_reference, _component, _symbol, expected_numbers) in zip(placed, cases)
    )
    assert placed[0].start.point == (20.0, 20.0)
    assert placed[0].end.point == (40.0, 20.0)
    assert tuple(anchor.name for anchor in placed[10].pin_anchors()) == ("+", "-")
    assert placed[12].IN.number == "3"
    assert placed[12].OUT.number == "2"
    assert placed[13]["IN+"].number == "3"
    assert placed[13]["IN-"].number == "2"

    for _reference, component, _symbol, _numbers in cases:
        assert f'data-component="component:{component.index}"' in svg
    assert "symbol-line" in svg
    assert "symbol-rectangle" in svg
    assert "symbol-circle" in svg

def test_schematic_placement_missing_default_symbol_reports_author_context():
    design = volt.Design("library-symbol-missing-default")
    sensor = design.define_component(
        "Sensor",
        pins=[volt.PinSpec("OUT", 1)],
    )
    u1 = design.instantiate(sensor, ref="U1")
    schematic = design.schematic("Main")
    before = schematic.to_json()

    try:
        schematic.place(u1, at=(10, 20))
    except ValueError as error:
        message = str(error)
        assert "No default schematic symbol" in message
        assert "U1" in message
        assert "sheet 'Main'" in message
        assert "pass symbol=" in message
        assert "schematic_symbol=" in message
    else:
        raise AssertionError("missing default schematic symbols should explain the fix")

    assert schematic.to_json() == before

def test_default_catalog_symbol_name_conflicts_reject_different_definitions():
    design = volt.Design("default-catalog-symbol-conflict")
    r1 = design.R("10k", ref="R1")
    schematic = design.schematic("Main")
    schematic.place(r1, at=(10, 20))

    try:
        schematic.register_symbol(
            _two_pin_test_symbol("volt.passives:resistor", label="CUSTOM")
        )
    except ValueError as error:
        assert "already exists with a different definition" in str(error)
    else:
        raise AssertionError("default catalog symbol name conflicts should be rejected")

def test_repeated_pin_labels_require_explicit_single_pin_addressing():
    design = volt.Design("repeated-pins")
    package = design.define_component(
        "RepeatedSupply",
        pins=[
            volt.PinSpec("VDD", 19, role="power", terminal="power", direction="input"),
            volt.PinSpec("VDD", 32, role="power", terminal="power", direction="input"),
            volt.PinSpec("GPIO", 1, role="bidirectional"),
        ],
    )
    u1 = design.instantiate(package, ref="U1")

    try:
        u1["VDD"]
    except ValueError as error:
        assert "ambiguous" in str(error)
        assert "pins('VDD')" in str(error)
    else:
        raise AssertionError("repeated pin label should require explicit addressing")

    assert u1[19].index == 0
    assert u1["VDD_32"].index == 1
    assert u1["GPIO"].index == 2

def test_schematic_placed_symbol_ambiguous_pin_name_reports_author_context():
    design = volt.Design("schematic-ambiguous-pin-context")
    supply = design.define_component(
        "Supply",
        pins=[
            volt.PinSpec("VDD", 1, role="power"),
            volt.PinSpec("VDD", 2, role="power"),
        ],
        schematic_symbol=volt.SchematicSymbolSpec(
            "volt.test:Supply",
            pins=(
                volt.SchematicSymbolSpec.pin("VDD", 1, (0, 0), "Left"),
                volt.SchematicSymbolSpec.pin("VDD", 2, (20, 0), "Right"),
            ),
            primitives=(volt.SchematicSymbolSpec.line((0, 0), (20, 0)),),
        ),
    )
    u1 = design.instantiate(supply, ref="U1")
    schematic = design.schematic("Main")
    placed = schematic.place(u1, at=(10, 20))

    try:
        placed.pin("VDD")
    except ValueError as error:
        message = str(error)
        assert "ambiguous" in message
        assert "VDD" in message
        assert "U1" in message
        assert "sheet 'Main'" in message
        assert "'1'" in message
        assert "'2'" in message
        assert "pins('VDD')" in message
    else:
        raise AssertionError("ambiguous schematic pin names should carry author context")

def test_repeated_pin_group_connects_all_matching_package_pins():
    design = volt.Design("repeated-group")
    package = design.define_component(
        "RepeatedSupply",
        pins=[
            volt.PinSpec("VDD", 19, role="power", terminal="power", direction="input"),
            volt.PinSpec("VDD", 32, role="power", terminal="power", direction="input"),
            volt.PinSpec("VSS", 18, role="ground", terminal="ground", direction="passive"),
            volt.PinSpec("VSS", 63, role="ground", terminal="ground", direction="passive"),
        ],
    )
    u1 = design.instantiate(package, ref="U1")

    vdd = design.net("VDD", kind="power")
    gnd = design.net("GND", kind="ground")
    vdd += u1.pins("VDD")
    gnd += u1.pins("VSS")

    circuit = json.loads(design.to_json())
    nets = {net["name"]: net for net in circuit["nets"]}

    assert len(u1.pins("VDD")) == 2
    assert nets["VDD"]["pins"] == ["pin:0", "pin:1"]
    assert nets["GND"]["pins"] == ["pin:2", "pin:3"]

def test_pin_spec_electrical_semantics_are_kernel_owned():
    design = volt.Design("pin-semantics")

    timer = design.define_component(
        "Timer",
        pins=[
            volt.PinSpec(
                "RESET",
                4,
                role="input",
                terminal="signal",
                direction="input",
                signal="digital",
                drive="high_impedance",
                polarity="active_low",
                voltage_range=(0.0, 5.5),
            ),
            volt.PinSpec(
                "VCC",
                8,
                role="power",
                terminal="power",
                direction="input",
                voltage_range=(4.5, 16.0),
            ),
            volt.PinSpec("GND", 1, role="ground", terminal="ground", direction="passive"),
        ],
    )
    design.instantiate(timer, ref="U1")

    circuit = json.loads(design.to_json())
    pin_definitions = {pin["name"]: pin for pin in circuit["pin_definitions"]}

    reset = pin_definitions["RESET"]
    assert reset["terminal_kind"] == "Signal"
    assert reset["direction"] == "Input"
    assert reset["signal_domain"] == "Digital"
    assert reset["drive_kind"] == "HighImpedance"
    assert reset["polarity"] == "ActiveLow"
    assert reset["electrical_attributes"]["voltage_range"] == {
        "type": "range",
        "dimension": "voltage",
        "minimum": 0.0,
        "maximum": 5.5,
    }

    assert pin_definitions["VCC"]["terminal_kind"] == "Power"
    assert pin_definitions["VCC"]["electrical_attributes"]["voltage_range"]["minimum"] == 4.5
    assert pin_definitions["GND"]["terminal_kind"] == "Ground"

def _resistor_0603_footprint():
    return volt.Footprint(
        library="Resistor_SMD",
        name="R_0603_1608Metric",
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-0.75, 0.0), size=(0.80, 0.95)),
            volt.FootprintPad.surface_mount("2", at=(0.75, 0.0), size=(0.80, 0.95)),
        ),
    )


def _sot23_footprint():
    return volt.Footprint(
        library="Package_TO_SOT_SMD",
        name="SOT-23",
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-0.9, -0.95), size=(0.8, 0.8)),
            volt.FootprintPad.surface_mount("2", at=(0.9, -0.95), size=(0.8, 0.8)),
            volt.FootprintPad.surface_mount("3", at=(0.0, 0.95), size=(0.8, 0.8)),
        ),
    )


def _soic8_footprint():
    pad_positions = {
        1: (-2.0, 0.0),
        2: (-2.0, 1.27),
        3: (-2.0, 2.54),
        4: (-2.0, 3.81),
        8: (2.0, 0.0),
    }
    return volt.Footprint(
        library="Package_SO",
        name="SOIC-8_3.9x4.9mm_P1.27mm",
        pads=tuple(
            volt.FootprintPad.surface_mount(
                str(number),
                at=pad_positions[number],
                size=(0.7, 1.4),
            )
            for number in pad_positions
        ),
    )


def _tie_and_mechanical_footprint():
    return volt.Footprint(
        ("volt.test", "TieAndMechanical"),
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-1.0, 0.0), size=(0.6, 0.6)),
            volt.FootprintPad.surface_mount("2", at=(0.0, 0.0), size=(0.6, 0.6)),
            volt.FootprintPad.surface_mount("4", at=(1.0, 0.0), size=(0.6, 0.6)),
            volt.FootprintPad.through_hole(
                "MH",
                at=(0.0, 2.0),
                size=(1.8, 1.8),
                drill=volt.FootprintDrill(1.0, plating="non_plated"),
                layers="mechanical_hole",
                mechanical_role="mounting",
            ),
        ),
    )


def _library_resistor_part(name="R_0603_10K", *, provenance=None):
    return volt.Part(
        name=name,
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        symbol=_two_pin_test_symbol(f"volt.test:{name}"),
        footprint=_resistor_0603_footprint(),
        pads={1: "1", 2: "2"},
        value="10k",
        manufacturer="Yageo",
        mpn="RC0603FR-0710KL",
        package="0603",
        provenance=provenance,
        prefix="R",
    )


def _resistor_0603_family(library, **overrides):
    defaults = {
        "prefix": "R",
        "package": "0603",
        "pins": [volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        "symbol": _two_pin_test_symbol("volt.test:R_0603"),
        "footprint": _resistor_0603_footprint(),
        "pads": {1: "1", 2: "2"},
        "manufacturer": "Yageo",
        "properties": {"kind": "resistor"},
    }
    defaults.update(overrides)
    return library.part_family(**defaults)


def test_library_parts_family_registers_repeated_resistor_catalog_parts():
    library = volt.Library("volt.test.passives")
    r0603 = _resistor_0603_family(library)

    ten_k = r0603.part("10K", mpn="RC0603FR-0710KL")
    hundred_k = r0603.part("100K", mpn="RC0603FR-07100KL")

    assert isinstance(ten_k, volt.Part)
    assert ten_k.name == "R_0603_10K"
    assert ten_k.value == "10K"
    assert ten_k.properties["kind"] == "resistor"
    assert ten_k.properties["value"] == "10K"
    assert hundred_k.name == "R_0603_100K"
    assert library["R_0603_10K"] is ten_k
    assert library["R_0603_100K"] is hundred_k
    assert isinstance(library.parts, tuple)
    assert not callable(library.parts)
    assert [part.name for part in library.parts] == ["R_0603_100K", "R_0603_10K"]

    result = library.build()

    assert result.ok
    assert [part.name for part in result.parts] == ["R_0603_100K", "R_0603_10K"]
    assert all(part.board_ready for part in result.parts)


def test_library_parts_family_overrides_are_isolated_snapshots():
    default_pads = {1: ["1"], 2: "2"}
    default_properties = {
        "kind": "resistor",
        "series": {"name": "RC"},
        "tags": ["default"],
    }
    library = volt.Library("volt.test.passives")
    r0603 = _resistor_0603_family(
        library,
        pads=default_pads,
        properties=default_properties,
        source_version="catalog-v1",
    )

    default_pads[1].append("9")
    default_properties["series"]["name"] = "changed"
    default_properties["tags"].append("changed")

    override_pads = {1: ["1"], 2: "2"}
    override_properties = {"tolerance": {"percent": 1}}
    ten_k = r0603.part(
        "10K",
        mpn="RC0603FR-0710KL",
        pads=override_pads,
        properties=override_properties,
        source_name="catalog/R0603/10K",
        source_version="catalog-v2",
    )

    override_pads[1].append("9")
    override_properties["tolerance"]["percent"] = 5

    hundred_k = r0603.part(
        "100K",
        value="100 kohm",
        mpn="RC0603FR-07100KL",
        manufacturer="KOA",
        package="0603",
        pads={1: "1", 2: "2"},
        source_name="catalog/R0603/100K",
    )

    assert ten_k.pads[1] == ("1",)
    assert ten_k.properties["kind"] == "resistor"
    assert ten_k.properties["series"]["name"] == "RC"
    assert ten_k.properties["tags"] == ("default",)
    assert ten_k.properties["tolerance"]["percent"] == 1
    assert ten_k.source_name == "catalog/R0603/10K"
    assert ten_k.source_version == "catalog-v2"
    assert hundred_k.name == "R_0603_100K"
    assert hundred_k.value == "100 kohm"
    assert hundred_k.mpn == "RC0603FR-07100KL"
    assert hundred_k.manufacturer == "KOA"
    assert hundred_k.package == "0603"
    assert "tolerance" not in hundred_k.properties
    assert hundred_k.source_name == "catalog/R0603/100K"
    assert hundred_k.source_version == "catalog-v1"


def test_library_parts_family_duplicate_names_fail_at_library_boundary():
    library = volt.Library("volt.test.passives")
    r0603 = _resistor_0603_family(library)

    r0603.part("10K", mpn="RC0603FR-0710KL")

    try:
        r0603.part("10K", mpn="RC0603FR-0710KL")
    except ValueError as error:
        assert "already exists" in str(error)
        assert "R_0603_10K" in str(error)
    else:
        raise AssertionError("helper-created part names should use Library.add duplicates")

    direct_library = volt.Library("volt.test.direct.passives")
    direct_library.add(_library_resistor_part())
    direct_r0603 = _resistor_0603_family(direct_library)

    try:
        direct_r0603.part("10K", mpn="RC0603FR-0710KL")
    except ValueError as error:
        assert "already exists" in str(error)
        assert "R_0603_10K" in str(error)
    else:
        raise AssertionError("direct part names should block helper duplicates")


def test_library_result_orders_helper_and_direct_parts_deterministically():
    library = volt.Library("volt.test.passives")
    r0603 = _resistor_0603_family(library)

    zeta = library.add(_library_resistor_part(name="Z_Direct_1K"))
    ten_k = r0603.part("10K", mpn="RC0603FR-0710KL")
    one_k = r0603.part("1K", mpn="RC0603FR-071KL")

    result = library.build()

    assert library["Z_Direct_1K"] is zeta
    assert library["R_0603_10K"] is ten_k
    assert library["R_0603_1K"] is one_k
    assert [part.name for part in library.parts] == [
        "R_0603_10K",
        "R_0603_1K",
        "Z_Direct_1K",
    ]
    assert [part.name for part in result.parts] == [
        "R_0603_10K",
        "R_0603_1K",
        "Z_Direct_1K",
    ]


def test_library_build_validates_board_ready_part():
    library = volt.Library("volt.test.passives")
    part = _library_resistor_part()

    library.add(part)
    result = library.build()

    assert result.ok
    assert tuple(result.diagnostics) == ()
    part_result = result.part("R_0603_10K")
    assert part_result.schematic_ready
    assert part_result.board_ready
    assert part_result.serializable
    assert part_result.has_footprint
    assert part_result.pad_mapping_complete
    assert library["R_0603_10K"] is part


def test_part_instantiation_requires_library_identity():
    part = _library_resistor_part()
    design = volt.Design("unbound-part")

    try:
        design.instantiate(part, ref="R1")
    except ValueError as error:
        assert "Library" in str(error)
    else:
        raise AssertionError("unbound parts should not be directly instantiated")


def test_library_part_is_immutable_after_construction():
    library = volt.Library("volt.test.passives")
    part = _library_resistor_part()
    library.add(part)

    try:
        part.name = "Changed"
    except AttributeError as error:
        assert "immutable" in str(error)
    else:
        raise AssertionError("part mutation should be rejected")

    assert library["R_0603_10K"] is part
    assert part.name == "R_0603_10K"


def test_library_part_typed_provenance_lowers_into_native_artifact():
    library = volt.Library("volt.test.passives")
    part = _library_resistor_part(
        provenance=volt.PartProvenance(
            datasheet="https://example.test/resistor.pdf",
            authored_by="Volt test",
            derived_from="vendor catalogue",
        )
    )
    library.add(part)

    artifact = library.build().part(part.name).artifact
    assert artifact is not None
    payload = json.loads(artifact.bytes)
    assert payload["provenance"] == {
        "datasheet": "https://example.test/resistor.pdf",
        "authored_by": "Volt test",
        "derived_from": "vendor catalogue",
    }


def test_library_part_collection_fields_are_immutable_snapshots():
    pads = {1: ["1"], 2: "2"}
    properties = {"bin": "A"}
    symbol_primitive = volt.SchematicSymbolSpec.line((0, 0), (20, 0))
    symbol = volt.SchematicSymbolSpec(
        "volt.test:R_0603_10K_nested",
        pins=(
            volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),
            volt.SchematicSymbolSpec.pin("2", 2, (20, 0), "Right"),
        ),
        primitives=(symbol_primitive,),
    )
    library = volt.Library("volt.test.passives")
    part = volt.Part(
        name="R_0603_10K_nested",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        symbol=symbol,
        footprint=_resistor_0603_footprint(),
        pads=pads,
        value="10k",
        manufacturer="Yageo",
        mpn="RC0603FR-0710KL",
        package="0603",
        properties=properties,
        prefix="R",
    )

    pads[1].append("9")
    properties["bin"] = "B"
    symbol_primitive["start"]["x"] = 99
    library.add(part)

    def assert_rejects_mutation(callback):
        try:
            callback()
        except (AttributeError, TypeError):
            return
        raise AssertionError("part collection mutation should be rejected")

    def mutate_pads():
        part.pads[1] += ("9",)

    def mutate_properties():
        part.properties["bin"] = "B"

    def mutate_symbol_primitive():
        part.schematic_symbols[0].primitives[0]["start"]["x"] = 99

    assert tuple(part.pads[1]) == ("1",)
    assert part.properties["bin"] == "A"
    assert part.schematic_symbols[0].primitives[0]["start"]["x"] == 0.0

    for mutation in (
        mutate_pads,
        mutate_properties,
        mutate_symbol_primitive,
    ):
        assert_rejects_mutation(mutation)

    result = library.build()

    assert result.part("R_0603_10K_nested").serializable
    payload = part._to_dict()
    assert payload["schematic_symbols"][0]["primitives"][0]["start"]["x"] == 0.0
    json.dumps(payload)


def test_project_instantiates_imported_part_without_manual_footprint_cache():
    library = volt.Library("volt.test.passives")
    resistor_part = _library_resistor_part()
    library.add(resistor_part)
    project = volt.Project("part-project")

    @project.design
    def design():
        d = volt.Design("part-project")
        r1 = d.instantiate(resistor_part, ref="R1")
        left = d.net("LEFT")
        right = d.net("RIGHT")
        r1.dnp(False)
        left += r1[1]
        right += r1[2]
        return d

    @project.board
    def board(context):
        design = context.design()
        pcb = design.add_board("Main")
        pcb.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))
        pcb.place(design.component("R1"), at=(10.0, 6.0))
        return pcb

    result = project.run()

    assert result.ok
    document = json.loads(result.board().to_json())
    definitions = document["board"]["footprint_definitions"]
    assert [definition["ref"] for definition in definitions] == [
        {"library": "Resistor_SMD", "name": "R_0603_1608Metric"}
    ]
    assert "footprint" not in document["board"]["placements"][0]


def test_part_pin_pad_mapping_supports_tied_pads():
    library = volt.Library("volt.test.connectors")
    connector = volt.Part(
        name="TieAndMechanical",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        symbol=_two_pin_test_symbol("volt.test:TieAndMechanical"),
        footprint=_tie_and_mechanical_footprint(),
        pads={1: "1", 2: ("2", "4")},
        manufacturer="Volt",
        mpn="TIE-MECH",
        package="custom",
        prefix="J",
    )
    library.add(connector)
    design = volt.Design("part-tied-pads")
    j1 = design.instantiate(connector, ref="J1")
    a_net = design.net("A")
    a_net += j1[1]
    tied_net = design.net("B")
    tied_net += j1[2]
    board = design.add_board("Main")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))
    board.place(j1, at=(10.0, 6.0))

    result = library.build()
    resolutions = {resolution.pad_label: resolution for resolution in board.resolve_pads()}
    artifact = json.loads(result.part("TieAndMechanical").artifact.bytes)

    assert result.ok
    assert set(resolutions) == {"1", "2", "4", "MH"}
    assert artifact["orderable_part"]["terminal_pad_mappings"] == [
        {"terminal": "1", "pads": ["1"]},
        {"terminal": "2", "pads": ["2", "4"]},
    ]
    assert "PCB_FOOTPRINT_UNRESOLVED" not in {
        diagnostic.code for diagnostic in board.validate()
    }


def test_part_validation_rejects_unknown_pad_label():
    library = volt.Library("volt.test.bad")
    library.add(
        volt.Part(
            name="BadPad",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:BadPad"),
            footprint=_resistor_0603_footprint(),
            pads={1: "1", 2: "9"},
            manufacturer="Yageo",
            mpn="BADPAD",
            package="0603",
        )
    )

    with pytest.raises(volt.CrossReferenceError, match="foreign footprint pad"):
        library.build()


def test_part_validation_rejects_closed_footprint_polygons_at_artifact_boundary():
    library = volt.Library("volt.test.bad")
    library.add(
        volt.Part(
            name="ClosedCourtyard",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:ClosedCourtyard"),
            footprint=volt.Footprint(
                ("volt.test", "ClosedCourtyard"),
                pads=(
                    volt.FootprintPad.surface_mount("1", at=(-0.75, 0.0), size=(0.8, 0.95)),
                    volt.FootprintPad.surface_mount("2", at=(0.75, 0.0), size=(0.8, 0.95)),
                ),
                courtyard=((-1.2, -0.8), (1.2, -0.8), (1.2, 0.8), (-1.2, 0.8), (-1.2, -0.8)),
            ),
            pads={1: "1", 2: "2"},
            manufacturer="Yageo",
            mpn="CLOSEDCOURTYARD",
            package="0603",
        )
    )

    with pytest.raises(volt.InvalidArgumentError, match="must not repeat vertices"):
        library.build()


def test_part_validation_rejects_unresolvable_pad_mapping_key():
    library = volt.Library("volt.test.bad")
    library.add(
        volt.Part(
            name="BadPadKey",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:BadPadKey"),
            footprint=volt.Footprint(
                ("volt.test", "BadPadKey"),
                pads=(
                    volt.FootprintPad.surface_mount("1", at=(-1.0, 0.0), size=(0.6, 0.6)),
                    volt.FootprintPad.surface_mount("2", at=(0.0, 0.0), size=(0.6, 0.6)),
                    volt.FootprintPad.surface_mount("3", at=(1.0, 0.0), size=(0.6, 0.6)),
                ),
            ),
            pads={1: "1", 2: "2", 99: "3"},
            manufacturer="Yageo",
            mpn="BADPADKEY",
            package="0603",
        )
    )

    with pytest.raises(volt.CrossReferenceError, match="Every package terminal must map"):
        library.build()


def test_part_validation_accepts_exact_part_without_optional_symbol_projection():
    library = volt.Library("volt.test.bad")
    library.add(
        volt.Part(
            name="NoSymbol",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            footprint=_resistor_0603_footprint(),
            pads={1: "1", 2: "2"},
            manufacturer="Yageo",
            mpn="NOSYMBOL",
            package="0603",
        )
    )

    result = library.build()

    assert result.ok
    assert result.part("NoSymbol").schematic_ready is False


def test_part_validation_allows_mechanical_pads_without_logical_pin_mapping():
    library = volt.Library("volt.test.mechanical")
    library.add(
        volt.Part(
            name="MechanicalPad",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:MechanicalPad"),
            footprint=_tie_and_mechanical_footprint(),
            pads={1: "1", 2: ("2", "4")},
            manufacturer="Volt",
            mpn="TIE-MECH",
            package="custom",
        )
    )

    result = library.build()

    assert result.ok
    assert tuple(result.diagnostics) == ()


def test_part_validation_rejects_unknown_mechanical_pad_role():
    library = volt.Library("volt.test.mechanical")
    library.add(
        volt.Part(
            name="TypoMechanicalPad",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:TypoMechanicalPad"),
            footprint=volt.Footprint(
                ("volt.test", "TypoMechanicalPad"),
                pads=(
                    volt.FootprintPad.surface_mount("1", at=(-1.0, 0.0), size=(0.6, 0.6)),
                    volt.FootprintPad.surface_mount("2", at=(0.0, 0.0), size=(0.6, 0.6)),
                    volt.FootprintPad.surface_mount(
                        "MP",
                        at=(1.0, 0.0),
                        size=(0.6, 0.6),
                        mechanical_role="mountng",
                    ),
                ),
            ),
            pads={1: "1", 2: "2"},
            manufacturer="Volt",
            mpn="TYPO-MECH",
            package="custom",
        )
    )

    with pytest.raises(ValueError, match="Unknown footprint pad mechanical role"):
        library.build()


def test_part_validation_rejects_unowned_electrical_footprint_pads():
    library = volt.Library("volt.test.incomplete")
    library.add(
        volt.Part(
            name="MissingElectricalPad",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:MissingElectricalPad"),
            footprint=volt.Footprint(
                ("volt.test", "ExtraElectrical"),
                pads=(
                    *_resistor_0603_footprint().pads,
                    volt.FootprintPad.surface_mount("3", at=(2.25, 0.0), size=(0.6, 0.6)),
                ),
            ),
            pads={1: "1", 2: "2"},
            manufacturer="Yageo",
            mpn="EXTRA",
            package="0603",
        )
    )

    with pytest.raises(volt.CrossReferenceError, match="ownership does not match"):
        library.build()


def test_part_validation_reports_non_serializable_source_metadata():
    library = volt.Library("volt.test.non-serializable")
    library.add(
        volt.Part(
            name="NonSerializable",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:NonSerializable"),
            footprint=_resistor_0603_footprint(),
            pads={1: "1", 2: "2"},
            manufacturer="Yageo",
            mpn="NON-SERIAL",
            package="0603",
            properties={"factory": object()},
        )
    )

    result = library.build()

    assert not result.ok
    assert [(diagnostic.source, diagnostic.code) for diagnostic in result.diagnostics] == [
        ("part:NonSerializable", "LIBRARY_PART_NON_SERIALIZABLE")
    ]
    assert result.part("NonSerializable").serializable is False


def test_library_result_is_deterministic():
    library = volt.Library("volt.test.deterministic")
    library.add(
        volt.Part(
            name="Zeta",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            footprint=_resistor_0603_footprint(),
            pads={1: "1", 2: "2"},
            manufacturer="Yageo",
            mpn="ZETA",
            package="0603",
        )
    )
    library.add(
        volt.Part(
            name="Alpha",
            pins=[],
            footprint=None,
            pads={},
        )
    )

    first = library.build().to_dict()
    second = library.build().to_dict()

    assert first == second
    assert [
        (diagnostic["source"], diagnostic["code"])
        for diagnostic in first["diagnostics"]["diagnostics"]
    ] == [
        ("part:Alpha", "LIBRARY_PART_MISSING_PINS"),
        ("part:Alpha", "LIBRARY_PART_MISSING_FOOTPRINT"),
    ]


def test_part_ref_only_missing_geometry_is_not_an_exact_instantiation_route():
    library = volt.Library("volt.test.missing_geometry")
    resistor = volt.Part(
        name="MissingGeometry",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        footprint=("missing", "NotARealFootprint"),
        pads={1: "1", 2: "2"},
        manufacturer="Yageo",
        mpn="RC0603FR-071KL",
        package="0603",
        prefix="R",
    )
    library.add(resistor)
    design = volt.Design("part-missing-footprint")
    result = library.build()

    assert not result.ok
    assert [diagnostic.code for diagnostic in result.diagnostics] == [
        "LIBRARY_PART_MISSING_FOOTPRINT_GEOMETRY"
    ]
    with pytest.raises(ValueError, match="complete native exact part"):
        design.instantiate(resistor, ref="R1")
    assert design.components() == ()


def test_footprint_rejects_empty_public_identity():
    try:
        volt.Footprint(library="", name="R_0603_1608Metric", pads=())
    except ValueError:
        pass
    else:
        raise AssertionError("empty footprint library should be rejected")

    try:
        volt.Footprint(library="Resistor_SMD", name="", pads=())
    except ValueError:
        pass
    else:
        raise AssertionError("empty footprint name should be rejected")


def test_pcb_readiness_requires_selected_physical_parts():
    design = volt.Design("pcb-readiness")
    r1 = design.R("10k", ref="R1")
    signal = design.net("SIGNAL")
    signal += r1[1]

    logical_report = design.validate()
    pcb_report = design.validate_for_pcb()

    assert "PHYSICAL_PART_REQUIRED" not in {diagnostic.code for diagnostic in logical_report}
    assert "PHYSICAL_PART_REQUIRED" in {diagnostic.code for diagnostic in pcb_report}


def test_default_validation_includes_exact_selected_part_erc_without_a_board():
    design = _overvoltage_exact_part_design()

    default_report = design.validate()
    pcb_report = design.validate_for_pcb()
    code = "SELECTED_PART_VOLTAGE_ABSOLUTE_LIMIT_VIOLATION"

    assert sum(diagnostic.code == code for diagnostic in default_report) == 1
    assert sum(diagnostic.code == code for diagnostic in pcb_report) == 1
