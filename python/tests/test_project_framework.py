import json

import pytest

import volt
from project_framework_helpers import (
    _board_ready_design,
    _minimal_design,
    _native_fixture_parts,
    _overvoltage_exact_part_design,
    _passive_0603,
    _stage_board,
    _stage_schematic,
)


def test_project_metadata_and_missing_build_function():
    project = volt.Project("status-led", version="0.1.0", description="LED module")

    assert project.name == "status-led"
    assert project.version == "0.1.0"
    assert project.description == "LED module"
    assert project.design.name == "design"
    assert project.schematic.name == "schematic"
    assert project.board.name == "board"

    with pytest.raises(RuntimeError, match="Project status-led has no build stages"):
        project.run()


def test_project_rejects_duplicate_stage_registration():
    project = volt.Project("status-led")

    @project.design
    def first():
        return volt.Design("first")

    with pytest.raises(RuntimeError, match="design stage is already registered"):

        @project.design
        def second():
            return volt.Design("second")


def test_project_stage_registers_tests_without_strings():
    project = volt.Project("status-led")

    @project.design.test
    def netlist_check(check):
        check.ok("placeholder")

    assert [test.name for test in project.design.tests] == ["netlist_check"]


def test_project_rejects_empty_name():
    with pytest.raises(ValueError, match="Project name must not be empty"):
        volt.Project("")


def test_project_rejects_stage_without_returned_model():
    project = volt.Project("status-led")

    @project.design
    def design():
        return None

    with pytest.raises(RuntimeError, match="design stage must return at least one Design"):
        project.run()


def test_project_rejects_wrong_model_type_from_stage():
    project = volt.Project("status-led")

    @project.design
    def design():
        return _minimal_design()

    @project.schematic
    def schematic(context):
        return volt.Design("not-a-schematic")

    with pytest.raises(TypeError, match="schematic stage must return Schematic models"):
        project.run()


def test_project_run_executes_staged_workflow_and_collects_models():
    project = volt.Project("status-led")
    calls = []

    @project.design
    def design():
        calls.append("design")
        return _minimal_design()

    @project.schematic
    def schematic(context):
        calls.append(("schematic", type(context).__name__, context.design().name))
        return context.design().schematic("Main")

    @project.board
    def board(context):
        calls.append(("board", type(context).__name__, context.design().name))
        pcb = context.design().add_board("Main")
        pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        return pcb

    result = project.run()

    assert calls == [
        "design",
        ("schematic", "BuildContext", "status-led"),
        ("board", "BuildContext", "status-led"),
    ]
    assert result.design("status-led").name == "status-led"
    assert result.schematic("Main").name == "Main"
    assert result.board("Main").name == "Main"
    assert [stage.name for stage in result.stages] == ["design", "schematic", "board"]


def test_project_later_stages_always_receive_build_context_for_single_design():
    project = volt.Project("status-led")
    contexts = []

    @project.design
    def design():
        return _board_ready_design()

    def _record_context(context):
        contexts.append(
            (
                type(context).__name__,
                tuple(design.name for design in context.designs),
                tuple(resource.name for resource in context.resources),
            )
        )

    @project.schematic
    def schematic(context):
        _record_context(context)
        return _stage_schematic(context.design())

    @project.board
    def board(context):
        _record_context(context)
        return _stage_board(context.design())

    result = project.run()

    assert result.ok
    assert contexts == [
        ("BuildContext", ("status-led",), ()),
        ("BuildContext", ("status-led",), ()),
    ]


def test_project_later_stages_keep_same_build_context_signature_with_resources_and_multiple_designs():
    project = volt.Project("control-panel")
    contexts = []

    @project.design
    def design():
        return (
            _board_ready_design("main-controller"),
            _board_ready_design("front-panel"),
            volt.ProjectResource("variant", "A"),
        )

    def _record_context(context):
        contexts.append(
            (
                type(context).__name__,
                tuple(design.name for design in context.designs),
                tuple(resource.name for resource in context.resources),
            )
        )

    @project.schematic
    def schematic(context):
        _record_context(context)
        return (
            _stage_schematic(context.design("main-controller")),
            _stage_schematic(context.design("front-panel")),
            volt.ProjectResource("anchor", (20, 20)),
        )

    @project.board
    def board(context):
        _record_context(context)
        assert context.resource("variant", str) == "A"
        assert context.resource("anchor", tuple) == (20, 20)
        return (
            _stage_board(context.design("main-controller")),
            _stage_board(context.design("front-panel")),
        )

    result = project.run()

    assert result.ok
    assert contexts == [
        ("BuildContext", ("main-controller", "front-panel"), ("variant",)),
        (
            "BuildContext",
            ("main-controller", "front-panel"),
            ("variant", "anchor"),
        ),
    ]


