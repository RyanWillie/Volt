"""Author the seven supported linear-DC corpus cases for opt-in integration tests.

The component values, references, source orientations, and case names intentionally mirror
``tests/fixtures/linear_dc/corpus.json`` and ``tests/io/electrical/linear_dc_test.cpp``.
Analytical values and tolerances remain owned by the JSON corpus.
"""

from __future__ import annotations

import volt


CASE_IDS = (
    "divider",
    "series_parallel_load",
    "source_polarity",
    "zero_ohm_constraint",
    "ideal_capacitor_dc",
    "ideal_inductor_dc",
    "composite_capacitor_dc",
)


def _build_library() -> tuple[volt.Library, dict[str, volt.Part]]:
    library = volt.Library("volt.tests.linear_dc", version="1")
    component = library.component(
        "two-terminal",
        pins=(volt.PinSpec("A", 1), volt.PinSpec("B", 2)),
        contract=volt.ComponentContract("volt.tests.linear_dc/two-terminal@1", ("A", "B")),
    )
    footprint = volt.Footprint(
        (library.namespace, "test-two-terminal"),
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-0.5, 0), size=(0.5, 0.5)),
            volt.FootprintPad.surface_mount("2", at=(0.5, 0), size=(0.5, 0.5)),
        ),
    )

    def fields(name: str) -> dict[str, object]:
        return {
            "component": component,
            "footprint": footprint,
            "pads": {"A": "1", "B": "2"},
            "manufacturer": "Volt test fixtures",
            "mpn": name,
            "package": "TEST-2",
            "prefix": name[0].upper(),
        }

    def single_element(name, element_type, key, parameter):
        builder = volt.PartElectricalModelBuilder(component)
        a, b = builder.terminal("a", "A"), builder.terminal("b", "B")
        builder.add(element_type, key, a, b, parameter)
        return library.part(name, **fields(name), electrical_model=builder.build())

    parts = {
        "resistor": single_element(
            "resistor", volt.ResistanceElement, "body", volt.ModelParameter(volt.ohms(1_000))
        ),
        "resistor-2k": single_element(
            "resistor-2k",
            volt.ResistanceElement,
            "body",
            volt.ModelParameter(volt.ohms(2_000)),
        ),
        "zero-resistor": single_element(
            "zero-resistor",
            volt.ResistanceElement,
            "body",
            volt.ModelParameter(volt.ohms(0), volt.Tolerance.percent(0)),
        ),
        "ideal-capacitor": single_element(
            "ideal-capacitor",
            volt.CapacitanceElement,
            "storage",
            volt.ModelParameter(volt.farads(1e-6)),
        ),
        "ideal-inductor": single_element(
            "ideal-inductor",
            volt.InductanceElement,
            "storage",
            volt.ModelParameter(volt.henries(1e-3)),
        ),
    }

    composite = volt.PartElectricalModelBuilder(component)
    a, b = composite.terminal("a", "A"), composite.terminal("b", "B")
    after_esr = composite.internal_node("after_esr")
    after_esl = composite.internal_node("after_esl")
    composite.add(
        volt.ResistanceElement,
        "esr",
        a,
        after_esr,
        volt.ModelParameter(volt.ohms(0.08)),
    )
    composite.add(
        volt.InductanceElement,
        "esl",
        after_esr,
        after_esl,
        volt.ModelParameter(volt.henries(1e-9)),
    )
    composite.add(
        volt.CapacitanceElement,
        "storage",
        after_esl,
        b,
        volt.ModelParameter(volt.farads(10e-6)),
    )
    parts["composite-capacitor"] = library.part(
        "composite-capacitor",
        **fields("composite-capacitor"),
        electrical_model=composite.build(),
    )
    return library, parts


LIBRARY, PARTS = _build_library()


def _instance(design: volt.Design, part: str, reference: str, from_, to):
    component = design.instantiate(PARTS[part], ref=reference).dnp(False)
    from_ += component["A"]
    to += component["B"]
    return component


def _divider() -> volt.Design:
    design = volt.Design("divider")
    supply, midpoint, reference = (
        design.net("supply"),
        design.net("midpoint"),
        design.net("reference"),
    )
    _instance(design, "resistor", "R1", supply, midpoint)
    _instance(design, "resistor", "R2", midpoint, reference)
    return design


def _series_parallel_load() -> volt.Design:
    design = volt.Design("series_parallel_load")
    supply, load, reference = (
        design.net("supply"),
        design.net("load"),
        design.net("reference"),
    )
    _instance(design, "resistor", "Rseries", supply, load)
    _instance(design, "resistor", "Rload1", load, reference)
    _instance(design, "resistor-2k", "Rload2", load, reference)
    return design


def _source_polarity() -> volt.Design:
    design = volt.Design("source_polarity")
    reference, positive, negative, current_driven = (
        design.net("reference"),
        design.net("positive"),
        design.net("negative"),
        design.net("current-driven"),
    )
    _instance(design, "resistor", "Rpositive", positive, reference)
    _instance(design, "resistor", "Rnegative", reference, negative)
    _instance(design, "resistor", "Rcurrent", current_driven, reference)
    return design


def _zero_ohm_constraint() -> volt.Design:
    design = volt.Design("zero_ohm_constraint")
    supply, load, reference = (
        design.net("supply"),
        design.net("load"),
        design.net("reference"),
    )
    _instance(design, "zero-resistor", "Rwire", supply, load)
    _instance(design, "resistor", "Rload", load, reference)
    return design


def _ideal_capacitor_dc() -> volt.Design:
    design = volt.Design("ideal_capacitor_dc")
    supply, downstream, reference = (
        design.net("supply"),
        design.net("downstream"),
        design.net("reference"),
    )
    _instance(design, "ideal-capacitor", "Cstorage", supply, downstream)
    _instance(design, "resistor", "Rreference", downstream, reference)
    return design


