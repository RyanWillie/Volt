"""A logical-only 1 kOhm/idealized diode bias network; no implicit simulation."""
import volt

LIBRARY = volt.Library("volt.samples.nonlinear_dc", version="1")
EVIDENCE = b"Illustrative idealized Shockley law, Is=1pA, n=1, T=300.15K; no physical calibration."


def _part(name, element_type, parameters):
    component = LIBRARY.component(
        name, pins=(volt.PinSpec("A", 1), volt.PinSpec("B", 2)),
        contract=volt.ComponentContract(f"volt.samples.nonlinear_dc/{name}@1", ("A", "B")))
    builder = volt.PartElectricalModelBuilder(component)
    a, b = builder.terminal("a", "A"), builder.terminal("b", "B")
    builder.add(element_type, "body", a, b, parameters)
    return LIBRARY.part(
        name, component=component,
        footprint=volt.Footprint((LIBRARY.namespace, "illustrative-2"), pads=(
            volt.FootprintPad.surface_mount("1", at=(-.5, 0), size=(.5, .5)),
            volt.FootprintPad.surface_mount("2", at=(.5, 0), size=(.5, .5)))),
        pads={"A": "1", "B": "2"}, manufacturer="Volt illustrative examples",
        mpn=name, package="ILLUSTRATIVE-2", electrical_model=builder.build(),
        evidence_assets=(EVIDENCE,))


R1K = _part("R1K", volt.ResistanceElement, volt.ModelParameter(volt.ohms(1000)))
DIODE = _part("D-ideal", volt.ShockleyDiodeElement, volt.DiodeParameters(
    volt.ModelParameter(volt.Quantity(volt.UnitDimension.CURRENT, 1e-12),
                       evidence=(volt.content_hash(EVIDENCE),)),
    volt.ModelParameter(volt.Quantity(volt.UnitDimension.RATIO, 1)),
    volt.Quantity(volt.UnitDimension.TEMPERATURE, 300.15),
    volt.QuantityRange.bounded(volt.Quantity(volt.UnitDimension.VOLTAGE, -.05),
                               volt.Quantity(volt.UnitDimension.VOLTAGE, .8)),
    evidence=(volt.content_hash(EVIDENCE),)))


def main():
    project = volt.Project("nonlinear-dc-bias", version="1")
    project.use_library(LIBRARY)

    @project.design
    def design():
        result = volt.Design("diode-bias")
        supply, anode, ground = (result.net("SUPPLY"), result.net("ANODE"),
                                  result.net("GROUND", kind="ground"))
        resistor = result.instantiate(R1K, ref="R1").dnp(False)
        diode = result.instantiate(DIODE, ref="D1").dnp(False)
        supply += resistor["A"]
        anode += resistor["B"], diode["A"]
        ground += diode["B"]
        return result

    return project


def request_for(input, supply_voltage=5):
    """Exact immutable source request; parameters remain intrinsic Part data."""
    # This example declares SUPPLY, ANODE, GROUND in that exact native order.
    supply, anode, ground = input.nets
    return volt.DcRequest(
        "diode-operating-point", input, reference=ground,
        sources=(volt.DcVoltageSource("supply", supply, ground,
                  volt.Quantity(volt.UnitDimension.VOLTAGE, supply_voltage)),),
        probes=(volt.DcVoltageProbe("anode-voltage", anode, ground),
                volt.DcSourceCurrentProbe("supply-current", "supply")))