def test_project_stage_resources_are_available_to_later_stages():
    project = volt.Project("status-led")
    calls = []

    @project.design
    def design():
        design = _board_ready_design()
        nets = {net.name: net for net in design.nets()}
        return design, volt.ProjectResource("nets", nets)

    @project.schematic
    def schematic(context):
        nets = context.resource("nets", dict)
        calls.append(("schematic", context.design().name, sorted(nets)))
        return _stage_schematic(context.design()), volt.ProjectResource(
            "schematic_anchor",
            (20, 20),
        )

    @project.board
    def board(context):
        calls.append(("board", context.resource("schematic_anchor", tuple)))
        return _stage_board(context.design())

    result = project.run()

    assert calls == [
        ("schematic", "status-led", ["GND", "LED_A", "VCC"]),
        ("board", (20, 20)),
    ]
    assert result.ok


def test_project_stage_resource_lookup_reports_missing_and_wrong_type():
    project = volt.Project("status-led")

    @project.design
    def design():
        return _minimal_design(), volt.ProjectResource("nets", {"VCC": object()})

    @project.schematic
    def schematic(context):
        with pytest.raises(LookupError, match="resource named parts"):
            context.resource("parts")
        with pytest.raises(TypeError, match="resource nets must be a list"):
            context.resource("nets", list)
        return context.design().schematic("Main")

    result = project.run_through(project.schematic)

    assert result.schematic("Main").name == "Main"


def test_project_run_through_stops_after_stage_handle():
    project = volt.Project("status-led")
    calls = []

    @project.design
    def design():
        calls.append("design")
        return _minimal_design()

    @project.board
    def board(context):
        calls.append("board")
        return context.design().add_board("Main")

    result = project.run_through(project.design)

    assert calls == ["design"]
    assert result.design("status-led").name == "status-led"
    assert result.boards == ()
    assert [stage.name for stage in result.stages] == ["design"]


def test_project_result_reports_diagnostics_and_ok_state():
    project = volt.Project("bad-led")

    @project.design
    def design():
        d = volt.Design("bad-led")
        lonely = d.net("LONELY")
        r1 = d.R("1k", ref="R1")
        r1.dnp(True)
        lonely += r1[1]
        return d

    result = project.run()

    assert not result.ok
    diagnostics = tuple(result.diagnostics)
    assert {diagnostic.code for diagnostic in diagnostics} == {
        "UNCONNECTED_REQUIRED_PIN",
        "SINGLE_PIN_NET",
    }
    assert {diagnostic.stage for diagnostic in diagnostics} == {"design"}
    assert {diagnostic.source for diagnostic in diagnostics} == {"logical:bad-led"}


def test_logical_only_project_result_includes_exact_selected_part_erc():
    project = volt.Project("overvoltage")

    @project.design
    def design():
        return _overvoltage_exact_part_design()

    result = project.run_through(project.design)
    matches = [
        diagnostic
        for diagnostic in result.diagnostics
        if diagnostic.code == "SELECTED_PART_VOLTAGE_ABSOLUTE_LIMIT_VIOLATION"
    ]

    assert result.boards == ()
    assert len(matches) == 1
    assert matches[0].report == "logical.default"


def test_project_diagnostic_exposes_copyable_expectation_kwargs(tmp_path):
    project = volt.Project("bad-led")

    @project.design
    def design():
        d = volt.Design("bad-led")
        lonely = d.net("LONELY")
        r1 = d.R("1k", ref="R1")
        r1.dnp(True)
        lonely += r1[1]
        return d

    result = project.run()
    diagnostic = next(
        diagnostic
        for diagnostic in result.diagnostics
        if diagnostic.code == "SINGLE_PIN_NET"
    )

    assert diagnostic.expect_diagnostic_kwargs == {
        "code": "SINGLE_PIN_NET",
        "severity": "warning",
        "stage": "design",
        "source": "logical:bad-led",
        "report": "logical.default",
        "design": "bad-led",
    }

    accepting = volt.Project("bad-led")
    accepting.expect_diagnostic(diagnostic)
    accepting.expect_diagnostic(
        code="UNCONNECTED_REQUIRED_PIN", severity="error", stage="design"
    )

    @accepting.design
    def accepting_design():
        d = volt.Design("bad-led")
        lonely = d.net("LONELY")
        r1 = d.R("1k", ref="R1")
        r1.dnp(True)
        lonely += r1[1]
        return d

    accepted = accepting.run()
    assert accepted.ok
    assert accepted.expected_diagnostics[0].expect_diagnostic_kwargs == (
        diagnostic.expect_diagnostic_kwargs
    )

    bundle = tmp_path / "bundle.volt"
    result.write(bundle)
    manifest = json.loads((bundle / "manifest.volt.json").read_text(encoding="utf-8"))
    diagnostics_path = next(
        artifact["path"]
        for artifact in manifest["artifacts"]
        if artifact["kind"] == "diagnostics"
    )
    payload = json.loads((bundle / diagnostics_path).read_text(encoding="utf-8"))
    persisted_diagnostic = next(
        item for item in payload["diagnostics"] if item["code"] == "SINGLE_PIN_NET"
    )
    assert persisted_diagnostic["expect_diagnostic_kwargs"] == (
        diagnostic.expect_diagnostic_kwargs
    )


