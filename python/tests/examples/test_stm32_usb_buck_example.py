import importlib
import inspect
import json
import re
import zipfile
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
    assert [stage.name for stage in result.stages] == ["design", "schematic", "board"]
    assert result.design().name == "stm32_usb_buck"
    assert result.schematic().name == "STM32 USB Buck"
    assert result.board().name == "STM32 USB Buck PCB"
    assert not result.diagnostics.errors(stage="schematic")
    assert not [
        diagnostic for diagnostic in result.unexpected_diagnostics if diagnostic.stage == "board"
    ]
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


def _logical_pin_labels(logical):
    component_by_id = {
        component["id"]: component["reference"] for component in logical["components"]
    }
    pin_definition_by_id = {
        pin_definition["id"]: pin_definition["name"]
        for pin_definition in logical["pin_definitions"]
    }
    return {
        pin["id"]: f"{component_by_id[pin['component']]}.{pin_definition_by_id[pin['definition']]}"
        for pin in logical["pins"]
    }


def _logical_net_names(logical):
    return {net["id"]: net["name"] for net in logical["nets"]}


def _is_octilinear_track(track):
    points = track["points"]
    return all(
        start[0] == end[0]
        or start[1] == end[1]
        or abs(start[0] - end[0]) == abs(start[1] - end[1])
        for start, end in zip(points, points[1:])
    )


def _diagnostic_pin_labels(logical, diagnostics, code):
    pin_labels = _logical_pin_labels(logical)
    labels = set()
    for diagnostic in diagnostics:
        if diagnostic["code"] != code:
            continue
        pin_entity = next(
            entity for entity in diagnostic["entities"] if entity["kind"] == "pin"
        )
        labels.add(pin_labels[f"pin:{pin_entity['index']}"])
    return labels


