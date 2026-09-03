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

    def fields(name):
        return dict(
            pins=(volt.PinSpec("A", 1), volt.PinSpec("B", 2)),
            contract=volt.ComponentContract(
                f"volt.samples.electrical_models/{name}@1", ("A", "B")
            ),
            footprint=footprint,
            pads={"A": "1", "B": "2"},
            manufacturer="Volt illustrative examples",
            mpn=name,
            package="ILLUSTRATIVE-2",
            provenance=volt.PartProvenance(
                authored_by="Volt examples", derived_from="Illustrative values only"
            ),
        )

    resistor_fields = fields("R330")
    rb = library.electrical_model_builder("R330", **resistor_fields)
    a, b = rb.terminal("a", "A"), rb.terminal("b", "B")
    rb.resistance(
        "body", a, b,
        volt.ModelParameter(volt.ohms(330), volt.Tolerance.percent(0.01), evidence),
    )
    library.part(
        "R330", **resistor_fields,
        electrical_model=rb.build(), evidence_assets=(EVIDENCE,),
    )

    capacitor_fields = fields("C-ideal")
    cb = library.electrical_model_builder("C-ideal", **capacitor_fields)
    a, b = cb.terminal("a", "A"), cb.terminal("b", "B")
    cb.capacitance("storage", a, b, volt.ModelParameter(volt.farads(100e-9)))
    library.part("C-ideal", **capacitor_fields, electrical_model=cb.build())

    inductor_fields = fields("L-ideal")
    lb = library.electrical_model_builder("L-ideal", **inductor_fields)
    a, b = lb.terminal("a", "A"), lb.terminal("b", "B")
    lb.inductance(
        "storage", a, b,
        volt.ModelParameter(volt.henries(10e-6), volt.Tolerance.percent(0)),
    )
    library.part("L-ideal", **inductor_fields, electrical_model=lb.build())

    composite_fields = fields("C-ESR-ESL")
    composite_fields["electrical_records"] = (
        volt.ElectricalRecord(
            volt.ElectricalSubject.directed_pins("A", "B"),
            "voltage", "absolute_limit", "range", minimum=-25, maximum=25,
            evidence=(str(evidence[0]), str(volt.content_hash(VI_EVIDENCE))),
        ),
    )
    xb = library.electrical_model_builder("C-ESR-ESL", **composite_fields)
    a, b = xb.terminal("a", "A"), xb.terminal("b", "B")
    x, y = xb.internal_node("after_esr"), xb.internal_node("after_esl")
    xb.resistance("esr", a, x, volt.ModelParameter(volt.ohms(0.08)))
    xb.inductance("esl", x, y, volt.ModelParameter(volt.henries(1e-9)))
    xb.capacitance(
        "storage", y, b,
        volt.ModelParameter(volt.farads(10e-6), volt.Tolerance.percent(0.2), evidence),
    )
    library.part(
        "C-ESR-ESL", **composite_fields, electrical_model=xb.build(),
        evidence_assets=(EVIDENCE, VI_EVIDENCE),
    )
    library.part("unmodeled", **fields("unmodeled"))
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