def test_project_expected_diagnostic_source_typo_is_visible():
    project = volt.Project("bad-led")
    project.expect_diagnostic(
        code="SINGLE_PIN_NET",
        severity="warning",
        stage="design",
        source="logical:typo",
    )

    @project.design
    def design():
        d = volt.Design("bad-led")
        lonely = d.net("LONELY")
        r1 = d.R("1k", ref="R1")
        r1.dnp(True)
        lonely += r1[1]
        return d

    result = project.run()

    assert not result.expected_diagnostics_ok
    assert [diagnostic.code for diagnostic in result.unexpected_diagnostics] == [
        "UNCONNECTED_REQUIRED_PIN",
        "SINGLE_PIN_NET",
    ]
    assert [item.expect_diagnostic_kwargs for item in result.missing_expected_diagnostics] == [
        {
            "code": "SINGLE_PIN_NET",
            "severity": "warning",
            "stage": "design",
            "source": "logical:typo",
        }
    ]


def test_project_expected_diagnostics_distinguish_expected_missing_and_unexpected():
    project = volt.Project("bad-led")
    project.expect_diagnostic(code="SINGLE_PIN_NET", severity="warning", stage="design")
    project.expect_diagnostic(code="STALE_EXPECTATION", severity="warning", stage="design")

    @project.design
    def design():
        d = volt.Design("bad-led")
        lonely = d.net("LONELY")
        r1 = d.R("1k", ref="R1")
        r1.dnp(True)
        lonely += r1[1]
        return d

    result = project.run()

    assert not result.ok
    assert not result.clean
    assert not result.expected_diagnostics_ok
    assert [(item.code, item.matched) for item in result.expected_diagnostics] == [
        ("SINGLE_PIN_NET", True),
        ("STALE_EXPECTATION", False),
    ]
    assert [diagnostic.code for diagnostic in result.expected_project_diagnostics] == [
        "SINGLE_PIN_NET",
    ]
    assert [diagnostic.code for diagnostic in result.unexpected_diagnostics] == [
        "UNCONNECTED_REQUIRED_PIN",
    ]
    assert [item.code for item in result.missing_expected_diagnostics] == [
        "STALE_EXPECTATION",
    ]


def test_project_expected_diagnostics_allow_success_with_expected_diagnostics():
    project = volt.Project("bad-led")
    project.expect_diagnostic(code="SINGLE_PIN_NET", severity="warning", stage="design")
    project.expect_diagnostic(code="UNCONNECTED_REQUIRED_PIN", severity="error", stage="design")

    @project.design
    def design():
        d = volt.Design("bad-led")
        lonely = d.net("LONELY")
        r1 = d.R("1k", ref="R1")
        r1.dnp(True)
        lonely += r1[1]
        return d

    result = project.run()

    assert result.ok
    assert not result.clean
    assert result.expected_diagnostics_ok
    assert {diagnostic.code for diagnostic in result.expected_project_diagnostics} == {
        "SINGLE_PIN_NET",
        "UNCONNECTED_REQUIRED_PIN",
    }
    assert result.unexpected_diagnostics == ()
    assert result.missing_expected_diagnostics == ()


def test_project_kicad_fab_critical_loss_fails_until_expected():
    project = volt.Project("status-led")

    @project.design
    def design():
        return _board_ready_design()

    @project.board
    def board(context):
        pcb = _stage_board(context.design())
        pcb.add(volt.Slot(start=(2.0, 2.0), end=(5.0, 2.0), width=1.0, role="mounting"))
        return pcb

    result = project.run()

    assert not result.ok
    diagnostic = next(
        diagnostic
        for diagnostic in result.diagnostics
        if diagnostic.code == "PCB_KICAD_FAB_EXPORT_LOSS"
    )
    assert diagnostic.severity == "error"
    assert diagnostic.category == "pcb.fabrication"
    assert diagnostic.stage == "board"
    assert diagnostic.source == "pcb:Main"
    assert diagnostic.report == "pcb.kicad_export"
    assert diagnostic.board == "Main"
    assert diagnostic.rule == "board.feature.slot"
    assert "board.feature.slot" in diagnostic.message

    project.expect_diagnostic(
        code="PCB_KICAD_FAB_EXPORT_LOSS",
        severity="error",
        stage="board",
        report="pcb.kicad_export",
        board="Main",
        rule="board.feature.slot",
    )
    expected = project.run()

    assert expected.ok
    assert expected.expected_diagnostics_ok
    assert expected.unexpected_diagnostics == ()


