import importlib
import inspect
import json
import re
from collections import Counter
from pathlib import Path
from tempfile import TemporaryDirectory

import volt


def _project_bundle_texts(bundle):
    return {
        path.relative_to(bundle).as_posix(): path.read_text(encoding="utf-8")
        for path in sorted(bundle.rglob("*"))
        if path.is_file()
    }


def test_stm32_usb_buck_example_exposes_project_result():
    main = importlib.import_module("examples.stm32_usb_buck.main")

    result = main.run_project()

    assert isinstance(result, volt.ProjectResult)
    assert [stage.name for stage in result.stages] == ["design", "schematic"]
    assert result.design().name == "stm32_usb_buck"
    assert result.schematic().name == "STM32 USB Buck"
    assert not result.diagnostics.errors(stage="schematic")
    assert result.test_failures() == ()


def test_stm32_usb_buck_project_schematic_stage_is_primary_authoring_function():
    main = importlib.import_module("examples.stm32_usb_buck.main")

    source = inspect.getsource(main.build_project)

    assert 'context.resource("stm32_board", Stm32UsbBuckBoard)' in source
    assert "build_schematic(" not in source
    assert "_author_power_region(power_region, board, nets)" in source
    assert "_author_mcu_region(mcu_region, board, nets)" in source
    assert "_author_connectors_region(connectors_region, board, nets)" in source


def _schematic_authoring_source(module):
    return "\n".join(
        inspect.getsource(getattr(module, name))
        for name in (
            "_author_power_region",
            "_author_mcu_region",
            "_author_connectors_region",
        )
    )


