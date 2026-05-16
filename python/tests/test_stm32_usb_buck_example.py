import importlib
import inspect
import json
import sys
from collections import Counter
from pathlib import Path
from tempfile import TemporaryDirectory

import volt


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT))


def test_stm32_usb_buck_example_writes_stable_logical_artifacts():
    main = importlib.import_module("examples.stm32_usb_buck.main")
    schematic_output = importlib.import_module("examples.stm32_usb_buck.schematic_output")

    with TemporaryDirectory() as temp_dir:
        artifacts = main.write_artifacts(Path(temp_dir))

        logical = json.loads(artifacts.logical_json.read_text(encoding="utf-8"))
        schematic = json.loads(artifacts.schematic_json.read_text(encoding="utf-8"))
        validation = json.loads(artifacts.validation_report.read_text(encoding="utf-8"))
        first_logical_text = artifacts.logical_json.read_text(encoding="utf-8")
        first_schematic_text = artifacts.schematic_json.read_text(encoding="utf-8")
        first_svg_text = artifacts.schematic_svg.read_text(encoding="utf-8")
        first_validation_text = artifacts.validation_report.read_text(encoding="utf-8")

        second_artifacts = main.write_artifacts(Path(temp_dir) / "second")
        assert second_artifacts.logical_json.read_text(encoding="utf-8") == first_logical_text
        assert second_artifacts.schematic_json.read_text(encoding="utf-8") == first_schematic_text
        assert second_artifacts.schematic_svg.read_text(encoding="utf-8") == first_svg_text
        assert (
            second_artifacts.validation_report.read_text(encoding="utf-8")
            == first_validation_text
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
    assert [sheet["name"] for sheet in schematic["sheets"]] == [
        "Power",
        "MCU",
        "USB and Connectors",
    ]
    assert len(schematic["symbol_instances"]) >= 10
    assert len(schematic["wire_runs"]) >= 20
    assert len(schematic["net_labels"]) <= 36
    assert len(schematic["power_ports"]) >= 20
    assert len(schematic["sheet_ports"]) >= 10
    assert len(schematic["no_connect_markers"]) >= 20
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
        "volt.benchmarks.stm32_usb_buck:Capacitor",
        "volt.benchmarks.stm32_usb_buck:USB_B_Micro",
    }
    assert ".to_json(" not in inspect.getsource(schematic_output.build_schematic)
    label_counts = Counter(
        (label["sheet"], label["net"]) for label in schematic["net_labels"]
    )
    assert label_counts
    assert max(label_counts.values()) <= 4

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
    connected_stm32_vdd_pin_ids = {
        logical["pins"][int(pin_ref.removeprefix("pin:"))]["definition"]
        for pin_ref in supply_pin_refs
        if logical["pins"][int(pin_ref.removeprefix("pin:"))]["component"] == stm32["id"]
    }
    assert connected_stm32_vdd_pin_ids & stm32_vdd_pin_ids == stm32_vdd_pin_ids

    assert "design_intent" in logical
    assert len(logical["design_intent"]["stub_nets"]) >= 4
    assert len(logical["design_intent"]["no_connect_pins"]) >= 20

    schematic_report = main.build_schematic(main.build_board()).validate()
    assert list(schematic_report) == []

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
    assert 'class="power-port power"' in first_svg_text
    assert 'class="power-port ground"' in first_svg_text
    assert 'class="sheet-port off-page"' in first_svg_text
    assert 'class="no-connect-marker"' in first_svg_text
    assert "data-net=\"net:" in first_svg_text

    codes = {diagnostic["code"] for diagnostic in validation["diagnostics"]}
    assert "POWER_INPUT_WITHOUT_SOURCE" in codes
    assert "SINGLE_PIN_NET" not in codes
    assert "UNCONNECTED_REQUIRED_PIN" not in codes
    assert validation["summary"]["errors"] == len(validation["diagnostics"])


def test_stm32_usb_buck_example_rejects_schematic_artifacts_without_pin_coverage():
    main = importlib.import_module("examples.stm32_usb_buck.main")

    def build_invalid_schematic(board):
        schematic = board.design.schematic("Main")
        component = board.components["VIN_SRC"]
        net = board.nets["+12V"]
        schematic.place(
            component,
            at=(12, 34),
            symbol=volt.SchematicSymbolSpec(
                "volt.test:ExternalSupply",
                pins=(volt.SchematicSymbolSpec.pin("OUT", 1, (44, 8), "Right"),),
                primitives=(volt.SchematicSymbolSpec.line((34, 8), (44, 8)),),
            ),
        )
        schematic.wire(net, ((0, 0), (10, 0)))
        schematic.label(net, at=(0, -2))
        return schematic

    original = main.build_schematic
    main.build_schematic = build_invalid_schematic
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
    finally:
        main.build_schematic = original


def test_stm32_usb_buck_schematic_author_fails_on_fallback_connected_pin_coverage():
    board_module = importlib.import_module("examples.stm32_usb_buck.stm32_board")
    schematic_output = importlib.import_module("examples.stm32_usb_buck.schematic_output")

    board = board_module.build_board()
    sheet = board.design.schematic("Coverage audit")
    placed = sheet.place(
        board.components["VIN_SRC"],
        at=(12, 34),
        symbol=schematic_output._external_supply_symbol(),
    )
    net_by_pin = {
        pin.index: net
        for net in board.design.nets()
        for pin in net.pins()
    }
    author = schematic_output._SchematicAuthor(net_by_pin, set())

    try:
        author.audit_no_fallback_pin_coverage([(sheet, placed)])
    except RuntimeError as error:
        assert "fallback schematic pin coverage" in str(error)
        assert "VIN_SRC.OUT" in str(error)
        assert "+12V" in str(error)
    else:
        raise AssertionError("connected pins must not be silently covered by fallback")


if __name__ == "__main__":
    test_stm32_usb_buck_example_writes_stable_logical_artifacts()
    test_stm32_usb_buck_example_rejects_schematic_artifacts_without_pin_coverage()
    test_stm32_usb_buck_schematic_author_fails_on_fallback_connected_pin_coverage()
