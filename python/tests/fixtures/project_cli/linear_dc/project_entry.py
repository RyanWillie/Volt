import os
from pathlib import Path

import volt


def _record_source_execution():
    sentinel = os.environ.get("VOLT_TEST_SOURCE_SENTINEL")
    if sentinel:
        with Path(sentinel).open("a", encoding="utf-8") as handle:
            handle.write("executed\n")


def _resistor_part():
    library = volt.Library("test.cli.linear-dc", version="1.0.0")
    component = library.component(
        "Linear resistor",
        pins=(
            volt.PinSpec("A", 1, role=None),
            volt.PinSpec("B", 2, role=None),
        ),
        contract=volt.ComponentContract(
            "test.cli.linear-dc/resistor@1", pin_keys=("A", "B")
        ),
    )
    model = volt.PartElectricalModelBuilder(component)
    a = model.terminal("a", "A")
    b = model.terminal("b", "B")
    resistance = 1001.0 if os.environ.get("VOLT_TEST_CHANGED_PART") else 1000.0
    model.add(
        volt.ResistanceElement,
        "body",
        a,
        b,
        volt.ModelParameter(volt.ohms(resistance)),
    )
    footprint = volt.Footprint(
        ("Test", "R2"),
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-0.5, 0), size=(0.5, 0.5)),
            volt.FootprintPad.surface_mount("2", at=(0.5, 0), size=(0.5, 0.5)),
        ),
    )
    return library.part(
        "R-1K",
        component=component,
        footprint=footprint,
        pads={"A": "1", "B": "2"},
        manufacturer="Volt Test",
        mpn="R-1K",
        package="TEST-2",
        electrical_model=model.build(),
        prefix="R",
    )


def _divider(part, name):
    design = volt.Design(name)
    supply = design.net("SUPPLY")
    midpoint = design.net("MIDPOINT")
    reference = design.net("REFERENCE")
    first = design.instantiate(part, ref="R1")
    second = design.instantiate(part, ref="R2")
    first.dnp(False)
    second.dnp(False)
    supply += first["A"]
    midpoint += first["B"], second["A"]
    reference += second["B"]
    if os.environ.get("VOLT_TEST_CHANGED_LOGICAL"):
        design.net("STALE")
    return design


def _floating():
    design = volt.Design("floating")
    design.net("REFERENCE")
    design.net("UNFIXED")
    return design


def _unsupported():
    design = volt.Design("unsupported")
    first = design.net("FIRST")
    reference = design.net("REFERENCE")
    resistor = design.R("1 kOhm", ref="R1")
    first += resistor[1]
    reference += resistor[2]
    return design


def main():
    _record_source_execution()
    print("linear-dc fixture source stdout")
    if os.environ.get("VOLT_TEST_FORBID_SIMULATION"):
        def forbidden_simulation(*_args, **_kwargs):
            raise AssertionError("ordinary project command invoked native DC execution")

        volt.compile_electrical = forbidden_simulation
        volt.solve_dc = forbidden_simulation
    project = volt.Project("linear-dc-cli", version="1.0.0")
    project.expect_diagnostic(code="SINGLE_PIN_NET", design="divider")
    project.expect_diagnostic(code="SINGLE_PIN_NET", design="alternate")
    project.expect_diagnostic(code="EMPTY_NET", design="floating")
    project.expect_diagnostic(code="SINGLE_PIN_NET", design="unsupported")
    project.expect_diagnostic(code="BOM_COMPONENT_IMPLICIT_DNP", design="unsupported")
    project.expect_diagnostic(code="BOM_COMPONENT_MISSING_SELECTED_PART", design="unsupported")
    part = _resistor_part()

    @project.design
    def design():
        return (
            _divider(part, "divider"),
            _divider(part, "alternate"),
            _floating(),
            _unsupported(),
        )

    if os.environ.get("VOLT_TEST_FAIL_DESIGN_TEST"):
        @project.design.test
        def deliberate_failure(_check):
            raise AssertionError("deliberate logical test failure")

    if os.environ.get("VOLT_TEST_FAIL_BOARD"):
        @project.board
        def board(_context):
            raise AssertionError("simulation executed the Board stage")

    if os.environ.get("VOLT_TEST_RETURN_RESULT"):
        return project.run_through(project.design)
    return project