def test_stm32_usb_buck_example_writes_stable_logical_artifacts():
    main = importlib.import_module("examples.stm32_usb_buck.main")
    schematic_symbols = importlib.import_module("examples.stm32_usb_buck.schematic_symbols")

    with TemporaryDirectory() as temp_dir:
        artifacts = main.write_artifacts(Path(temp_dir))

        logical = json.loads(artifacts.logical_json.read_text(encoding="utf-8"))
        schematic = json.loads(artifacts.schematic_json.read_text(encoding="utf-8"))
        validation = json.loads(artifacts.diagnostics_json.read_text(encoding="utf-8"))
        first_logical_text = artifacts.logical_json.read_text(encoding="utf-8")
        first_schematic_text = artifacts.schematic_json.read_text(encoding="utf-8")
        first_svg_text = artifacts.schematic_svg.read_text(encoding="utf-8")
        first_body_svg_text = artifacts.schematic_body_svg.read_text(encoding="utf-8")
        first_page_texts = tuple(
            path.read_text(encoding="utf-8") for path in artifacts.schematic_svg_pages
        )
        first_validation_text = artifacts.diagnostics_json.read_text(encoding="utf-8")
        first_project_texts = _project_bundle_texts(
            artifacts.logical_json.parent / "stm32_usb_buck.volt"
        )

        stale_page = artifacts.schematic_svg_pages[0].parent / "stale.svg"
        stale_page.write_text("<svg></svg>\n", encoding="utf-8")
        repeated_artifacts = main.write_artifacts(Path(temp_dir))
        assert not stale_page.exists()
        assert [path.name for path in repeated_artifacts.schematic_svg_pages] == [
            "stm32_usb_buck_STM32_USB_Buck.svg"
        ]

        second_artifacts = main.write_artifacts(Path(temp_dir) / "second")
        assert second_artifacts.logical_json.read_text(encoding="utf-8") == first_logical_text
        assert second_artifacts.schematic_json.read_text(encoding="utf-8") == first_schematic_text
        assert second_artifacts.schematic_svg.read_text(encoding="utf-8") == first_svg_text
        assert (
            second_artifacts.schematic_body_svg.read_text(encoding="utf-8")
            == first_body_svg_text
        )
        assert (
            tuple(path.read_text(encoding="utf-8") for path in second_artifacts.schematic_svg_pages)
            == first_page_texts
        )
        assert (
            second_artifacts.diagnostics_json.read_text(encoding="utf-8")
            == first_validation_text
        )
        assert (
            _project_bundle_texts(second_artifacts.logical_json.parent / "stm32_usb_buck.volt")
            == first_project_texts
        )

    assert logical["format"] == "volt.logical_circuit"
    assert {item["name"] for item in logical["module_definitions"]} >= {
        "UsbInterface",
        "McuSupport",
        "PowerInputAndRegulator",
    }
    assert "volt.benchmarks.stm32_usb_buck" in {
        definition["source"]["namespace"]
        for definition in logical["component_definitions"]
        if "source" in definition
    }

    net_names = {net["name"] for net in logical["nets"]}
    assert {"+12V", "+5V", "+3V3", "VDDA", "GND", "USB_DP", "USB_DM"} <= net_names
    net_ids = {net["id"] for net in logical["nets"]}
    component_ids = {component["id"] for component in logical["components"]}

    assert schematic["format"] == "volt.schematic"
    assert [sheet["name"] for sheet in schematic["sheets"]] == ["STM32 USB Buck"]
    sheet = schematic["sheets"][0]
    assert sheet["metadata"]["title"] == "STM32 USB Buck Reference Schematic"
    assert sheet["metadata"]["orientation"] == "Landscape"
    assert sheet["metadata"]["size"] == {"width": 594, "height": 420}
    assert sheet["metadata"]["coordinate_zones"] == {
        "columns": 16,
        "rows": 10,
        "visible": True,
    }
    assert sheet["metadata"]["grid"] == {"spacing": 5, "visible": False}
    assert sheet["metadata"]["title_block"] == [
        {"key": "Number", "value": "1"},
        {"key": "Page Count", "value": "1"},
        {"key": "Revision", "value": "A"},
        {"key": "Date", "value": "2026-05-18"},
        {"key": "Project", "value": "Volt STM32 USB Buck"},
        {"key": "File", "value": "examples/stm32_usb_buck/schematic_output.py"},
    ]
    assert [region["name"] for region in sheet["regions"]] == [
        "Power Circuitry",
        "STM32 Microcontroller",
        "Connectors and USB",
    ]
    assert all(region["style"] == {"border": "dashed"} for region in sheet["regions"])
    assert len(schematic["symbol_instances"]) >= 10
    assert len(schematic["wire_runs"]) >= 20
    assert 1 <= len(schematic["net_labels"]) <= 10
    assert 8 <= len(schematic["power_ports"]) <= 32
    assert len(schematic["sheet_ports"]) == 25
    assert {
        port["name"] for port in schematic["sheet_ports"]
    } >= {"USB D+", "USB D-", "SWDIO", "SWCLK", "SWO", "BOOT0", "VCAP1", "VCAP2"}
    assert {port["kind"] for port in schematic["sheet_ports"]} == {"Bidirectional"}
    assert len(schematic["no_connect_markers"]) <= 6
    assert {instance["component"] for instance in schematic["symbol_instances"]} <= component_ids
    assert {wire["net"] for wire in schematic["wire_runs"]} <= net_ids
    assert {label["net"] for label in schematic["net_labels"]} <= net_ids
    assert {port["net"] for port in schematic["power_ports"]} <= net_ids
    assert {port["net"] for port in schematic["sheet_ports"]} <= net_ids
    assert {
        marker["pin"] for marker in schematic["no_connect_markers"]
    } <= {pin["id"] for pin in logical["pins"]}
    assert {definition["name"] for definition in schematic["symbol_definitions"]} >= {
        "volt.benchmarks.stm32_usb_buck:STM32F405RGTx",
        "volt.benchmarks.stm32_usb_buck:AP1117_15",
        "volt.examples.stm32_usb_buck:ReadableUSBMicroB",
        "volt.examples.stm32_usb_buck:PlainCapacitor",
        "volt.examples.stm32_usb_buck:PlainResistor",
    }
    object_collections = (
        "symbol_instances",
        "wire_runs",
        "net_labels",
        "junctions",
        "power_ports",
        "symbol_fields",
    )
    for collection in object_collections:
        assert {item["sheet"] for item in schematic[collection]} == {"sheet:0"}
    assert {
        item["authored_region"]
        for collection in object_collections
        for item in schematic[collection]
        if "authored_region" in item
    } >= {"Power Circuitry", "STM32 Microcontroller", "Connectors and USB"}
    reference_fields = [
        field for field in schematic["symbol_fields"] if field["name"] == "reference"
    ]
    reference_labels = [field["value"] for field in reference_fields]
    symbol_instances_by_id = {
        instance["id"]: instance for instance in schematic["symbol_instances"]
    }
    component_references_by_id = {
        component["id"]: component["reference"] for component in logical["components"]
    }
    component_values_by_reference = {
        component["reference"]: component["properties"]["value"]["value"]
        for component in logical["components"]
        if "value" in component.get("properties", {})
    }
    placed_component_references = {
        component_references_by_id[instance["component"]]
        for instance in schematic["symbol_instances"]
    }
    assert placed_component_references == set(schematic_symbols.DISPLAY_REFERENCES)
    assert {
        field["value"]
        for field in reference_fields
    } == {
        schematic_symbols.DISPLAY_REFERENCES[reference]
        for reference in placed_component_references
    }
    assert {
        schematic_symbols.DISPLAY_REFERENCES[
            component_references_by_id[
                symbol_instances_by_id[field["symbol_instance"]]["component"]
            ]
        ]
        for field in reference_fields
    } == set(reference_labels)
    assert len(reference_labels) == len(set(reference_labels))
    assert all(re.fullmatch(r"(?:C|D|J|R|SW|U|Y)\d+", label) for label in reference_labels)
    assert all("/" not in label and "_" not in label for label in reference_labels)
    assert component_values_by_reference["USB/J1"] == "USB Micro-B"
    assert component_values_by_reference["USB/U1"] == "USBLC6-4SC6"
    assert component_values_by_reference["J2"] == "SWD 10-pin"
    assert component_values_by_reference["J3"] == "GPIO 1x4"
    internal_reference_labels = {
        "VIN_SRC",
        "U3V3",
        "CVDD",
        "CVCAP1",
        "CVCAP2",
        "RRESET",
        "RBOOT",
        "SWBOOT",
        "CHSEIN",
        "CHSEOUT",
    }
    assert internal_reference_labels.isdisjoint(reference_labels)
    power_port_labels = {
        port["label"] for port in schematic["power_ports"] if "label" in port
    }
    assert "GND" in power_port_labels
    assert all("/" not in label for label in power_port_labels)
    symbol_field_values = {field["value"] for field in schematic["symbol_fields"]}
    assert {
        "AP1117-5.0",
        "AP1117-3.3",
        "100 nF",
        "4.7 uF",
        "2.2 uF",
        "10 kOhm",
        "100 kOhm",
        "18 pF",
        "330 Ohm",
        "8 MHz",
        "STM32F405RGT6",
        "USB Micro-B",
    } <= symbol_field_values
    assert all(field["orientation"] == "Right" for field in schematic["symbol_fields"])
    assert {label["orientation"] for label in schematic["net_labels"]} <= {"Left", "Right"}
    junction_keys = [
        (junction["sheet"], junction["net"], junction["position"]["x"], junction["position"]["y"])
        for junction in schematic["junctions"]
    ]
    assert len(junction_keys) == len(set(junction_keys))
    composition_source = inspect.getsource(main.build_project)
    authoring_source = _schematic_authoring_source(main)
    schematic_source = composition_source + "\n" + authoring_source
    assert "class _SchematicAuthor" not in schematic_source
    assert "audit_no_fallback_pin_coverage" not in schematic_source
    assert ".region(" in composition_source
    assert ".drawing(" in authoring_source
    assert "region.drawing(" in authoring_source
    assert "drawing.frame(" not in authoring_source
    assert re.search(r"\bat=\(\s*\d", authoring_source) is None
    assert "drawing.move_from(" in authoring_source
    assert "drawing.stack(" in authoring_source
    assert "drawing.two_terminal(" in authoring_source
    assert "drawing.C(" not in schematic_source
    assert "drawing.R(" not in schematic_source
    assert "drawing.LED(" not in schematic_source
    assert "drawing.connect(" in authoring_source
    assert "drawing.signal_tag(" in authoring_source
    assert "drawing.signal_tags(" in authoring_source
    assert "drawing.signal_stub(" not in authoring_source
    assert "drawing.no_connect(" in authoring_source
    assert "shape=" in authoring_source
    assert 'name="value"' not in schematic_source
    label_counts = Counter(
        (label["sheet"], label["net"]) for label in schematic["net_labels"]
    )
    assert label_counts
    assert max(label_counts.values()) <= 4

    symbol_text_by_definition = {
        definition["name"]: {
            primitive["text"]
            for primitive in definition["primitives"]
            if primitive["type"] == "text"
        }
        for definition in schematic["symbol_definitions"]
    }
    assert {
        "PA11",
        "44",
        "PA12",
        "45",
        "PA13",
        "46",
        "PA14",
        "49",
        "PB3",
        "55",
        "NRST",
        "7",
        "BOOT0",
        "60",
        "VCAP1",
        "31",
        "VCAP2",
        "47",
    } <= symbol_text_by_definition["volt.examples.stm32_usb_buck:STM32F405RGTxCompact"]
    assert {
        "1",
        "VTref",
        "2",
        "SWDIO",
        "4",
        "SWCLK",
        "6",
        "SWO",
        "8",
        "BOOT0",
        "10",
        "NRST",
    } <= symbol_text_by_definition["volt.examples.stm32_usb_buck:CompactSWD10"]
    assert {
        "1",
        "VBUS",
        "2",
        "D-",
        "3",
        "D+",
        "4",
        "ID",
        "5",
        "GND",
    } <= symbol_text_by_definition["volt.examples.stm32_usb_buck:ReadableUSBMicroB"]
    svg_labels = set(re.findall(r">([^<>]+)</text>", first_svg_text))
    assert {"PA11", "44", "PA12", "45", "SWDIO", "10", "VBUS", "D+"} <= svg_labels
    assert not ({"PA11 44", "PA12 45", "2 SWDIO", "10 NRST"} & svg_labels)

    net_pin_counts = {
        net["id"]: len(net["pins"]) for net in logical["nets"] if len(net["pins"]) > 1
    }
    wire_or_port_net_ids = {
        *(wire["net"] for wire in schematic["wire_runs"]),
        *(port["net"] for port in schematic["power_ports"]),
        *(port["net"] for port in schematic["sheet_ports"]),
    }
    labelled_only_multi_pin_nets = {
        label["net"]
        for label in schematic["net_labels"]
        if label["net"] in net_pin_counts and label["net"] not in wire_or_port_net_ids
    }
    assert labelled_only_multi_pin_nets == set()

    stm32 = next(component for component in logical["components"] if component["reference"] == "U1")
    assert stm32["selected_physical_part"]["footprint"] == {
        "library": "Package_QFP",
        "name": "LQFP-64_10x10mm_P0.5mm",
    }

    component_definitions = {
        definition["id"]: definition for definition in logical["component_definitions"]
    }
    pin_definitions = {
        pin["id"]: pin for pin in logical["pin_definitions"]
    }
    stm32_definition = component_definitions[stm32["definition"]]
    stm32_pin_ids = set(stm32_definition["pins"])
    stm32_vdd_pin_ids = {
        pin_id
        for pin_id in stm32_pin_ids
        if pin_definitions[pin_id]["name"] == "VDD"
    }
    supply_net = next(net for net in logical["nets"] if net["name"] == "+3V3")
    supply_pin_refs = set(supply_net["pins"])
    pins_by_id = {pin["id"]: pin for pin in logical["pins"]}
    connected_stm32_vdd_pin_ids = {
        pins_by_id[pin_ref]["definition"]
        for pin_ref in supply_pin_refs
        if pins_by_id[pin_ref]["component"] == stm32["id"]
    }
    assert connected_stm32_vdd_pin_ids & stm32_vdd_pin_ids == stm32_vdd_pin_ids

    assert "design_intent" in logical
    assert len(logical["design_intent"]["stub_nets"]) >= 4
    assert len(logical["design_intent"]["no_connect_pins"]) >= 20

    result = main.run_project()
    schematic_report = result.schematic().validate()
    assert not schematic_report.has_errors
    assert {
        diagnostic.code for diagnostic in schematic_report
    } <= {"SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED"}
    readability_report = result.schematic().validate_readability()
    # This larger reference schematic still has known page-level readability warnings; make sure
    # the generic local signal-stub primitive stays compatible with endpoint readability checks.
    assert not readability_report.has_errors
    assert "SCHEMATIC_DANGLING_WIRE_ENDPOINT" not in {
        diagnostic.code for diagnostic in readability_report
    }
    assert {
        "SCHEMATIC_SYMBOL_OVERLAP",
        "SCHEMATIC_TEXT_COLLISION",
        "SCHEMATIC_TEXT_TOUCHES_SYMBOL",
        "SCHEMATIC_TEXT_TOUCHES_WIRE",
        "SCHEMATIC_VISUAL_COLLISION",
        "SCHEMATIC_WIRE_CROSSES_SYMBOL",
        "SCHEMATIC_TERMINAL_TOUCHES_SYMBOL",
        "SCHEMATIC_TERMINAL_TOUCHES_UNRELATED_WIRE",
        "SCHEMATIC_DIFFERENT_NET_WIRE_CROSSING",
    }.isdisjoint({diagnostic.code for diagnostic in readability_report})
    assert result.design().to_json() == main.build_board().design.to_json()

    assert "<svg xmlns=\"http://www.w3.org/2000/svg\"" in first_svg_text
    assert ".wire-run" in first_svg_text
    assert ".net-label" in first_svg_text
    assert "pin-anchor" not in first_svg_text
    assert "pin-label" not in first_svg_text
    assert 'class="layer layer-symbols"' in first_svg_text
    assert 'class="layer layer-wires"' in first_svg_text
    assert 'class="layer layer-labels"' in first_svg_text
    assert 'class="symbol-instance"' in first_svg_text
    assert 'class="wire-run"' in first_svg_text
    assert 'class="net-label"' in first_svg_text
    assert ">U1</text>" in first_svg_text
    assert ">+3V3</text>" in first_svg_text
    assert ">GND</text>" in first_svg_text
    assert "PWR/" not in first_svg_text
    assert "SUPPORT/" not in first_svg_text
    assert "USB/" not in first_svg_text
    assert "LED_STATUS/" not in first_svg_text
    assert "MCU_USB_" not in first_svg_text
    assert "STATUS_LED" not in first_svg_text
    assert "VCAP_" not in first_svg_text
    assert 'class="power-port power"' in first_svg_text
    assert 'class="power-port ground"' in first_svg_text
    assert 'class="sheet-port bidirectional"' in first_svg_text
    assert 'class="sheet-port off-page"' not in first_svg_text
    assert "data-net=\"net:" in first_svg_text
    assert 'class="title-block"' in first_svg_text
    assert 'class="coordinate-zones"' in first_svg_text
    assert 'class="sheet-region-frame dashed"' in first_svg_text
    assert 'class="schematic-body"' in first_body_svg_text
    assert 'class="title-block"' not in first_body_svg_text
    assert 'class="coordinate-zones"' not in first_body_svg_text
    assert 'viewBox="0 0 594 420"' not in first_body_svg_text
    assert '<text class="symbol-text"' in first_body_svg_text
    assert [path.name for path in artifacts.schematic_svg_pages] == ["stm32_usb_buck_STM32_USB_Buck.svg"]
    assert all('viewBox="0 0 594 420"' in text for text in first_page_texts)
    assert 'data-sheet="sheet:0"' in first_page_texts[0]
    assert 'data-sheet="sheet:1"' not in first_page_texts[0]

    codes = {diagnostic["code"] for diagnostic in validation["diagnostics"]}
    assert "POWER_INPUT_WITHOUT_SOURCE" in codes
    assert "SINGLE_PIN_NET" not in codes
    assert "UNCONNECTED_REQUIRED_PIN" not in codes
    assert validation["status"] == "expected-diagnostics"
    assert validation["unexpected"] == []
    assert validation["missing_expected"] == []
    assert validation["summary"]["errors"] == 2
    assert validation["summary"]["warnings"] == len(validation["diagnostics"]) - 2
    assert {item["code"] for item in validation["expected"]} == {
        "POWER_INPUT_WITHOUT_SOURCE",
        "SCHEMATIC_DENSE_PORT_TAGS",
        "SCHEMATIC_LABEL_CROWDS_SYMBOL",
        "SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED",
        "SCHEMATIC_SYMBOL_FIELD_FAR_FROM_SYMBOL",
        "SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW",
    }

    project_manifest = json.loads(first_project_texts["manifest.volt.json"])
    assert project_manifest["format"] == "volt.project_result"
    assert project_manifest["ok"] is True
    assert project_manifest["status"] == "expected-diagnostics"
    assert project_manifest["tests"]["summary"] == {"failed": 0, "passed": 1}