def test_project_expected_kicad_loss_construct_does_not_cover_another_loss():
    project = volt.Project("status-led")
    project.expect_diagnostic(
        code="PCB_KICAD_FAB_EXPORT_LOSS",
        severity="error",
        stage="board",
        report="pcb.kicad_export",
        board="Main",
        rule="board.feature.slot",
    )

    @project.design
    def design():
        return _board_ready_design()

    @project.board
    def board(context):
        pcb = _stage_board(context.design())
        front = pcb.add_layer("F.Cu", role="copper", side="top")
        nets = {net.name: net for net in context.design().nets()}
        pcb.add(volt.Slot(start=(2.0, 2.0), end=(5.0, 2.0), width=1.0, role="mounting"))
        pcb.add_zone(
            outline=((1.0, 1.0), (6.0, 1.0), (6.0, 4.0), (1.0, 4.0)),
            layers=(front,),
            net=nets["VCC"],
        )
        pcb.add_keepout(
            outline=((7.0, 1.0), (9.0, 1.0), (9.0, 3.0), (7.0, 3.0)),
            layers=(front,),
            restrictions=("copper",),
        )
        return pcb

    result = project.run()

    assert not result.ok
    assert [(item.code, item.matched) for item in result.expected_diagnostics] == [
        ("PCB_KICAD_FAB_EXPORT_LOSS", True)
    ]
    assert [diagnostic.rule for diagnostic in result.expected_project_diagnostics] == [
        "board.feature.slot"
    ]
    unexpected_kicad = [
        diagnostic
        for diagnostic in result.unexpected_diagnostics
        if diagnostic.code == "PCB_KICAD_FAB_EXPORT_LOSS"
    ]
    assert [diagnostic.rule for diagnostic in unexpected_kicad] == [
        "board.keepout"
    ]


def test_project_informational_kicad_loss_does_not_fail():
    project = volt.Project("status-led")

    @project.design
    def design():
        return _board_ready_design()

    @project.board
    def board(context):
        pcb = _stage_board(context.design())
        docs = pcb.add_layer("Documentation", role="mechanical", side="none")
        pcb.add_text("Assembly note", at=(2.0, 8.0), layer=docs)
        return pcb

    result = project.run()

    assert result.ok
    assert "PCB_KICAD_FAB_EXPORT_LOSS" not in {
        diagnostic.code for diagnostic in result.diagnostics
    }
    export = result.board("Main").to_kicad_pcb()
    assert [warning.fabrication_impact for warning in export.warnings] == [
        "informational"
    ]


def test_project_fabrication_layer_kicad_text_loss_fails():
    project = volt.Project("status-led")

    @project.design
    def design():
        return _board_ready_design()

    @project.board
    def board(context):
        pcb = _stage_board(context.design())
        fabrication = pcb.add_layer("FabNotes", role="fabrication", side="top")
        pcb.add_text("Fab note", at=(2.0, 8.0), layer=fabrication)
        return pcb

    result = project.run()

    assert not result.ok
    diagnostic = next(
        diagnostic
        for diagnostic in result.diagnostics
        if diagnostic.code == "PCB_KICAD_FAB_EXPORT_LOSS"
    )
    assert diagnostic.rule == "board.text.layer"
    export = result.board("Main").to_kicad_pcb()
    assert [warning.fabrication_impact for warning in export.warnings] == [
        "fab-critical"
    ]


def test_project_stage_tests_run_with_product_check_helpers():
    project = volt.Project("status-led")

    @project.design
    def design():
        return _minimal_design()

    @project.design.test
    def vcc_reaches_connector(check):
        check.net("VCC").connects("J1.1", "R1.1")
        check.net("GND").connects("J1.2", "D1.K")
        check.no_connection("VCC", "GND")

    result = project.run_through(project.design)

    assert result.ok
    assert [test.name for test in result.stage(project.design).tests] == [
        "vcc_reaches_connector",
    ]
    assert result.test_failures() == ()


def test_project_stage_test_failures_make_result_not_ok():
    project = volt.Project("status-led")

    @project.design
    def design():
        return _minimal_design()

    @project.design.test
    def wrong_connection(check):
        check.net("VCC").connects("J1.2")

    result = project.run_through(project.design)

    assert not result.ok
    failures = result.test_failures()
    assert len(failures) == 1
    assert failures[0].stage == "design"
    assert failures[0].name == "wrong_connection"
    assert "VCC" in failures[0].message
    assert "J1.2" in failures[0].message


