import json

import pytest

import volt


def _passive_0603(ref):
    return volt.FootprintDefinition(
        ref,
        pads=(
            volt.FootprintPad.surface_mount(
                "1",
                at=(-0.75, 0.0),
                size=(0.8, 0.95),
                shape="rounded_rectangle",
            ),
            volt.FootprintPad.surface_mount(
                "2",
                at=(0.75, 0.0),
                size=(0.8, 0.95),
                shape="rounded_rectangle",
            ),
        ),
    )


def _header_1x02():
    return volt.FootprintDefinition(
        ("connectors", "PinHeader_1x02_P2.54mm"),
        pads=(
            volt.FootprintPad.through_hole(
                "1",
                at=(0.0, -1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
            ),
            volt.FootprintPad.through_hole(
                "2",
                at=(0.0, 1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
            ),
        ),
    )


def _minimal_design(name="status-led"):
    design = volt.Design(name)
    vcc = design.net("VCC", kind="power")
    led_a = design.net("LED_A")
    gnd = design.net("GND", kind="ground")
    j1 = design.connector_1x02(ref="J1")
    r1 = design.R("330", ref="R1")
    d1 = design.LED(ref="D1")
    vcc += j1[1], r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"], j1[2]
    return design


def _board_ready_design(name="status-led"):
    design = _minimal_design(name)
    design.component("J1").select_part(
        manufacturer="Generic",
        part_number="HDR-1x02",
        package="2.54mm-1x02",
        footprint=_header_1x02(),
        pin_pads={1: "1", 2: "2"},
    )
    design.component("R1").select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-07330RL",
        package="0603",
        footprint=_passive_0603(("passives", "R_0603_1608Metric")),
        pin_pads={1: "1", 2: "2"},
    )
    design.component("D1").select_part(
        manufacturer="Lite-On",
        part_number="LTST-C190KRKT",
        package="0603",
        footprint=_passive_0603(("leds", "LED_0603_1608Metric")),
        pin_pads={"A": "1", "K": "2"},
    )
    return design


def _stage_schematic(design):
    sheet = design.schematic("Main", size=(220, 150), margins=(8, 8, 8, 8))
    nets = {net.name: net for net in design.nets()}
    vcc = nets["VCC"]
    led_a = nets["LED_A"]
    gnd = nets["GND"]

    with sheet.drawing(unit=20) as drawing:
        header = drawing.place(
            design.component("J1"),
            at=(45, 60),
            orient="Right",
        ).label_ref(loc="left")
        resistor = (
            drawing.two_terminal(design.component("R1"))
            .at(header[1].right(35))
            .right()
            .label_ref(loc="top", offset=6)
            .label_value(loc="bottom", offset=10)
        )
        led = (
            drawing.two_terminal(design.component("D1"))
            .at(resistor.end.right(18))
            .right()
            .reverse()
            .label_ref(loc="top", offset=8)
        )
        supply = drawing.power("VCC", net=vcc, at=header[1].up(18))
        header_ground = drawing.ground("GND", net=gnd, at=header[2].down(18))
        led_ground = drawing.ground("GND", net=gnd, at=led.end.down(24))

        drawing.connect(supply.pin, header[1], net=vcc, shape="-")
        drawing.connect(header[1], resistor.start, net=vcc, shape="-").dot()
        drawing.connect(resistor.end, led.start, net=led_a, shape="-")
        drawing.connect(header[2], header_ground.pin, net=gnd, shape="-")
        drawing.connect(led.end, led_ground.pin, net=gnd, shape="-")
        drawing.net_label(led_a, at=resistor.end.up(12))

    return sheet


def _stage_board(design):
    pcb = design.board("Main")
    pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
    pcb.place(design.component("J1"), at=(4, 5), locked=True)
    pcb.place(design.component("R1"), at=(10, 5))
    pcb.place(design.component("D1"), at=(15, 5), rotation=180)
    return pcb


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
    def schematic(design):
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
    def schematic(design):
        calls.append(("schematic", design.name))
        return design.schematic("Main")

    @project.board
    def board(design):
        calls.append(("board", design.name))
        pcb = design.board("Main")
        pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        return pcb

    result = project.run()

    assert calls == ["design", ("schematic", "status-led"), ("board", "status-led")]
    assert result.design("status-led").name == "status-led"
    assert result.schematic("Main").name == "Main"
    assert result.board("Main").name == "Main"
    assert [stage.name for stage in result.stages] == ["design", "schematic", "board"]


def test_project_run_through_stops_after_stage_handle():
    project = volt.Project("status-led")
    calls = []

    @project.design
    def design():
        calls.append("design")
        return _minimal_design()

    @project.board
    def board(design):
        calls.append("board")
        return design.board("Main")

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
    def schematic(design):
        return _stage_schematic(design)

    @project.board
    def board(design):
        return _stage_board(design)

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
    def schematic(design):
        return _stage_schematic(design)

    @project.board
    def board(design):
        return _stage_board(design)

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
        "pcb/Main.volt.pcb.json",
        "pcb/Main.svg",
        "diagnostics/diagnostics.json",
        "diagnostics/tests.json",
    ]
    assert set(first_texts) == {
        "diagnostics/diagnostics.json",
        "diagnostics/tests.json",
        "logical/status-led.volt.json",
        "manifest.volt.json",
        "pcb/Main.svg",
        "pcb/Main.volt.pcb.json",
        "schematic/Main.svg",
        "schematic/Main.volt.schematic.json",
    }


def test_project_result_write_cleans_stale_bundle_artifacts(tmp_path):
    full = volt.Project("status-led")

    @full.design
    def full_design():
        return _board_ready_design()

    @full.schematic
    def full_schematic(design):
        return _stage_schematic(design)

    @full.board
    def full_board(design):
        return _stage_board(design)

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