def test_stm32_usb_buck_example_rejects_schematic_artifacts_without_pin_coverage():
    main = importlib.import_module("examples.stm32_usb_buck.main")

    def author_invalid_power_region(region, board, _nets):
        component = board.components["VIN_SRC"]
        net = board.nets["+12V"]
        region.place(
            component,
            at=(12, 34),
            symbol=volt.SchematicSymbolSpec(
                "volt.test:ExternalSupply",
                pins=(volt.SchematicSymbolSpec.pin("OUT", 1, (44, 8), "Right"),),
                primitives=(volt.SchematicSymbolSpec.line((34, 8), (44, 8)),),
            ),
        )
        region.wire(net, ((0, 0), (10, 0)))
        region.label(net, at=(0, -2))

    original = main._author_power_region
    main._author_power_region = author_invalid_power_region
    try:
        with TemporaryDirectory() as temp_dir:
            output_dir = Path(temp_dir)
            try:
                main.write_artifacts(output_dir)
            except RuntimeError as error:
                assert "SCHEMATIC_PIN_NET_NOT_VISUALLY_COVERED" in str(error)
            else:
                raise AssertionError("invalid schematic readiness should fail artifact generation")

            assert not (output_dir / "stm32_usb_buck.volt.json").exists()
            assert not (output_dir / "stm32_usb_buck.volt.schematic.json").exists()
            assert not (output_dir / "stm32_usb_buck.svg").exists()
            assert not (output_dir / "stm32_usb_buck.body.svg").exists()
            assert not (output_dir / "stm32_usb_buck.pages").exists()
    finally:
        main._author_power_region = original


