import json

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


def test_custom_component_definitions_are_kernel_owned():
    design = volt.Design("custom")

    opamp = design.define_component(
        "OpAmp",
        pins=[
            volt.PinSpec("OUT", 1, role="output"),
            volt.PinSpec("IN-", 2, role="input"),
            volt.PinSpec("IN+", 3, role="input"),
            volt.PinSpec("V-", 4, role="power"),
            volt.PinSpec("V+", 8, role="power"),
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
    assert pin_definitions["OUT"]["role"] == "DigitalOutput"
    assert pin_definitions["V+"]["role"] == "PowerInput"
    assert len(circuit["pins"]) == 5
    assert circuit["nets"][0]["pins"] == ["pin:0"]

    report = design.validate()
    assert report.has_errors
    assert {diagnostic.code for diagnostic in report} == {"UNCONNECTED_REQUIRED_PIN", "SINGLE_PIN_NET"}


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


def test_component_selected_part_serializes():
    design = volt.Design("selected-part")
    r1 = design.R(resistance=330, tolerance=0.01, ref="R1")

    r1.select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-07330RL",
        package="0603",
        footprint=("Resistor_SMD", "R_0603_1608Metric"),
        pin_pads={
            1: "1",
            2: "2",
        },
        properties={
            "supplier": "Digi-Key",
        },
        voltage_rating=75,
        power_rating=0.1,
    )

    circuit = json.loads(design.to_json())
    resistor = next(
        component for component in circuit["components"] if component["reference"] == "R1"
    )
    part = resistor["selected_physical_part"]

    assert part["manufacturer_part"] == {
        "manufacturer": "Yageo",
        "part_number": "RC0603FR-07330RL",
    }
    assert part["package"] == "0603"
    assert part["footprint"] == {
        "library": "Resistor_SMD",
        "name": "R_0603_1608Metric",
    }
    assert part["pin_pad_mappings"] == [
        {"pin": "pin_def:0", "pad": "1"},
        {"pin": "pin_def:1", "pad": "2"},
    ]
    assert part["properties"]["supplier"] == {"type": "string", "value": "Digi-Key"}
    assert part["electrical_attributes"]["voltage_rating"] == {
        "type": "quantity",
        "dimension": "voltage",
        "value": 75.0,
    }
    assert part["electrical_attributes"]["power_rating"] == {
        "type": "quantity",
        "dimension": "power",
        "value": 0.1,
    }


def test_custom_component_selected_part_accepts_named_pin_mappings():
    design = volt.Design("selected-custom")
    opamp = design.define_component(
        "OpAmp",
        pins=[
            volt.PinSpec("OUT", 1, role="output"),
            volt.PinSpec("IN-", 2, role="input"),
            volt.PinSpec("IN+", 3, role="input"),
            volt.PinSpec("V-", 4, role="power"),
            volt.PinSpec("V+", 8, role="power"),
        ],
    )
    u1 = design.instantiate(opamp, ref="U1")

    u1.select_part(
        manufacturer="Texas Instruments",
        part_number="TLV9002IDR",
        package="SOIC-8",
        footprint=("Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"),
        pin_pads={
            "OUT": "1",
            "IN-": "2",
            "IN+": "3",
            "V-": "4",
            "V+": "8",
        },
        voltage_rating=5.5,
    )

    circuit = json.loads(design.to_json())
    part = circuit["components"][0]["selected_physical_part"]

    assert part["manufacturer_part"]["manufacturer"] == "Texas Instruments"
    assert part["manufacturer_part"]["part_number"] == "TLV9002IDR"
    assert part["pin_pad_mappings"] == [
        {"pin": "pin_def:0", "pad": "1"},
        {"pin": "pin_def:1", "pad": "2"},
        {"pin": "pin_def:2", "pad": "3"},
        {"pin": "pin_def:3", "pad": "4"},
        {"pin": "pin_def:4", "pad": "8"},
    ]
    assert part["electrical_attributes"]["voltage_rating"] == {
        "type": "quantity",
        "dimension": "voltage",
        "value": 5.5,
    }


def test_selected_part_mapping_errors_are_rejected():
    design = volt.Design("bad-part")
    r1 = design.R(ref="R1")

    try:
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=("Resistor_SMD", "R_0603_1608Metric"),
            pin_pads={1: "1"},
        )
    except RuntimeError:
        pass
    else:
        raise AssertionError("missing pin mapping should be rejected")

    try:
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=("Resistor_SMD", "R_0603_1608Metric"),
            pin_pads={1: "1", 2: "1"},
        )
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate pad mapping should be rejected")

    try:
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=("Resistor_SMD", "R_0603_1608Metric"),
            pin_pads={1: "1", "1": "2"},
        )
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate logical pin mapping should be rejected")

    try:
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=("Resistor_SMD", "R_0603_1608Metric"),
            pin_pads={1: "1", "BOGUS": "2"},
        )
    except IndexError:
        pass
    else:
        raise AssertionError("unknown pin mapping should be rejected")