def test_project_stage_tests_include_schematic_and_board_helpers():
    project = volt.Project("status-led")

    @project.design
    def design():
        return _board_ready_design()

    @project.schematic
    def schematic(context):
        return _stage_schematic(context.design())

    @project.board
    def board(context):
        return _stage_board(context.design())

    @project.schematic.test
    def schematic_places_parts(check):
        check.places("J1", "R1", "D1")

    @project.board.test
    def board_places_parts(check):
        check.has_outline()
        check.places("J1", "R1", "D1")

    result = project.run()

    assert result.ok
    assert result.test_failures() == ()


def test_project_result_write_emits_deterministic_bundle_across_working_roots(
    tmp_path, monkeypatch
):
    project = volt.Project("status-led", version="0.1.0", description="LED module")

    @project.design
    def design():
        return _board_ready_design()

    @project.schematic
    def schematic(context):
        return _stage_schematic(context.design())

    @project.board
    def board(context):
        return _stage_board(context.design())

    first = project.run()
    second = project.run()
    first.write(tmp_path / "first.volt")
    other_working_root = tmp_path / "other-working-root"
    other_working_root.mkdir()
    monkeypatch.chdir(other_working_root)
    second.write(tmp_path / "second.volt")

    first_bytes = {
        path.relative_to(tmp_path / "first.volt").as_posix(): path.read_bytes()
        for path in sorted((tmp_path / "first.volt").rglob("*"))
        if path.is_file()
    }
    second_bytes = {
        path.relative_to(tmp_path / "second.volt").as_posix(): path.read_bytes()
        for path in sorted((tmp_path / "second.volt").rglob("*"))
        if path.is_file()
    }

    assert first_bytes == second_bytes
    manifest = json.loads(first_bytes["manifest.volt.json"])
    assert manifest["format"] == "volt.project_result"
    assert manifest["schema_version"] == 2
    assert manifest["project"] == {
        "description": "LED module",
        "name": "status-led",
        "version": "0.1.0",
    }
    assert manifest["run"] == {
        "ok": True,
        "profile": "default",
        "stages": ["design", "schematic", "board"],
        "status": "clean",
    }
    assert manifest["export_selection"] == []
    assert manifest["build_id"].startswith("sha256:")
    assert manifest["bundle_digest"].startswith("sha256:")
    assert manifest["authoring_inputs"]["digest"].startswith("sha256:")
    assert len(manifest["dependency_lock"]["selected_parts"]) == 3
    kinds = [artifact["kind"] for artifact in manifest["artifacts"]]
    assert kinds.count("logical_model") == 1
    assert kinds.count("schematic_model") == 1
    assert kinds.count("board_model") == 1
    assert kinds.count("compiled_board") == 1
    assert kinds.count("board_scene") == 1
    assert kinds.count("diagnostics") == 1
    assert kinds.count("project_tests") == 1
    assert not {
        "schematic_svg",
        "board_svg",
        "kicad_pcb",
        "bom",
        "cpl",
    }.intersection(kinds)
    assert set(first_bytes) == {
        artifact["path"] for artifact in manifest["artifacts"]
    } | {"manifest.volt.json"}

    reports = {
        artifact["kind"]: json.loads(first_bytes[artifact["path"]])
        for artifact in manifest["artifacts"]
        if artifact["kind"] in {"diagnostics", "project_tests"}
    }
    assert reports["diagnostics"]["summary"] == {
        "errors": 0,
        "infos": 0,
        "warnings": 0,
    }
    assert reports["project_tests"]["summary"] == {"failed": 0, "passed": 0}


def test_project_result_write_retains_components_defined_after_native_parts(tmp_path):
    project = volt.Project("authoring-order")

    @project.design
    def design():
        result = volt.Design("authoring-order")
        parts = _native_fixture_parts()
        result.instantiate(parts["resistor"], ref="R1")
        result.instantiate(parts["led"], ref="D1").dnp(True)
        return result

    output = tmp_path / "bundle.volt"
    project.run().write(output)
    manifest = json.loads((output / "manifest.volt.json").read_text(encoding="utf-8"))

    assert len(manifest["dependency_lock"]["selected_parts"]) == 2
    assert [artifact["kind"] for artifact in manifest["artifacts"]].count(
        "component_definition"
    ) == 2


def test_project_result_write_preserves_expected_diagnostic_status(tmp_path):
    project = volt.Project("bad-led")
    project.expect_diagnostic(code="SINGLE_PIN_NET", severity="warning", stage="design")
    project.expect_diagnostic(code="UNCONNECTED_REQUIRED_PIN", severity="error", stage="design")

    @project.design
    def design():
        d = volt.Design("bad-led")
        lonely = d.net("LONELY")
        r1 = d.R("1k", ref="R1")
        r1.dnp(True)
        lonely += r1[1]
        return d

    result = project.run()
    output = tmp_path / "bundle.volt"
    result.write(output)

    manifest = json.loads((output / "manifest.volt.json").read_text(encoding="utf-8"))

    assert result.ok
    assert manifest["run"]["ok"] is True
    assert manifest["run"]["status"] == "expected-diagnostics"
    assert manifest["run"]["profile"] == "default"
    assert manifest["dependency_lock"]["selected_parts"] == []
    assert len(manifest["dependency_lock"]["libraries"]) == 1
    assert [artifact["kind"] for artifact in manifest["artifacts"]].count(
        "component_definition"
    ) == 1