def test_stm32_usb_buck_example_writes_stable_logical_artifacts():
    main = importlib.import_module("examples.stm32_usb_buck.main")
    stm32_board = importlib.import_module("examples.stm32_usb_buck.stm32_board")
    schematic_symbols = importlib.import_module("examples.stm32_usb_buck.schematic_symbols")
    expected_pcb_layer_svg_names = [
        "stm32_usb_buck.pcb.F_Cu.svg",
        "stm32_usb_buck.pcb.In1_Cu.svg",
        "stm32_usb_buck.pcb.In2_Cu.svg",
        "stm32_usb_buck.pcb.B_Cu.svg",
        "stm32_usb_buck.pcb.F_SilkS.svg",
    ]

    with TemporaryDirectory() as temp_dir:
        artifacts = main.write_artifacts(Path(temp_dir))

        logical = json.loads(artifacts.logical_json.read_text(encoding="utf-8"))
        schematic = json.loads(artifacts.schematic_json.read_text(encoding="utf-8"))
        pcb = json.loads(artifacts.pcb_json.read_text(encoding="utf-8"))
        validation = json.loads(artifacts.diagnostics_json.read_text(encoding="utf-8"))
        first_logical_text = artifacts.logical_json.read_text(encoding="utf-8")
        first_schematic_text = artifacts.schematic_json.read_text(encoding="utf-8")
        first_svg_text = artifacts.schematic_svg.read_text(encoding="utf-8")
        first_body_svg_text = artifacts.schematic_body_svg.read_text(encoding="utf-8")
        first_pcb_text = artifacts.pcb_json.read_text(encoding="utf-8")
        first_pcb_svg_text = artifacts.pcb_svg.read_text(encoding="utf-8")
        first_pcb_layer_svg_texts = {
            path.name: path.read_text(encoding="utf-8") for path in artifacts.pcb_layer_svgs
        }
        first_kicad_text = artifacts.kicad_pcb.read_text(encoding="utf-8")
        first_cpl_json_text = artifacts.cpl_json.read_text(encoding="utf-8")
        first_cpl_csv_text = artifacts.cpl_csv.read_text(encoding="utf-8")
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
        assert [path.name for path in repeated_artifacts.pcb_layer_svgs] == (
            expected_pcb_layer_svg_names
        )

        second_artifacts = main.write_artifacts(Path(temp_dir) / "second")
        assert second_artifacts.logical_json.read_text(encoding="utf-8") == first_logical_text
        assert second_artifacts.schematic_json.read_text(encoding="utf-8") == first_schematic_text
        assert second_artifacts.schematic_svg.read_text(encoding="utf-8") == first_svg_text
        assert (
            second_artifacts.schematic_body_svg.read_text(encoding="utf-8")
            == first_body_svg_text
        )
        assert second_artifacts.pcb_json.read_text(encoding="utf-8") == first_pcb_text
        assert second_artifacts.pcb_svg.read_text(encoding="utf-8") == first_pcb_svg_text
        assert {
            path.name: path.read_text(encoding="utf-8")
            for path in second_artifacts.pcb_layer_svgs
        } == first_pcb_layer_svg_texts
        assert second_artifacts.kicad_pcb.read_text(encoding="utf-8") == first_kicad_text
        assert second_artifacts.cpl_json.read_text(encoding="utf-8") == first_cpl_json_text
        assert second_artifacts.cpl_csv.read_text(encoding="utf-8") == first_cpl_csv_text
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
    assert all("selected_physical_part" in component for component in logical["components"])

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
    net_names_by_id = _logical_net_names(logical)
    pin_labels_by_id = _logical_pin_labels(logical)
    assert {
        net_names_by_id[net_id] for net_id in logical["design_intent"]["stub_nets"]
    } == set(stm32_board.STUB_NET_NAMES)
    assert {
        pin_labels_by_id[pin_id]
        for pin_id in logical["design_intent"]["no_connect_pins"]
    } == set(stm32_board.NO_CONNECT_PIN_LABELS)
    assert "USB/J1.ID" in stm32_board.NO_CONNECT_PIN_LABELS
    assert set(stm32_board.STM32_UNUSED_PIN_NO_CONNECTS) < set(
        stm32_board.NO_CONNECT_PIN_LABELS
    )
    assert all(
        label.startswith("U1.")
        for label in stm32_board.STM32_UNUSED_PIN_NO_CONNECTS
    )
    assert {
        pin_labels_by_id[marker["pin"]]
        for marker in schematic["no_connect_markers"]
    } == {"USB/J1.ID", "J2.NC", "J3.3"}

    result = main.run_project()
    schematic_report = result.schematic().validate()
    assert not schematic_report.has_errors
    assert {
        diagnostic.code for diagnostic in schematic_report
    } <= {"SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED"}
    assert {
        diagnostic.code for diagnostic in schematic_report
    } == {"SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED"}
    validation_no_connect_labels = _diagnostic_pin_labels(
        logical,
        validation["diagnostics"],
        "SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED",
    )
    assert validation_no_connect_labels == set(stm32_board.STM32_UNUSED_PIN_NO_CONNECTS)
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

    assert pcb["format"] == "volt.pcb"
    assert pcb["board"]["name"] == "STM32 USB Buck PCB"
    assert [layer["name"] for layer in pcb["board"]["layers"]] == [
        "F.Cu",
        "In1.Cu",
        "In2.Cu",
        "B.Cu",
        "F.SilkS",
    ]
    assert pcb["board"]["layer_stack"]["board_thickness_mm"] == 1.6
    assert pcb["board"]["outline"]["vertices"] == [
        [0, 0],
        [90, 0],
        [90, 58],
        [0, 58],
    ]
    assert pcb["board"]["rules"] == {
        "board_outline_clearance_mm": 0.25,
        "copper_clearance_mm": 0.127,
        "minimum_track_width_mm": 0.2,
        "minimum_via_annular_diameter_mm": 0.7,
        "minimum_via_drill_diameter_mm": 0.3,
        "package_assembly_clearance_mm": 0.25,
    }
    assert pcb["board"]["capability_profile"]["name"] == (
        "JLCPCB 4-layer FR-4 capability snapshot"
    )
    assert pcb["board"]["capability_profile"]["supported_copper_layer_counts"] == [4]
    placement_component_ids = {
        placement["component"] for placement in pcb["board"]["placements"]
    }
    assert placement_component_ids == component_ids
    assert len(pcb["board"]["placements"]) == len(logical["components"])
    assert len(pcb["board"]["tracks"]) >= 35
    assert len(pcb["board"]["vias"]) >= 8
    assert len(pcb["board"]["zones"]) >= 1
    assert len(pcb["board"]["texts"]) >= 10
    assert all(_is_octilinear_track(track) for track in pcb["board"]["tracks"])
    assert len(pcb["viewer"]["diagnostics"]) == 28
    assert {diagnostic["code"] for diagnostic in pcb["viewer"]["diagnostics"]} == {
        "PCB_NET_UNROUTED"
    }
    assert len(pcb["viewer"]["pad_resolutions"]) >= 90
    pcb_net_names = {
        net_names_by_id[track["net"]] for track in pcb["board"]["tracks"]
    } | {
        net_names_by_id[via["net"]] for via in pcb["board"]["vias"]
    } | {
        net_names_by_id[zone["net"]] for zone in pcb["board"]["zones"]
    }
    assert {
        "+12V",
        "+3V3",
        "VDDA",
        "GND",
        "PWR/IN_12V",
        "PWR/OUT_5V",
        "PWR/OUT_3V3",
        "USB/VBUS",
        "USB/USB_DP",
        "USB/USB_DM",
        "MCU_USB_DP",
        "MCU_USB_DM",
        "NRST",
        "BOOT0",
        "SWDIO",
        "SWCLK",
        "SWO",
        "USB/GND",
        "SUPPORT/GND",
        "SUPPORT/VCAP_1",
        "SUPPORT/VCAP_2",
        "LED_STATUS/SIGNAL",
    } <= pcb_net_names
    pcb_texts = {text["text"] for text in pcb["board"]["texts"]}
    assert {
        "STM32 USB BUCK",
        "12V IN",
        "USB",
        "SWD",
        "GPIO",
        "+3V3",
        "VDDA",
        "BOOT0",
        "RESET",
        "STATUS",
    } <= pcb_texts
    board_report = result.board().validate()
    assembly_report = result.board().validate_assembly()
    assert not board_report.has_errors
    assert not assembly_report.has_errors

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
    assert 'data-board-name="STM32 USB Buck PCB"' in first_pcb_svg_text
    assert 'class="pcb-track"' in first_pcb_svg_text
    assert 'class="pcb-via"' in first_pcb_svg_text
    assert 'class="pcb-zone fill-solid"' in first_pcb_svg_text
    assert 'data-placement="component_placement:' in first_pcb_svg_text
    assert [path.name for path in artifacts.pcb_layer_svgs] == expected_pcb_layer_svg_names
    for svg_name in expected_pcb_layer_svg_names:
        assert first_pcb_layer_svg_texts[svg_name].count('class="pcb-layer') == 1
    pcb_layer_tokens = {
        "stm32_usb_buck.pcb.F_Cu.svg": "F_Cu",
        "stm32_usb_buck.pcb.In1_Cu.svg": "In1_Cu",
        "stm32_usb_buck.pcb.In2_Cu.svg": "In2_Cu",
        "stm32_usb_buck.pcb.B_Cu.svg": "B_Cu",
        "stm32_usb_buck.pcb.F_SilkS.svg": "F_SilkS",
    }
    for svg_name, layer_token in pcb_layer_tokens.items():
        layer_text = first_pcb_layer_svg_texts[svg_name]
        assert f'id="pcb-layer-{layer_token}"' in layer_text
        for other_token in set(pcb_layer_tokens.values()) - {layer_token}:
            assert f'id="pcb-layer-{other_token}"' not in layer_text
    assert 'class="pcb-track"' in first_pcb_layer_svg_texts["stm32_usb_buck.pcb.F_Cu.svg"]
    assert (
        'class="pcb-zone fill-solid"'
        in first_pcb_layer_svg_texts["stm32_usb_buck.pcb.B_Cu.svg"]
    )
    assert 'data-text="board_text:' in first_pcb_layer_svg_texts[
        "stm32_usb_buck.pcb.F_SilkS.svg"
    ]
    assert first_kicad_text.startswith("(kicad_pcb\n")
    assert '(generator "Volt")' in first_kicad_text
    assert '(footprint "LQFP-64_10x10mm_P0.5mm"' in first_kicad_text
    assert '(zone\n    (net ' in first_kicad_text
    assert '(net_name "GND")' in first_kicad_text
    assert '(layer "B.Cu")' in first_kicad_text
    assert '(1 "In1.Cu" signal)' in first_kicad_text
    assert '(2 "In2.Cu" signal)' in first_kicad_text
    assert '(property "Reference" "C9"' in first_kicad_text
    assert json.loads(first_cpl_json_text)["format"] == "volt.cpl"
    assert first_cpl_csv_text.startswith("Designator,Mid X,Mid Y,Layer,Rotation\n")
    assert [path.name for path in artifacts.schematic_svg_pages] == ["stm32_usb_buck_STM32_USB_Buck.svg"]
    assert all('viewBox="0 0 594 420"' in text for text in first_page_texts)
    assert 'data-sheet="sheet:0"' in first_page_texts[0]
    assert 'data-sheet="sheet:1"' not in first_page_texts[0]

    codes = {diagnostic["code"] for diagnostic in validation["diagnostics"]}
    assert "POWER_INPUT_WITHOUT_SOURCE" not in codes
    assert "SINGLE_PIN_NET" not in codes
    assert "UNCONNECTED_REQUIRED_PIN" not in codes
    unrouted = [
        diagnostic
        for diagnostic in validation["diagnostics"]
        if diagnostic["code"] == "PCB_NET_UNROUTED"
    ]
    assert len(unrouted) == 28
    assert {
        diagnostic["expect_diagnostic_kwargs"]["stage"] for diagnostic in unrouted
    } == {"board"}
    assert {
        diagnostic["expect_diagnostic_kwargs"]["board"] for diagnostic in unrouted
    } == {"STM32 USB Buck PCB"}
    assert validation["status"] == "expected-diagnostics"
    assert validation["unexpected"] == []
    assert validation["missing_expected"] == []
    assert validation["summary"]["errors"] == 0
    assert validation["summary"]["warnings"] == len(validation["diagnostics"])
    assert {
        diagnostic["expect_diagnostic_kwargs"]["stage"]
        for diagnostic in validation["diagnostics"]
    } == {"board", "schematic"}
    assert {item["code"] for item in validation["expected"]} == {
        "PCB_NET_UNROUTED",
        "SCHEMATIC_DENSE_PORT_TAGS",
        "SCHEMATIC_LABEL_CROWDS_SYMBOL",
        "SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED",
        "SCHEMATIC_SYMBOL_FIELD_FAR_FROM_SYMBOL",
        "SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW",
    }

    project_manifest = json.loads(first_project_texts["manifest.volt.json"])
    project_tests = json.loads(first_project_texts["diagnostics/tests.json"])
    assert project_manifest["format"] == "volt.project_result"
    assert project_manifest["ok"] is True
    assert project_manifest["status"] == "expected-diagnostics"
    assert project_manifest["tests"]["summary"] == {"failed": 0, "passed": 5}
    assert "pcb/STM32-USB-Buck-PCB.volt.pcb.json" in first_project_texts
    assert "pcb/STM32-USB-Buck-PCB.svg" in first_project_texts
    assert "pcb/STM32-USB-Buck-PCB.kicad_pcb" in first_project_texts
    assert "pcb/STM32-USB-Buck-PCB.cpl.json" in first_project_texts
    assert "pcb/STM32-USB-Buck-PCB.cpl.csv" in first_project_texts
    assert project_tests["summary"] == {"failed": 0, "passed": 5}
    assert {
        (test["stage"], test["name"], test["ok"]) for test in project_tests["tests"]
    } == {
        ("design", "input_regulators_and_rails_are_preserved", True),
        ("design", "usb_debug_and_user_io_are_preserved", True),
        ("design", "power_and_signal_domains_stay_separate", True),
        ("schematic", "schematic_places_displayed_benchmark_parts", True),
        ("board", "pcb_places_and_routes_benchmark_parts", True),
    }