def test_stm32_usb_buck_project_stage_uses_shared_drawing_session_sugar():
    main = importlib.import_module("examples.stm32_usb_buck.main")

    composition_source = inspect.getsource(main.build_project)
    authoring_source = _schematic_authoring_source(main)
    source = composition_source + "\n" + authoring_source

    assert "_SchematicAuthor" not in source
    assert "fallback schematic pin coverage" not in source
    assert "clock_block(" not in source
    assert "reset_block(" not in source
    assert "boot_block(" not in source
    assert "power_block(" not in source
    assert "usb_block(" not in source
    assert ".region(" in composition_source
    assert "region.drawing(" in authoring_source
    assert "drawing.frame(" not in authoring_source
    assert re.search(r"\bat=\(\s*\d", authoring_source) is None
    assert "drawing.move_from(" in authoring_source
    assert "drawing.stack(" in authoring_source
    assert "drawing.two_terminal(" in authoring_source
    assert "drawing.C(" not in source
    assert "drawing.R(" not in source
    assert "drawing.LED(" not in source
    assert "drawing.connect(" in authoring_source
    assert "drawing.net_label(" in authoring_source
    assert "drawing.signal_tag(" in authoring_source
    assert "drawing.signal_stub(" not in authoring_source
    assert "drawing.no_connect(" in authoring_source