def test_project_result_write_rejects_unprofiled_board(tmp_path):
    project = volt.Project("status-led")

    @project.design
    def design():
        return _board_ready_design()

    @project.board
    def board(context):
        pcb = context.design().add_board("Main")
        pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        pcb.place(context.design().component("J1"), at=(4, 5), locked=True)
        pcb.place(context.design().component("R1"), at=(10, 5))
        pcb.place(context.design().component("D1"), at=(15, 5), rotation=180)
        return pcb

    result = project.run()
    bundle = tmp_path / "status-led.volt"

    assert result.ok
    with pytest.raises(
        RuntimeError,
        match="CompiledBoard delivery requires one concrete Board capability profile",
    ):
        result.write(bundle)
    assert not bundle.exists()


def test_project_result_write_never_rewrites_historical_bundle(tmp_path):
    full = volt.Project("status-led")

    @full.design
    def full_design():
        return _board_ready_design()

    @full.schematic
    def full_schematic(context):
        return _stage_schematic(context.design())

    @full.board
    def full_board(context):
        return _stage_board(context.design())

    design_only = volt.Project("status-led")

    @design_only.design
    def design():
        return _minimal_design()

    output = tmp_path / "status-led.volt"
    full.run().write(output)
    before = {
        path.relative_to(output).as_posix(): path.read_bytes()
        for path in output.rglob("*")
        if path.is_file()
    }

    with pytest.raises(RuntimeError, match="destination already exists"):
        design_only.run().write(output)

    after = {
        path.relative_to(output).as_posix(): path.read_bytes()
        for path in output.rglob("*")
        if path.is_file()
    }
    assert after == before


def test_project_result_write_refuses_empty_existing_output_root(tmp_path):
    project = volt.Project("status-led")

    @project.design
    def design():
        return _minimal_design()

    output = tmp_path / "status-led.volt"
    output.mkdir()

    with pytest.raises(RuntimeError, match="destination already exists"):
        project.run().write(output)

    assert list(output.iterdir()) == []


def test_project_result_write_refuses_non_bundle_output_root(tmp_path):
    project = volt.Project("status-led")

    @project.design
    def design():
        return _minimal_design()

    output = tmp_path / "status-led.volt"
    (output / "logical").mkdir(parents=True)
    (output / "logical" / "notes.txt").write_text("do not delete\n", encoding="utf-8")

    with pytest.raises(RuntimeError, match="destination already exists"):
        project.run().write(output)

    assert (output / "logical" / "notes.txt").read_text(encoding="utf-8") == "do not delete\n"


def test_project_result_contains_multiple_boards():
    project = volt.Project("control-panel")

    @project.design
    def design():
        return (
            _board_ready_design("main-controller"),
            _board_ready_design("front-panel"),
        )

    @project.board
    def board(context):
        main = _stage_board(context.design("main-controller"))
        front_design = context.design("front-panel")
        front = front_design.add_board("FrontPanel")
        front.set_capability_profile(
            volt.CapabilityProfile(
                name="Project framework delivery fixture",
                source="Volt Python test fixture",
                as_of="2026-07-24",
                minimum_track_width=0.01,
                minimum_via_drill=0.01,
                minimum_via_annular=0.02,
            )
        )
        front.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        front.place(front_design.component("J1"), at=(4, 5), locked=True)
        front.place(front_design.component("R1"), at=(10, 5))
        front.place(front_design.component("D1"), at=(15, 5), rotation=180)
        return main, front

    result = project.run()

    assert [board._design.name for board in result.boards] == [
        "main-controller",
        "front-panel",
    ]
    assert result.board("main-controller:Main")._design.name == "main-controller"
    assert result.board("front-panel:FrontPanel")._design.name == "front-panel"
    assert result.board("Main")._design.name == "main-controller"


def test_project_result_contains_multiple_designs():
    project = volt.Project("kit")

    @project.design
    def design():
        return (_minimal_design("main-controller"), _minimal_design("debug-adapter"))

    result = project.run_through(project.design)

    assert [design.name for design in result.designs] == [
        "main-controller",
        "debug-adapter",
    ]
    assert result.design("main-controller").name == "main-controller"
    assert result.design("debug-adapter").name == "debug-adapter"


