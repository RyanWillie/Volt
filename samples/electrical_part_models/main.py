"""Author illustrative exact R/C/L Parts and a logical-only ProjectBundle."""

import json
from pathlib import Path
import sys

import volt


EVIDENCE = b"Illustrative passive parameters for the Volt authoring example; not measured."
VI_EVIDENCE = b"Illustrative terminal voltage limit; not a manufacturer guarantee."


def build_library():
    library = volt.Library("volt.samples.electrical_models", version="1")
    footprint = volt.Footprint(
        (library.namespace, "illustrative-2"),
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-0.5, 0), size=(0.5, 0.5)),
            volt.FootprintPad.surface_mount("2", at=(0.5, 0), size=(0.5, 0.5)),
        ),
    )
    evidence = (volt.content_hash(EVIDENCE),)

    def component(name):
        return library.component(
            name, pins=(volt.PinSpec("A", 1), volt.PinSpec("B", 2)),
            contract=volt.ComponentContract(
                f"volt.samples.electrical_models/{name}@1", ("A", "B")
            ),
        )

    def fields(name):
        return dict(
            footprint=footprint,
            pads={"A": "1", "B": "2"},
            manufacturer="Volt illustrative examples",
            mpn=name,
            package="ILLUSTRATIVE-2",
            provenance=volt.PartProvenance(
                authored_by="Volt examples", derived_from="Illustrative values only"
            ),
        )

    resistor_component = component("R330")
    rb = volt.PartElectricalModelBuilder(resistor_component)
    a, b = rb.terminal("a", "A"), rb.terminal("b", "B")
    rb.add(volt.ResistanceElement,
        "body", a, b,
        volt.ModelParameter(volt.ohms(330), volt.Tolerance.percent(0.01), evidence),
    )
    library.part(
        "R330", component=resistor_component, **fields("R330"),
        electrical_model=rb.build(), evidence_assets=(EVIDENCE,),
    )

    capacitor_component = component("C-ideal")
    cb = volt.PartElectricalModelBuilder(capacitor_component)
    a, b = cb.terminal("a", "A"), cb.terminal("b", "B")
    cb.add(volt.CapacitanceElement, "storage", a, b, volt.ModelParameter(volt.farads(100e-9)))
    library.part("C-ideal", component=capacitor_component, **fields("C-ideal"), electrical_model=cb.build())

    inductor_component = component("L-ideal")
    lb = volt.PartElectricalModelBuilder(inductor_component)
    a, b = lb.terminal("a", "A"), lb.terminal("b", "B")
    lb.add(volt.InductanceElement,
        "storage", a, b,
        volt.ModelParameter(volt.henries(10e-6), volt.Tolerance.percent(0)),
    )
    library.part("L-ideal", component=inductor_component, **fields("L-ideal"), electrical_model=lb.build())

    composite_component = component("C-ESR-ESL")
    composite_fields = fields("C-ESR-ESL")
    composite_fields["electrical_records"] = (
        volt.ElectricalRecord(
            volt.ElectricalSubject.directed_pins("A", "B"),
            "voltage", "absolute_limit", "range", minimum=-25, maximum=25,
            evidence=(str(evidence[0]), str(volt.content_hash(VI_EVIDENCE))),
        ),
    )
    xb = volt.PartElectricalModelBuilder(composite_component)
    a, b = xb.terminal("a", "A"), xb.terminal("b", "B")
    x, y = xb.internal_node("after_esr"), xb.internal_node("after_esl")
    xb.add(volt.ResistanceElement, "esr", a, x, volt.ModelParameter(volt.ohms(0.08)))
    xb.add(volt.InductanceElement, "esl", x, y, volt.ModelParameter(volt.henries(1e-9)))
    xb.add(volt.CapacitanceElement,
        "storage", y, b,
        volt.ModelParameter(volt.farads(10e-6), volt.Tolerance.percent(0.2), evidence),
    )
    library.part(
        "C-ESR-ESL", component=composite_component, **composite_fields, electrical_model=xb.build(),
        evidence_assets=(EVIDENCE, VI_EVIDENCE),
    )
    library.part("unmodeled", component=component("unmodeled"), **fields("unmodeled"))
    return library


def write_example(destination: Path) -> None:
    library = build_library()
    library_result = library.build()
    assert library_result.ok
    project = volt.Project("electrical-part-models", version="1")

    @project.design
    def design():
        d = volt.Design("main")
        positive, negative = d.net("positive"), d.net("negative")
        references = {"R330": "R1", "C-ideal": "C1", "L-ideal": "L1",
                      "C-ESR-ESL": "C2", "unmodeled": "U1"}
        for part in library.parts:
            instance = d.instantiate(part, ref=references[part.name])
            instance.dnp(False)
            positive += instance["A"]
            negative += instance["B"]
        return d

    result = project.run()
    assert result.ok, [(item.code, item.message) for item in result.diagnostics]
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "library.voltlib").write_bytes(library_result.bundle_bytes)
    result.write(destination / "project.volt")
    # Artifact expectations survive removal of the authoring source and original library.
    expected = {
        "parts": {
            part.name: {
                "bytes": part.artifact.bytes.decode("utf-8"),
                "exact_reference": part.exact_reference,
            }
            for part in library_result.parts
        },
        "evidence": [EVIDENCE.decode("utf-8"), VI_EVIDENCE.decode("utf-8")],
    }
    (destination / "expected.json").write_text(json.dumps(expected), encoding="utf-8")
    print(f"Wrote five exact Parts and a logical-only project to {destination}")


if __name__ == "__main__":
    write_example(Path(sys.argv[1]))
