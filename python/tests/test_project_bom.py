import json

import volt
from project_framework_helpers import _passive_0603


def _resistor_parts(*specs):
    library = volt.Library("volt.tests.bom", version="1.0.0")
    return tuple(
        library.part(
            name,
            pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
            value=value,
            manufacturer="Yageo",
            mpn=mpn,
            package="0603",
            prefix="R",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            pads={1: "1", 2: "2"},
            approved_alternate_mpns=alternates,
        )
        for name, value, mpn, alternates in specs
    )


def test_design_bom_projects_dnp_overrides_alternates_and_sourcing():
    r330_a, r330_b, r1k = _resistor_parts(
        ("R-330-A", "330", "RC0603FR-07330RL", ("RC0603FR-07330RLA",)),
        ("R-330-B", "330", "RC0603FR-07330RL", ("RC0603FR-07330RLA",)),
        ("R-1K", "1k", "RC0603FR-071KL", ("RC0603FR-071KLA",)),
    )
    design = volt.Design("bom-demo")
    design.instantiate(r330_a, ref="R1").dnp(False)
    design.instantiate(r330_b, ref="R2").dnp(False).selection_override()
    design.instantiate(r1k, ref="R3").dnp(True)
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
    implicit_part, bad_part = _resistor_parts(
        ("R-330", "330", "RC0603FR-07330RL", ()),
        ("R-1K-Bad", "1k", "RC0603FR-071KL", ("RC0603FR-071KL",)),
    )
    design = volt.Design("bom-readiness")
    design.R("330", ref="R1").dnp(False)
    design.instantiate(implicit_part, ref="R2")
    design.instantiate(bad_part, ref="R3").dnp(False)

    report = design.validate_bom_readiness()

    assert [diagnostic.code for diagnostic in report] == [
        "BOM_COMPONENT_MISSING_SELECTED_PART",
        "BOM_COMPONENT_IMPLICIT_DNP",
        "BOM_APPROVED_ALTERNATE_DUPLICATES_PRIMARY",
    ]
    assert all(diagnostic.category == "bom" for diagnostic in report)
    assert [diagnostic.entities[0].kind for diagnostic in report] == [
        "component",
        "component",
        "component",
    ]


def test_project_bundle_default_graph_does_not_implicitly_export_bom(tmp_path):
    project = volt.Project("bom-demo")

    @project.design
    def design():
        (part,) = _resistor_parts(
            ("R-330", "330", "RC0603FR-07330RL", ("RC0603FR-07330RLA",)),
        )
        result = volt.Design("bom-demo")
        result.instantiate(part, ref="R1").dnp(False)
        result.set_sourcing_snapshot(
            {"RC0603FR-07330RL": {"supplier": "Digi-Key", "sku": "311-330HRCT-ND"}}
        )
        return result

    bundle = tmp_path / "bundle"
    project.run().write(bundle)

    manifest = json.loads((bundle / "manifest.volt.json").read_text(encoding="utf-8"))

    assert manifest["export_selection"] == []
    assert "bom" not in {artifact["kind"] for artifact in manifest["artifacts"]}
    assert project.run().design().bom()["lines"][0]["mpn"] == "RC0603FR-07330RL"


def test_project_bundle_reports_bom_readiness_failures(tmp_path):
    project = volt.Project("not-bom-ready")

    @project.design
    def design():
        result = volt.Design("not-bom-ready")
        vcc = result.net("VCC").mark_stub()
        mid = result.net("MID")
        gnd = result.net("GND").mark_stub()
        r1 = result.R("330", ref="R1").dnp(False)
        r2 = result.R("1k", ref="R2")
        vcc += r1[1]
        mid += r1[2], r2[1]
        gnd += r2[2]
        return result

    bundle = tmp_path / "bundle"
    project.run().write(bundle)

    manifest = json.loads((bundle / "manifest.volt.json").read_text(encoding="utf-8"))
    diagnostics_artifact = next(
        artifact
        for artifact in manifest["artifacts"]
        if artifact["kind"] == "diagnostics"
    )
    diagnostics = json.loads(
        (bundle / diagnostics_artifact["path"]).read_text(encoding="utf-8")
    )

    assert manifest["run"]["ok"] is False
    assert manifest["run"]["status"] == "failed"
    assert [item["report"] for item in diagnostics["diagnostics"]] == [
        "logical.bom_ready",
        "logical.bom_ready",
        "logical.bom_ready",
    ]
    assert [item["code"] for item in diagnostics["diagnostics"]] == [
        "BOM_COMPONENT_MISSING_SELECTED_PART",
        "BOM_COMPONENT_IMPLICIT_DNP",
        "BOM_COMPONENT_MISSING_SELECTED_PART",
    ]