def test_project_result_disambiguates_schematics_by_design():
    project = volt.Project("kit")

    @project.design
    def design():
        return (_minimal_design("main-controller"), _minimal_design("debug-adapter"))

    @project.schematic
    def schematic(context):
        return (
            context.design("main-controller").schematic("Main"),
            context.design("debug-adapter").schematic("Main"),
        )

    result = project.run_through(project.schematic)

    assert result.schematic("main-controller:Main")._design.name == "main-controller"
    assert result.schematic("debug-adapter:Main")._design.name == "debug-adapter"


def test_project_result_artifact_manifest_groups_outputs_by_board(tmp_path):
    project = volt.Project("control-panel")

    @project.design
    def design():
        return _board_ready_design("control-panel")

    @project.board
    def board(context):
        design = context.design()
        main = _stage_board(design)
        alternate = design.add_board("Alternate")
        alternate.set_capability_profile(
            volt.CapabilityProfile(
                name="Project framework delivery fixture",
                source="Volt Python test fixture",
                as_of="2026-07-24",
                minimum_track_width=0.01,
                minimum_via_drill=0.01,
                minimum_via_annular=0.02,
            )
        )
        alternate.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        alternate.place(design.component("J1"), at=(4, 5), locked=True)
        alternate.place(design.component("R1"), at=(10, 5))
        alternate.place(design.component("D1"), at=(15, 5), rotation=180)
        return main, alternate

    project.run().write(tmp_path / "control-panel.volt")

    manifest = json.loads(
        (tmp_path / "control-panel.volt" / "manifest.volt.json").read_text(
            encoding="utf-8"
        )
    )
    board_owners = [
        artifact["id"]["owner"]["value"]
        for artifact in manifest["artifacts"]
        if artifact["kind"] == "board_model"
    ]

    assert board_owners == [
        {"board": "Alternate", "design": "control-panel"},
        {"board": "Main", "design": "control-panel"},
    ]


def test_project_diagnostics_preserve_board_and_design_identity():
    project = volt.Project("fixture")

    @project.design
    def design():
        return _board_ready_design("fixture")

    @project.board
    def board(context):
        return context.design().add_board("Fixture")

    result = project.run()

    diagnostics = tuple(result.diagnostics)
    board_diagnostics = [
        diagnostic for diagnostic in diagnostics if diagnostic.board == "Fixture"
    ]

    assert board_diagnostics
    assert {diagnostic.design for diagnostic in board_diagnostics} == {"fixture"}
    assert {diagnostic.source for diagnostic in board_diagnostics} == {"pcb:Fixture"}


def test_project_projection_lookup_escapes_ambiguous_names():
    project = volt.Project("kit")

    @project.design
    def design():
        return (_board_ready_design("main:controller"), _board_ready_design("main"))

    @project.board
    def board(context):
        first = _stage_board(context.design("main:controller"))
        second = context.design("main").add_board("controller:Main")
        second.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        return (first, second)

    result = project.run()

    assert result.board("main~1controller:Main")._design.name == "main:controller"
    assert result.board("main:controller~1Main").name == "controller:Main"


def test_project_diagnostics_include_registered_library_identity():
    library = volt.Library("volt.test.parts")
    library.add(volt.Part(name="Empty", pins=()))
    project = volt.Project("status-led")
    project.use_library(library)

    @project.design
    def design():
        return _board_ready_design()

    result = project.run_through(project.design)

    diagnostic = result.diagnostics.errors(stage="library")[0]
    assert diagnostic.source == "part:Empty"
    assert json.loads(diagnostic.report.removeprefix("library:")) == {
        "library_bundle_digest": library.build().digest,
        "namespace": "volt.test.parts",
        "version": "1.0.0",
    }
    assert diagnostic.code == "LIBRARY_PART_MISSING_PINS"


def test_project_bundle_scopes_library_diagnostics_to_selected_parts(tmp_path):
    library = volt.Library("volt.test.parts")
    library.add(
        volt.Part(
            name="Unused",
            pins=(volt.PinSpec("A", 1), volt.PinSpec("B", 2)),
            footprint=volt.Footprint(
                ("volt.test.parts", "Overlap"),
                pads=(
                    volt.FootprintPad.surface_mount(
                        "1", at=(0.0, 0.0), size=(1.0, 1.0)
                    ),
                    volt.FootprintPad.surface_mount(
                        "2", at=(0.0, 0.0), size=(1.0, 1.0)
                    ),
                ),
            ),
            pads={"A": "1", "B": "2"},
            manufacturer="Volt",
            mpn="UNUSED",
            package="test",
        )
    )
    project = volt.Project("selected-library-scope")
    project.use_library(library)

    @project.design
    def design():
        return volt.Design("main")

    result = project.run_through(project.design)
    assert [
        diagnostic.code
        for diagnostic in result.diagnostics
        if diagnostic.stage == "library"
    ] == ["PART_PAD_OVERLAP"]

    path = tmp_path / "selected-library-scope.volt"
    result.write(path)
    bundle = volt.ProjectBundle.open(path)
    report = json.loads(bundle.graph.loaded_project.diagnostics.bytes)

    assert report["status"] == "clean"
    assert report["diagnostics"] == []
    assert report["summary"] == {"errors": 0, "infos": 0, "warnings": 0}


