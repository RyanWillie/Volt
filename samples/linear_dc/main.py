"""A logical-only, exact-Part 5 V resistor divider project."""

import volt


LIBRARY = volt.Library("volt.samples.linear_dc", version="1")
RESISTOR_COMPONENT = LIBRARY.component(
    "R1K",
    pins=(volt.PinSpec("A", 1), volt.PinSpec("B", 2)),
    contract=volt.ComponentContract("volt.samples.linear_dc/R1K@1", ("A", "B")),
)

_model = volt.PartElectricalModelBuilder(RESISTOR_COMPONENT)
_a = _model.terminal("a", "A")
_b = _model.terminal("b", "B")
_model.add(
    volt.ResistanceElement,
    "body",
    _a,
    _b,
    volt.ModelParameter(volt.ohms(1_000)),
)

RESISTOR_1K = LIBRARY.part(
    "R1K",
    component=RESISTOR_COMPONENT,
    footprint=volt.Footprint(
        (LIBRARY.namespace, "illustrative-2"),
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-0.5, 0), size=(0.5, 0.5)),
            volt.FootprintPad.surface_mount("2", at=(0.5, 0), size=(0.5, 0.5)),
        ),
    ),
    pads={"A": "1", "B": "2"},
    manufacturer="Volt illustrative examples",
    mpn="R1K",
    package="ILLUSTRATIVE-2",
    prefix="R",
    electrical_model=_model.build(),
)


def main() -> volt.Project:
    """Return the configured Project without running any stage or simulation."""
    project = volt.Project(
        "linear-dc-divider",
        version="1",
        description="Logical-only 5 V divider for explicit native linear DC",
    )
    project.use_library(LIBRARY)

    @project.design
    def design():
        result = volt.Design("divider")
        supply = result.net("SUPPLY", kind="power", voltage=5.0)
        midpoint = result.net("MIDPOINT")
        ground = result.net("GROUND", kind="ground")
        upper = result.instantiate(RESISTOR_1K, ref="R1").dnp(False)
        lower = result.instantiate(RESISTOR_1K, ref="R2").dnp(False)
        supply += upper["A"]
        midpoint += upper["B"], lower["A"]
        ground += lower["B"]
        return result

    return project
