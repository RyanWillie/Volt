import json

import pytest

import volt
from project_framework_helpers import (
    _board_ready_design,
    _minimal_design,
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
        pcb = context.design().board("Main")
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
        return context.design().board("Main")

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


def test_project_expected_diagnostics_distinguish_expected_missing_and_unexpected():
    project = volt.Project("bad-led")
    project.expect_diagnostic(code="SINGLE_PIN_NET", severity="warning", stage="design")
    project.expect_diagnostic(code="STALE_EXPECTATION", severity="warning", stage="design")

    @project.design
    def design():
        d = volt.Design("bad-led")
        lonely = d.net("LONELY")
        r1 = d.R("1k", ref="R1")
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


def test_project_result_write_emits_deterministic_bundle(tmp_path):
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
    second.write(tmp_path / "second.volt")

    first_texts = {
        path.relative_to(tmp_path / "first.volt").as_posix(): path.read_text(
            encoding="utf-8"
        )
        for path in sorted((tmp_path / "first.volt").rglob("*"))
        if path.is_file()
    }
    second_texts = {
        path.relative_to(tmp_path / "second.volt").as_posix(): path.read_text(
            encoding="utf-8"
        )
        for path in sorted((tmp_path / "second.volt").rglob("*"))
        if path.is_file()
    }

    assert first_texts == second_texts
    manifest = json.loads(first_texts["manifest.volt.json"])
    assert manifest["format"] == "volt.project_result"
    assert manifest["schema_version"] == 1
    assert manifest["project"] == {
        "description": "LED module",
        "name": "status-led",
        "version": "0.1.0",
    }
    assert manifest["ok"] is True
    assert manifest["diagnostics"]["summary"] == {
        "errors": 0,
        "infos": 0,
        "warnings": 0,
    }
    assert manifest["tests"]["summary"] == {"failed": 0, "passed": 0}
    assert [artifact["path"] for artifact in manifest["artifacts"]] == [
        "logical/status-led.volt.json",
        "schematic/Main.volt.schematic.json",
        "schematic/Main.svg",
        "schematic/Main.body.svg",
        "schematic/Main.pages/Main.svg",
        "pcb/Main.volt.pcb.json",
        "pcb/Main.svg",
        "pcb/Main.kicad_pcb",
        "diagnostics/diagnostics.json",
        "diagnostics/tests.json",
    ]
    assert set(first_texts) == {
        "diagnostics/diagnostics.json",
        "diagnostics/tests.json",
        "logical/status-led.volt.json",
        "manifest.volt.json",
        "pcb/Main.kicad_pcb",
        "pcb/Main.svg",
        "pcb/Main.volt.pcb.json",
        "schematic/Main.body.svg",
        "schematic/Main.pages/Main.svg",
        "schematic/Main.svg",
        "schematic/Main.volt.schematic.json",
    }


def test_project_result_write_flat_artifacts_emits_legacy_example_outputs(tmp_path):
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

    artifacts = project.run().write_artifacts(tmp_path, slug="status_led")

    assert artifacts.logical_json == tmp_path / "status_led.volt.json"
    assert artifacts.schematic_json == tmp_path / "status_led.volt.schematic.json"
    assert artifacts.schematic_svg == tmp_path / "status_led.svg"
    assert artifacts.schematic_body_svg == tmp_path / "status_led.body.svg"
    assert artifacts.schematic_svg_pages == (tmp_path / "status_led.pages" / "status_led_Main.svg",)
    assert artifacts.pcb_json == tmp_path / "status_led.volt.pcb.json"
    assert artifacts.pcb_svg == tmp_path / "status_led.pcb.svg"
    assert artifacts.kicad_pcb == tmp_path / "status_led.kicad_pcb"
    assert artifacts.diagnostics_json == tmp_path / "status_led.validation.json"
    assert artifacts.schematic_svg.read_text(encoding="utf-8").startswith("<svg")
    assert artifacts.kicad_pcb.read_text(encoding="utf-8").startswith("(kicad_pcb\n")
    diagnostics = json.loads(artifacts.diagnostics_json.read_text(encoding="utf-8"))
    assert diagnostics["status"] == "clean"
    assert diagnostics["summary"] == {"errors": 0, "infos": 0, "warnings": 0}


def test_project_result_write_cleans_stale_bundle_artifacts(tmp_path):
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
    assert (output / "pcb" / "Main.volt.pcb.json").exists()
    assert (output / "schematic" / "Main.volt.schematic.json").exists()

    design_only.run().write(output)

    assert not (output / "pcb").exists()
    assert not (output / "schematic").exists()
    manifest = json.loads((output / "manifest.volt.json").read_text(encoding="utf-8"))
    assert [artifact["path"] for artifact in manifest["artifacts"]] == [
        "logical/status-led.volt.json",
        "diagnostics/diagnostics.json",
        "diagnostics/tests.json",
    ]


def test_project_result_write_allows_empty_existing_output_root(tmp_path):
    project = volt.Project("status-led")

    @project.design
    def design():
        return _minimal_design()

    output = tmp_path / "status-led.volt"
    output.mkdir()

    project.run().write(output)

    assert (output / "manifest.volt.json").exists()


def test_project_result_write_refuses_non_bundle_output_root(tmp_path):
    project = volt.Project("status-led")

    @project.design
    def design():
        return _minimal_design()

    output = tmp_path / "status-led.volt"
    (output / "logical").mkdir(parents=True)
    (output / "logical" / "notes.txt").write_text("do not delete\n", encoding="utf-8")

    with pytest.raises(FileExistsError, match="not an existing Volt project-result bundle"):
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
        return (
            _stage_board(context.design("main-controller")),
            _stage_board(context.design("front-panel")),
        )

    result = project.run()

    assert [board._design.name for board in result.boards] == [
        "main-controller",
        "front-panel",
    ]
    assert result.board("main-controller:Main")._design.name == "main-controller"
    assert result.board("front-panel:Main")._design.name == "front-panel"


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
        return (
            _board_ready_design("main-controller"),
            _board_ready_design("front-panel"),
        )

    @project.board
    def board(context):
        return (
            _stage_board(context.design("main-controller")),
            _stage_board(context.design("front-panel")),
        )

    project.run().write(tmp_path / "control-panel.volt")

    manifest = json.loads(
        (tmp_path / "control-panel.volt" / "manifest.volt.json").read_text(
            encoding="utf-8"
        )
    )
    pcb_artifacts = [
        artifact for artifact in manifest["artifacts"] if artifact["kind"] == "pcb"
    ]

    assert [
        (artifact["name"], artifact["group"])
        for artifact in pcb_artifacts
    ] == [
        ("main-controller:Main", {"design": "main-controller", "board": "Main"}),
        ("front-panel:Main", {"design": "front-panel", "board": "Main"}),
    ]


def test_project_diagnostics_preserve_board_and_design_identity():
    project = volt.Project("fixture")

    @project.design
    def design():
        return _board_ready_design("fixture")

    @project.board
    def board(context):
        return context.design().board("Fixture")

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
        second = context.design("main").board("controller:Main")
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
    assert diagnostic.report == "library:volt.test.parts"
    assert diagnostic.code == "LIBRARY_PART_MISSING_PINS"


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
        return (
            _stage_board(context.design("main-controller")),
            _stage_board(context.design("front-panel")),
        )

    first = project.run()
    second = project.run()
    first.write(tmp_path / "first.volt")
    second.write(tmp_path / "second.volt")

    first_texts = {
        path.relative_to(tmp_path / "first.volt").as_posix(): path.read_text(
            encoding="utf-8"
        )
        for path in sorted((tmp_path / "first.volt").rglob("*"))
        if path.is_file()
    }
    second_texts = {
        path.relative_to(tmp_path / "second.volt").as_posix(): path.read_text(
            encoding="utf-8"
        )
        for path in sorted((tmp_path / "second.volt").rglob("*"))
        if path.is_file()
    }

    assert first_texts == second_texts
    assert set(first_texts) == {
        "diagnostics/diagnostics.json",
        "diagnostics/tests.json",
        "logical/front-panel.volt.json",
        "logical/main-controller.volt.json",
        "manifest.volt.json",
        "pcb/front-panel-Main.kicad_pcb",
        "pcb/front-panel-Main.svg",
        "pcb/front-panel-Main.volt.pcb.json",
        "pcb/main-controller-Main.kicad_pcb",
        "pcb/main-controller-Main.svg",
        "pcb/main-controller-Main.volt.pcb.json",
    }