def test_project_bundle_scopes_library_diagnostics_by_exact_release_and_part_key(
    tmp_path,
):
    def exact_part(name, source_name, footprint_name):
        return volt.Part(
            name=name,
            source_name=source_name,
            pins=(volt.PinSpec("A", 1),),
            footprint=volt.Footprint(
                ("volt.test.release", footprint_name),
                pads=(
                    volt.FootprintPad.surface_mount(
                        "1", at=(0.0, 0.0), size=(1.0, 1.0)
                    ),
                ),
            ),
            pads={"A": "1"},
            manufacturer="Volt",
            mpn=name.upper(),
            package="test",
        )

    first_release = volt.Library("volt.test.release", version="1")
    first = first_release.add(exact_part("Selected", "B", "First"))
    unused_release = volt.Library("volt.test.release", version="2")
    unused_release.add(volt.Part(name="B", source_name="unused-key", pins=()))
    third_release = volt.Library("volt.test.release", version="3")
    third = third_release.add(exact_part("Other", "C", "Third"))

    project = volt.Project("exact-library-report-scope")
    project.use_library(first_release)
    project.use_library(unused_release)
    project.use_library(third_release)

    @project.design
    def design():
        first_design = volt.Design("first")
        first_design.instantiate(first, ref="U1")
        third_design = volt.Design("third")
        third_design.instantiate(third, ref="U1")
        return (first_design, third_design)

    result = project.run_through(project.design)
    unused = [
        diagnostic
        for diagnostic in result.diagnostics
        if diagnostic.code == "LIBRARY_PART_MISSING_PINS"
    ]
    assert len(unused) == 1
    assert unused[0].source == "part:unused-key"
    assert json.loads(unused[0].report.removeprefix("library:"))["version"] == "2"

    path = tmp_path / "exact-library-report-scope.volt"
    result.write(path)
    bundle = volt.ProjectBundle.open(path)
    report = json.loads(bundle.graph.loaded_project.diagnostics.bytes)

    assert all(
        diagnostic["source"] != "part:unused-key"
        for diagnostic in report["diagnostics"]
    )
    library_errors = result.diagnostics.errors(stage="library")
    assert {diagnostic.source for diagnostic in library_errors} == {"part:unused-key"}
    assert report["summary"]["errors"] == (
        len(result.diagnostics.errors()) - len(library_errors)
    )
    assert {
        (row["library"], row["version"])
        for row in bundle.graph.dependency_lock["libraries"]
    } == {("volt.test.release", "1"), ("volt.test.release", "3")}


def test_two_board_project_fixture_writes_deterministic_bundle(tmp_path):
    project = volt.Project("control-panel")

    @project.design
    def design():
        return (
            _board_ready_design("main-controller"),
            _board_ready_design("front-panel"),
        )

    @project.board
    def board(context):
        main = _stage_board(context.design("main-controller"))
        front_design = context.design("front-panel")
        front = front_design.add_board("FrontPanel")
        front.set_capability_profile(
            volt.CapabilityProfile(
                name="Project framework delivery fixture",
                source="Volt Python test fixture",
                as_of="2026-07-24",
                minimum_track_width=0.01,
                minimum_via_drill=0.01,
                minimum_via_annular=0.02,
            )
        )
        front.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        front.place(front_design.component("J1"), at=(4, 5), locked=True)
        front.place(front_design.component("R1"), at=(10, 5))
        front.place(front_design.component("D1"), at=(15, 5), rotation=180)
        return main, front

    first = project.run()
    second = project.run()
    first.write(tmp_path / "first.volt")
    second.write(tmp_path / "second.volt")

    first_bytes = {
        path.relative_to(tmp_path / "first.volt").as_posix(): path.read_bytes()
        for path in sorted((tmp_path / "first.volt").rglob("*"))
        if path.is_file()
    }
    second_bytes = {
        path.relative_to(tmp_path / "second.volt").as_posix(): path.read_bytes()
        for path in sorted((tmp_path / "second.volt").rglob("*"))
        if path.is_file()
    }

    assert first_bytes == second_bytes
    manifest = json.loads(first_bytes["manifest.volt.json"])
    kinds = [artifact["kind"] for artifact in manifest["artifacts"]]
    assert kinds.count("logical_model") == 2
    assert kinds.count("board_model") == 2
    assert kinds.count("compiled_board") == 2
    assert kinds.count("board_scene") == 2
    assert manifest["export_selection"] == []
    assert set(first_bytes) == {
        artifact["path"] for artifact in manifest["artifacts"]
    } | {"manifest.volt.json"}
