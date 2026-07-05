import json

import pytest

import volt


def test_led_circuit_validates():
    design = volt.Design("led")

    vcc = design.net("VCC", kind="power")
    led_a = design.net("LED_A")
    gnd = design.net("GND", kind="ground")

    j1 = design.connector_1x02(ref="J1")
    r1 = design.R("330 ohm", ref="R1")
    d1 = design.LED(ref="D1")

    vcc += j1[1], r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"], j1[2]

    report = design.validate()
    assert len(report) == 0
    assert not report.has_errors

    json_text = design.to_json()
    assert '"name": "VCC"' in json_text
    assert '"reference": "R1"' in json_text

def test_design_returns_component_handles_by_reference():
    design = volt.Design("component-lookup")
    r1 = design.R("330", ref="R1")
    d1 = design.LED(ref="D1")

    assert design.component("R1").index == r1.index
    assert design.component("D1").index == d1.index
    assert [component.reference for component in design.components()] == ["R1", "D1"]
    assert r1[1].component.index == r1.index
    assert r1[1].component_reference == "R1"
    assert r1[1].name == "1"
    assert r1[1].number == "1"

    with pytest.raises(KeyError, match="R404"):
        design.component("R404")

def test_python_kernel_error_translator_exposes_typed_structural_failures():
    assert volt.VoltError is volt._volt.VoltError
    assert issubclass(volt.UnknownEntityError, volt.VoltError)
    assert issubclass(volt.DuplicateNameError, volt.VoltError)
    assert issubclass(volt.CrossReferenceError, volt.VoltError)
    assert issubclass(volt.InvalidArgumentError, volt.VoltError)
    assert issubclass(volt.InvalidStateError, volt.VoltError)

    circuit = volt._volt.Circuit()
    sig = circuit.add_net("SIG")
    with pytest.raises(volt.DuplicateNameError, match="^Net name already exists$") as duplicate:
        circuit.add_net("SIG")
    assert str(duplicate.value) == "Net name already exists"
    assert duplicate.value.code == "DuplicateName"
    assert duplicate.value.entity is None
    assert isinstance(duplicate.value, RuntimeError)

    with pytest.raises(
        volt.UnknownEntityError, match="^Volt entity id is out of range$"
    ) as unknown:
        circuit.net_pins(99)
    assert str(unknown.value) == "Volt entity id is out of range"
    assert unknown.value.code == "UnknownEntity"
    assert unknown.value.entity is None
    assert isinstance(unknown.value, IndexError)

    definition = circuit.define_resistor()
    component = circuit.instantiate_ref(definition, "R1", {})
    pin = circuit.pin_by_number(component, "1")
    alt = circuit.add_net("ALT")
    circuit.connect(sig, pin)
    with pytest.raises(
        volt.InvalidStateError, match="^Pin is already connected to another net$"
    ) as invalid_state:
        circuit.connect(alt, pin)
    assert str(invalid_state.value) == "Pin is already connected to another net"
    assert invalid_state.value.code == "InvalidState"
    assert invalid_state.value.entity is None
    assert isinstance(invalid_state.value, RuntimeError)

    design = volt.Design("cross-reference-error")
    board = design.board()
    silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
    net = design.net("ZONE")
    with pytest.raises(
        volt.CrossReferenceError, match="^Board copper zones require copper layers$"
    ) as cross_reference:
        board.add_zone(
            outline=((0.0, 0.0), (2.0, 0.0), (2.0, 2.0), (0.0, 2.0)),
            layers=(silk,),
            net=net,
        )
    assert str(cross_reference.value) == "Board copper zones require copper layers"
    assert cross_reference.value.code == "CrossReferenceViolation"
    assert cross_reference.value.entity == {"kind": "board_layer", "index": silk}
    assert isinstance(cross_reference.value, RuntimeError)

    with pytest.raises(ValueError, match="^Net name must not be empty$") as invalid_argument:
        design.net("")
    assert str(invalid_argument.value) == "Net name must not be empty"
    assert invalid_argument.value.code == "InvalidArgument"
    assert invalid_argument.value.entity is None
    assert isinstance(invalid_argument.value, volt.InvalidArgumentError)
    assert isinstance(invalid_argument.value, volt.VoltError)
    assert isinstance(invalid_argument.value, RuntimeError)