def test_stm32_usb_buck_example_writes_jlcpcb_manufacturing_package():
    main = importlib.import_module("examples.stm32_usb_buck.main")

    with TemporaryDirectory() as temp_dir:
        output = Path(temp_dir) / "stm32_usb_buck_jlcpcb_manufacturing"
        package = main.write_jlcpcb_manufacturing_package(output)

        assert package.status == "expected-diagnostics"
        assert package.output == output
        assert package.archive == output.with_suffix(".zip")
        assert package.archive.is_file()
        assert package.board == {
            "design": "stm32_usb_buck",
            "name": "STM32 USB Buck PCB",
            "output_name": "STM32 USB Buck PCB",
        }

        manifest = json.loads(
            output.joinpath("manufacturing", "manifest.json").read_text(encoding="utf-8")
        )
        assert manifest["format"] == "volt.manufacturing_package"
        assert manifest["schema_version"] == 1
        assert manifest["profile"]["config"] == main.jlcpcb_manufacturing_profile_metadata()
        assert manifest["profile"]["board"]["name"] == (
            "JLCPCB 4-layer FR-4 capability snapshot"
        )
        assert manifest["profile"]["board"]["supported_copper_layer_counts"] == [4]
        assert manifest["native_fabrication"]["coverage"] == {
            "classification": "complete",
            "fab_critical_loss": False,
        }
        assert not any(
            warning["fabrication_impact"] == "fab-critical"
            for warning in manifest["native_fabrication"]["warnings"]
        )

        native_files = {
            item["filename"]: output / item["path"]
            for item in manifest["native_fabrication"]["files"]
        }
        assert set(native_files) >= {
            "STM32_USB_Buck_PCB.GTL",
            "STM32_USB_Buck_PCB.G2",
            "STM32_USB_Buck_PCB.G3",
            "STM32_USB_Buck_PCB.GBL",
            "STM32_USB_Buck_PCB.GTO",
            "STM32_USB_Buck_PCB.GKO",
            "STM32_USB_Buck_PCB-PTH.TXT",
            "STM32_USB_Buck_PCB-NPTH.TXT",
        }
        assert "%TF.FileFunction,Copper,L1,Top*%" in native_files[
            "STM32_USB_Buck_PCB.GTL"
        ].read_text(encoding="utf-8")
        assert "%TF.FileFunction,Copper,L2,Inr*%" in native_files[
            "STM32_USB_Buck_PCB.G2"
        ].read_text(encoding="utf-8")
        assert "%TF.FileFunction,Copper,L3,Inr*%" in native_files[
            "STM32_USB_Buck_PCB.G3"
        ].read_text(encoding="utf-8")
        assert "%TF.FileFunction,Copper,L4,Bot*%" in native_files[
            "STM32_USB_Buck_PCB.GBL"
        ].read_text(encoding="utf-8")
        assert ";TYPE=PLATED" in native_files["STM32_USB_Buck_PCB-PTH.TXT"].read_text(
            encoding="utf-8"
        )
        assert ";TYPE=NON_PLATED" in native_files[
            "STM32_USB_Buck_PCB-NPTH.TXT"
        ].read_text(encoding="utf-8")

        artifact_paths = {item["kind"]: item["path"] for item in manifest["artifacts"]}
        assert artifact_paths["bom"] == "bom/bom.json"
        assert artifact_paths["bom_csv"] == "bom/bom.csv"
        assert artifact_paths["cpl"] == "pcb/STM32-USB-Buck-PCB.cpl.json"
        assert artifact_paths["cpl_csv"] == "pcb/STM32-USB-Buck-PCB.cpl.csv"
        assert output.joinpath("manufacturing", "inspection.html").is_file()

        with zipfile.ZipFile(package.archive) as archive:
            names = set(archive.namelist())
        assert "manufacturing/manifest.json" in names
        assert "manufacturing/fabrication/gerber/STM32_USB_Buck_PCB.G2" in names
        assert "manufacturing/fabrication/drill/STM32_USB_Buck_PCB-PTH.TXT" in names


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