def _ideal_inductor_dc() -> volt.Design:
    design = volt.Design("ideal_inductor_dc")
    supply, load, reference = (
        design.net("supply"),
        design.net("load"),
        design.net("reference"),
    )
    _instance(design, "ideal-inductor", "Lstorage", supply, load)
    _instance(design, "resistor", "Rload", load, reference)
    return design


def _composite_capacitor_dc() -> volt.Design:
    design = volt.Design("composite_capacitor_dc")
    terminal_a, reference = design.net("terminal-a"), design.net("reference")
    _instance(design, "composite-capacitor", "C1", terminal_a, reference)
    return design


def main() -> volt.Project:
    """Return the configured fixture Project without running or simulating it."""
    project = volt.Project("ngspice-dc-integration-corpus", version="1")
    project.use_library(LIBRARY)

    @project.design
    def design():
        return tuple(
            factory()
            for factory in (
                _divider,
                _series_parallel_load,
                _source_polarity,
                _zero_ohm_constraint,
                _ideal_capacitor_dc,
                _ideal_inductor_dc,
                _composite_capacitor_dc,
            )
        )

    return project


def _handles(design: volt.Design):
    dc_input = volt.prepare_dc_input(design)
    nets = {net.name: dc_input.net(net) for net in design.nets()}
    components = {
        component.reference: dc_input.occurrence(component)
        for component in design.components()
    }
    return dc_input, nets, components


def request_for(design: volt.Design) -> volt.DcRequest:
    """Create the corpus request and direct observation probes for one fixture Design."""
    dc_input, net, component = _handles(design)

    def voltage(key: str, from_: str, to: str = "reference"):
        return volt.DcVoltageProbe(key, net[from_], net[to])

    def current(key: str, reference: str, element: str):
        return volt.DcModelElementCurrentProbe(key, component[reference], element)

    def source_current(key: str, source: str):
        return volt.DcSourceCurrentProbe(key, source)

    def volts(value: float):
        return volt.Quantity(volt.UnitDimension.VOLTAGE, value)

    def amperes(value: float):
        return volt.Quantity(volt.UnitDimension.CURRENT, value)

    if design.name == "divider":
        sources = (volt.DcVoltageSource("supply-5v", net["supply"], net["reference"], volts(5)),)
        probes = (
            voltage("supply_voltage_v", "supply"),
            voltage("midpoint_voltage_v", "midpoint"),
            current("upper_current_a", "R1", "body"),
            current("lower_current_a", "R2", "body"),
            source_current("source_current_a", "supply-5v"),
        )
        key = "divider-operating-point"
    elif design.name == "series_parallel_load":
        sources = (volt.DcVoltageSource("drive", net["supply"], net["reference"], volts(5)),)
        probes = (
            voltage("load_voltage_v", "load"),
            current("series_current_a", "Rseries", "body"),
            current("load_1k_current_a", "Rload1", "body"),
            current("load_2k_current_a", "Rload2", "body"),
            source_current("source_current_a", "drive"),
        )
        key = "series-parallel-load"
    elif design.name == "source_polarity":
        sources = (
            volt.DcVoltageSource("positive", net["positive"], net["reference"], volts(5)),
            volt.DcVoltageSource("negative", net["negative"], net["reference"], volts(-5)),
            volt.DcCurrentSource("inject", net["reference"], net["current-driven"], amperes(0.002)),
        )
        probes = (
            voltage("positive_node_v", "positive"),
            source_current("positive_source_current_a", "positive"),
            voltage("negative_node_v", "negative"),
            source_current("negative_source_current_a", "negative"),
            current("reversed_resistor_current_a", "Rnegative", "body"),
            voltage("current_driven_node_v", "current-driven"),
            source_current("authored_current_source_a", "inject"),
        )
        key = "source-polarity"
    elif design.name == "zero_ohm_constraint":
        sources = (volt.DcVoltageSource("drive", net["supply"], net["reference"], volts(5)),)
        probes = (
            voltage("supply_voltage_v", "supply"),
            voltage("load_voltage_v", "load"),
            current("constraint_current_a", "Rwire", "body"),
            source_current("source_current_a", "drive"),
        )
        key = "zero-ohm"
    elif design.name == "ideal_capacitor_dc":
        sources = (volt.DcVoltageSource("drive", net["supply"], net["reference"], volts(5)),)
        probes = (
            voltage("supply_voltage_v", "supply"),
            voltage("downstream_voltage_v", "downstream"),
            current("capacitor_current_a", "Cstorage", "storage"),
            source_current("source_current_a", "drive"),
        )
        key = "ideal-capacitor"
    elif design.name == "ideal_inductor_dc":
        sources = (volt.DcVoltageSource("drive", net["supply"], net["reference"], volts(5)),)
        probes = (
            voltage("supply_voltage_v", "supply"),
            voltage("load_voltage_v", "load"),
            current("inductor_current_a", "Lstorage", "storage"),
            source_current("source_current_a", "drive"),
        )
        key = "ideal-inductor"
    elif design.name == "composite_capacitor_dc":
        sources = (volt.DcVoltageSource("drive", net["terminal-a"], net["reference"], volts(5)),)
        probes = (
            voltage("terminal_a_voltage_v", "terminal-a"),
            current("esr_current_a", "C1", "esr"),
            current("esl_current_a", "C1", "esl"),
            current("capacitor_current_a", "C1", "storage"),
            source_current("source_current_a", "drive"),
        )
        key = "composite-capacitor"
    else:
        raise KeyError(design.name)

    return volt.DcRequest(
        key,
        dc_input,
        reference=net["reference"],
        sources=sources,
        probes=probes,
    )