def test_natural_electrical_values_serialize_as_kernel_attributes():
    design = volt.Design("typed")

    design.R(resistance=330, tolerance=0.01, ref="R1")
    design.C(capacitance=100e-9, voltage_rating=16, ref="C1")
    design.net("VDD", kind="power", voltage=3.3)

    circuit = json.loads(design.to_json())
    resistor = next(
        component for component in circuit["components"] if component["reference"] == "R1"
    )
    capacitor = next(
        component for component in circuit["components"] if component["reference"] == "C1"
    )
    vdd = next(net for net in circuit["nets"] if net["name"] == "VDD")

    assert resistor["electrical_attributes"]["resistance"] == {
        "type": "quantity",
        "dimension": "resistance",
        "value": 330.0,
    }
    assert resistor["electrical_attributes"]["tolerance"] == {
        "type": "tolerance",
        "mode": "percent",
        "dimension": "ratio",
        "minus": 0.01,
        "plus": 0.01,
    }
    assert capacitor["electrical_attributes"]["capacitance"] == {
        "type": "quantity",
        "dimension": "capacitance",
        "value": 100e-9,
    }
    assert capacitor["selected_physical_part"]["electrical_attributes"]["voltage_rating"] == {
        "type": "quantity",
        "dimension": "voltage",
        "value": 16.0,
    }
    assert vdd["electrical_attributes"]["voltage"] == {
        "type": "quantity",
        "dimension": "voltage",
        "value": 3.3,
    }

def test_net_class_ipc_current_sizing_is_kernel_owned_and_serialized():
    design = volt.Design("net-class")
    vdd = design.net("VDD", kind="power")

    power = design.net_class("Power", current=1.0, temp_rise=10.0, copper_weight=1.0)
    power.assign(vdd)

    info = power.info()
    assert info["track_width_mm"] == pytest.approx(0.3003762222)
    assert info["derived_track_width"]["calculator"]["id"] == "ipc-2221.trace-width.current"

    circuit = json.loads(design.to_json())
    rule = circuit["net_classes"]["classes"][0]
    assert "track_width_mm" not in rule
    assert rule["derived_track_width"]["value_mm"] == pytest.approx(0.3003762222)
    assert rule["derived_track_width"]["inputs"][0] == {
        "name": "current",
        "value": 1.0,
        "unit": "A",
    }
    assert circuit["net_classes"]["net_assignments"] == [
        {"net": "net:0", "net_class": "net_class:0"}
    ]

def test_custom_component_definitions_are_kernel_owned():
    design = volt.Design("custom")

    opamp = design.define_component(
        "OpAmp",
        pins=[
            volt.PinSpec("OUT", 1, role="output"),
            volt.PinSpec("IN-", 2, role="input"),
            volt.PinSpec("IN+", 3, role="input"),
            volt.PinSpec("V-", 4, role="power_input"),
            volt.PinSpec("V+", 8, role="power_input"),
        ],
        properties={"category": "analog"},
    )
    u1 = design.instantiate(opamp, ref="U1")

    vout = design.net("VOUT")
    vout += u1["OUT"]

    circuit = json.loads(design.to_json())
    definition = circuit["component_definitions"][0]
    component = circuit["components"][0]
    pin_definitions = {pin["name"]: pin for pin in circuit["pin_definitions"]}

    assert definition["name"] == "OpAmp"
    assert definition["properties"]["category"] == {"type": "string", "value": "analog"}
    assert component["reference"] == "U1"
    assert "role" not in pin_definitions["OUT"]
    assert pin_definitions["OUT"]["terminal_kind"] == "Signal"
    assert pin_definitions["OUT"]["direction"] == "Output"
    assert pin_definitions["OUT"]["signal_domain"] == "Digital"
    assert pin_definitions["V+"]["terminal_kind"] == "Power"
    assert pin_definitions["V+"]["direction"] == "Input"
    assert len(circuit["pins"]) == 5
    assert circuit["nets"][0]["pins"] == ["pin:0"]

    report = design.validate()
    assert report.has_errors
    assert {diagnostic.code for diagnostic in report} == {"UNCONNECTED_REQUIRED_PIN", "SINGLE_PIN_NET"}

