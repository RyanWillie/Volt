import json

import volt


def _select_resistor(component, *, part_number, alternates=(), selection_override=False):
    component.select_part(
        manufacturer="Yageo",
        part_number=part_number,
        package="0603",
        footprint=("passives", "R_0603_1608Metric"),
        pin_pads={1: "1", 2: "2"},
        approved_alternate_mpns=alternates,
        selection_override=selection_override,
    )
    return component


def test_design_bom_projects_dnp_overrides_alternates_and_sourcing():
    design = volt.Design("bom-demo")
    r1 = design.R("330", ref="R1").dnp(False)
    r2 = design.R("330", ref="R2").dnp(False)
    r3 = design.R("1k", ref="R3").dnp(True)
    _select_resistor(r1, part_number="RC0603FR-07330RL", alternates=("RC0603FR-07330RLA",))
    _select_resistor(
        r2,
        part_number="RC0603FR-07330RL",
        alternates=("RC0603FR-07330RLA",),
        selection_override=True,
    )
    _select_resistor(r3, part_number="RC0603FR-071KL", alternates=("RC0603FR-071KLA",))
    design.set_sourcing_snapshot(
        {
            "RC0603FR-07330RL": {
                "supplier": "Digi-Key",
                "sku": "311-330HRCT-ND",
            },
        }
    )

    bom = design.bom()

    assert bom["format"] == "volt.bom"
    assert bom["lines"][0]["mpn"] == "RC0603FR-071KL"
    assert bom["lines"][0]["dnp"] is True
    assert bom["lines"][0]["quantity"] == 0
    assert bom["lines"][1]["mpn"] == "RC0603FR-07330RL"
    assert bom["lines"][1]["quantity"] == 2
    assert bom["lines"][1]["references"] == ["R1", "R2"]
    assert bom["lines"][1]["approved_alternate_mpns"] == ["RC0603FR-07330RLA"]
    assert bom["lines"][1]["selection_override_references"] == ["R2"]
    assert bom["lines"][1]["sourcing"] == {
        "sku": "311-330HRCT-ND",
        "supplier": "Digi-Key",
    }
    assert bom["components"][2]["dnp"] is True

    assert design.bom_csv() == (
        "manufacturer,mpn,package,quantity,references,dnp,approved_alternate_mpns,"
        "selection_override_references,sourcing.sku,sourcing.supplier\n"
        "Yageo,RC0603FR-071KL,0603,0,R3,true,RC0603FR-071KLA,,,\n"
        "Yageo,RC0603FR-07330RL,0603,2,R1 R2,false,RC0603FR-07330RLA,R2,"
        "311-330HRCT-ND,Digi-Key\n"
    )
    assert not design.validate_bom_readiness().has_errors


def test_bom_readiness_diagnostics_reference_offending_instances():
    design = volt.Design("bom-readiness")
    missing_part = design.R("330", ref="R1").dnp(False)
    implicit_dnp = design.R("330", ref="R2")
    bad_alternate = design.R("1k", ref="R3").dnp(False)
    _select_resistor(implicit_dnp, part_number="RC0603FR-07330RL")
    _select_resistor(
        bad_alternate,
        part_number="RC0603FR-071KL",
        alternates=("RC0603FR-071KL",),
    )

    report = design.validate_bom_readiness()

    assert [diagnostic.code for diagnostic in report] == [
        "BOM_COMPONENT_MISSING_SELECTED_PART",
        "BOM_COMPONENT_IMPLICIT_DNP",
        "BOM_APPROVED_ALTERNATE_INCOMPATIBLE",
    ]
    assert all(diagnostic.category == "bom" for diagnostic in report)
    assert [diagnostic.entities[0].kind for diagnostic in report] == [
        "component",
        "component",
        "component",
    ]


def test_project_bundle_emits_bom_json_and_csv(tmp_path):
    project = volt.Project("bom-demo")

    @project.design
    def design():
        result = volt.Design("bom-demo")
        r1 = result.R("330", ref="R1").dnp(False)
        _select_resistor(
            r1,
            part_number="RC0603FR-07330RL",
            alternates=("RC0603FR-07330RLA",),
        )
        result.set_sourcing_snapshot(
            {"RC0603FR-07330RL": {"supplier": "Digi-Key", "sku": "311-330HRCT-ND"}}
        )
        return result

    bundle = tmp_path / "bundle"
    project.run().write(bundle)

    bom_json = json.loads((bundle / "bom" / "bom.json").read_text(encoding="utf-8"))
    bom_csv = (bundle / "bom" / "bom.csv").read_text(encoding="utf-8")
    manifest = json.loads((bundle / "manifest.volt.json").read_text(encoding="utf-8"))

    assert bom_json["lines"][0]["mpn"] == "RC0603FR-07330RL"
    assert "RC0603FR-07330RLA" in bom_csv
    assert {
        (artifact["kind"], artifact["path"])
        for artifact in manifest["artifacts"]
        if artifact["kind"].startswith("bom")
    } == {
        ("bom", "bom/bom.json"),
        ("bom_csv", "bom/bom.csv"),
    }
