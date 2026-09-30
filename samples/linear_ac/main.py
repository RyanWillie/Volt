"""Exact-part RC low-pass authoring. Project stages never invoke a solver."""
import volt

LIBRARY = volt.Library("volt.samples.linear_ac", version="1")


def _part(key, element, quantity):
    component = LIBRARY.component(
        key, pins=(volt.PinSpec("A", 1), volt.PinSpec("B", 2)),
        contract=volt.ComponentContract(f"volt.samples.linear_ac/{key}@1", ("A", "B")),
    )
    model = volt.PartElectricalModelBuilder(component)
    a, b = model.terminal("a", "A"), model.terminal("b", "B")
    model.add(element, "body", a, b, volt.ModelParameter(quantity))
    return LIBRARY.part(
        key, component=component,
        footprint=volt.Footprint(
            (LIBRARY.namespace, "illustrative-2"),
            pads=(volt.FootprintPad.surface_mount("1", at=(-0.5, 0), size=(0.5, 0.5)),
                  volt.FootprintPad.surface_mount("2", at=(0.5, 0), size=(0.5, 0.5))),
        ), pads={"A": "1", "B": "2"},
        manufacturer="Volt illustrative examples", mpn=key, package="ILLUSTRATIVE-2",
        electrical_model=model.build(), prefix="R" if element is volt.ResistanceElement else "C",
    )

RESISTOR = _part("R1K", volt.ResistanceElement, volt.ohms(1000))
CAPACITOR = _part("C1U", volt.CapacitanceElement, volt.farads(1e-6))


def main():
    project = volt.Project("linear-ac-lowpass", version="1")
    project.use_library(LIBRARY)

    @project.design
    def design():
        result = volt.Design("lowpass")
        supply, output, ground = result.net("INPUT"), result.net("OUTPUT"), result.net("GROUND", kind="ground")
        resistor = result.instantiate(RESISTOR, ref="R1").dnp(False)
        capacitor = result.instantiate(CAPACITOR, ref="C1").dnp(False)
        supply += resistor["A"]
        output += resistor["B"], capacitor["A"]
        ground += capacitor["B"]
        return result

    return project