def test_module_authoring_serializes_kernel_owned_contents():
    design = volt.Design("module-authoring")
    resistor = design.define_component(
        "Resistor",
        pins=[
            volt.PinSpec("1", 1),
            volt.PinSpec("2", 2),
        ],
    )

    divider = design.define_module("Divider")
    vin = divider.port("VIN", kind="power", role="power_input")
    out = divider.port("OUT")
    r1 = divider.instantiate(resistor, ref="R1")
    vin += r1[1]
    out += r1[2]

    vbat = design.net("VBAT", kind="power", voltage=12.0)
    sense = design.net("SENSE")
    div_a = design.instantiate(divider, ref="DIV_A")
    vbat += div_a["VIN"]
    sense += div_a["OUT"]

    circuit = json.loads(design.to_json())

    module = circuit["module_definitions"][0]
    assert module["name"] == "Divider"
    assert module["components"] == [
        {
            "id": "module_component:0",
            "definition": "component_def:0",
            "reference": "R1",
            "properties": {},
        }
    ]
    assert module["connections"] == [
        {
            "net": "template_net:0",
            "component": "module_component:0",
            "pin": "pin_def:0",
        },
        {
            "net": "template_net:1",
            "component": "module_component:0",
            "pin": "pin_def:1",
        },
    ]

    instance = circuit["module_instances"][0]
    assert instance["name"] == "DIV_A"
    assert instance["component_origins"] == [
        {"template_component": "module_component:0", "component": "component:0"}
    ]
    assert instance["port_bindings"] == [
        {"port": "port:0", "parent_net": "net:0"},
        {"port": "port:1", "parent_net": "net:1"},
    ]

    component = div_a.component("R1")
    assert circuit["components"][component.index]["reference"] == "DIV_A/R1"

def test_module_authoring_exposes_hierarchy_inspection_views():
    design = volt.Design("module-inspection")
    resistor = design.define_component(
        "Resistor",
        pins=[
            volt.PinSpec("1", 1),
            volt.PinSpec("2", 2),
        ],
    )

    divider = design.define_module("Divider")
    vin = divider.port("VIN", kind="power", role="power_input")
    out = divider.port("OUT")
    r1 = divider.instantiate(resistor, ref="R1")
    vin += r1[1]
    out += r1[2]

    parent_vin = design.net("VIN", kind="power")
    parent_out = design.net("OUT")
    div_a = design.instantiate(divider, ref="DIV_A")
    parent_vin += div_a["VIN"]
    parent_out += div_a["OUT"]

    assert divider.template_nets() == (
        volt.TemplateNetInfo(index=0, name="VIN", kind="Power"),
        volt.TemplateNetInfo(index=1, name="OUT", kind="Signal"),
    )
    assert divider.ports() == (
        volt.PortInfo(
            index=0, name="VIN", internal_net=0, role="PowerInput", required=True
        ),
        volt.PortInfo(index=1, name="OUT", internal_net=1, role="Passive", required=True),
    )
    assert divider.components() == (
        volt.ModuleComponentInfo(index=0, definition=0, reference="R1"),
    )
    assert divider.connections() == (
        volt.ModuleConnectionInfo(net=0, component=0, pin_definition=0),
        volt.ModuleConnectionInfo(net=1, component=0, pin_definition=1),
    )
    assert div_a.net_origins() == (
        volt.ModuleNetOriginInfo(template_net=0, net=2),
        volt.ModuleNetOriginInfo(template_net=1, net=3),
    )
    assert div_a.component_origins() == (
        volt.ModuleComponentOriginInfo(module_component=0, component=0),
    )
    assert div_a.port_bindings() == (
        volt.PortBindingInfo(port=0, internal_net=2, parent_net=0),
        volt.PortBindingInfo(port=1, internal_net=3, parent_net=1),
    )