def test_invalid_selected_part_rating_does_not_select_part():
    design = volt.Design("bad-rating")

    try:
        design.C(ref="C1", voltage_rating=float("inf"))
    except ValueError:
        pass
    else:
        raise AssertionError("non-finite capacitor voltage rating should be rejected")

    capacitor = json.loads(design.to_json())["components"][0]
    assert capacitor["reference"] == "C1"
    assert "selected_physical_part" not in capacitor

    r1 = design.R(ref="R1")

    try:
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=("Resistor_SMD", "R_0603_1608Metric"),
            pin_pads={1: "1", 2: "2"},
            voltage_rating=float("inf"),
        )
    except ValueError:
        pass
    else:
        raise AssertionError("non-finite selected-part rating should be rejected")

    circuit = json.loads(design.to_json())
    assert "selected_physical_part" not in circuit["components"][0]


def test_voltage_rating_diagnostic_is_inspectable():
    design = volt.Design("rating")
    vdd = design.net("VDD", kind="power", voltage=5.0)
    c1 = design.C(capacitance=100e-9, ref="C1")
    c1.select_part(
        manufacturer="Example",
        part_number="LOW-VOLTAGE-CAP",
        package="0603",
        footprint=("Capacitor_SMD", "C_0603"),
        pin_pads={1: "1", 2: "2"},
        voltage_rating=3.3,
    )

    vdd += c1[1]

    report = design.validate()

    assert report.has_errors
    assert "SELECTED_PART_VOLTAGE_RATING_EXCEEDED" in {
        diagnostic.code for diagnostic in report
    }


def test_power_pin_semantics_drive_diagnostics():
    design = volt.Design("typed-power")
    load = design.define_component(
        "Load",
        pins=[
            volt.PinSpec(
                "VCC",
                1,
                role="power",
                terminal="power",
                direction="input",
                voltage_range=(3.0, 3.6),
            ),
            volt.PinSpec("GND", 2, role="ground", terminal="ground", direction="passive"),
        ],
    )
    u1 = design.instantiate(load, ref="U1")

    vcc = design.net("VCC", kind="power", voltage=3.3)
    vcc += u1["VCC"]
    gnd = design.net("GND", kind="ground")
    gnd += u1["GND"]

    report = design.validate()

    assert "POWER_INPUT_WITHOUT_SOURCE" in {diagnostic.code for diagnostic in report}


def test_diagnostics_are_inspectable():
    design = volt.Design("incomplete")
    design.R("10k", ref="R1")

    report = design.validate()
    assert report.has_errors
    assert len(report) == 2
    assert {diagnostic.code for diagnostic in report} == {"UNCONNECTED_REQUIRED_PIN"}
    assert all(diagnostic.severity == "error" for diagnostic in report)
    assert all(diagnostic.entities for diagnostic in report)


if __name__ == "__main__":
    test_led_circuit_validates()
    test_natural_electrical_values_serialize_as_kernel_attributes()
    test_custom_component_definitions_are_kernel_owned()
    test_pin_spec_electrical_semantics_are_kernel_owned()
    test_component_selected_part_serializes()
    test_custom_component_selected_part_accepts_named_pin_mappings()
    test_selected_part_mapping_errors_are_rejected()
    test_invalid_selected_part_rating_does_not_select_part()
    test_voltage_rating_diagnostic_is_inspectable()
    test_power_pin_semantics_drive_diagnostics()
    test_diagnostics_are_inspectable()
