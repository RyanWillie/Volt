import json

import volt

from project_framework_helpers import _board_ready_design, _minimal_design, _stage_board


def test_project_single_model_stage_tests_keep_stable_wrapper_helpers():
    project = volt.Project("status-led")

    @project.design
    def design():
        return _minimal_design()

    @project.design.test
    def direct_helpers_still_work(check):
        assert check.names() == ("status-led",)
        assert [design.name for design in check.designs()] == ["status-led"]
        check.design().net("VCC").connects("J1.1", "R1.1")
        check.design("status-led").net("GND").connects("J1.2", "D1.K")
        check.net("VCC").connects("J1.1", "R1.1")
        check.no_connection("VCC", "GND")

    result = project.run_through(project.design)

    assert result.ok
    assert result.test_failures() == ()


def test_project_multi_design_stage_tests_support_explicit_multi_model_helpers():
    project = volt.Project("kit")

    @project.design
    def design():
        return (_minimal_design("main-controller"), _minimal_design("debug-adapter"))

    @project.design.test
    def rails_are_present(check):
        assert check.names() == ("main-controller", "debug-adapter")
        check.design("main-controller").net("VCC").connects("J1.1", "R1.1")
        for design in check.designs():
            design.net("GND").connects("J1.2", "D1.K")
            design.no_connection("VCC", "GND")

    result = project.run_through(project.design)

    assert result.ok
    assert result.stage(project.design).model_count == 2
    assert [test.name for test in result.stage(project.design).tests] == [
        "rails_are_present",
    ]


def test_project_multi_model_stage_tests_identify_the_relevant_model():
    project = volt.Project("kit")

    @project.design
    def design():
        return (_minimal_design("main-controller"), _minimal_design("debug-adapter"))

    @project.design.test
    def debug_adapter_has_wrong_pin(check):
        check.design("debug-adapter").net("VCC").connects("J1.2")

    result = project.run_through(project.design)

    assert not result.ok
    failure = result.test_failures()[0]
    assert failure.stage == "design"
    assert failure.name == "debug_adapter_has_wrong_pin"
    assert "debug-adapter" in failure.message
    assert "VCC" in failure.message
    assert "J1.2" in failure.message


def test_project_multi_model_stage_tests_share_projection_lookup_rules():
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

    @project.board.test
    def outlines_are_addressable(check):
        assert check.names() == ("main~1controller:Main", "main:controller~1Main")
        check.board("Main").has_outline()
        check.board("controller:Main").has_outline()
        check.board("main~1controller:Main").has_outline()
        check.board("main:controller~1Main").has_outline()

    result = project.run()

    assert result.stage(project.board).tests[0].ok
    assert result.test_failures() == ()


def test_project_multi_model_stage_tests_keep_deterministic_order_in_results(tmp_path):
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

    @project.board.test
    def every_board_has_an_outline(check):
        assert check.names() == ("main-controller:Main", "front-panel:Main")
        for board in check.boards():
            board.has_outline()

    first = project.run()
    second = project.run()
    first.write(tmp_path / "first.volt")
    second.write(tmp_path / "second.volt")

    first_tests = json.loads(
        (tmp_path / "first.volt" / "diagnostics" / "tests.json").read_text(
            encoding="utf-8"
        )
    )
    second_tests = json.loads(
        (tmp_path / "second.volt" / "diagnostics" / "tests.json").read_text(
            encoding="utf-8"
        )
    )
    first_manifest = json.loads(
        (tmp_path / "first.volt" / "manifest.volt.json").read_text(encoding="utf-8")
    )
    second_manifest = json.loads(
        (tmp_path / "second.volt" / "manifest.volt.json").read_text(encoding="utf-8")
    )

    assert first_tests == second_tests
    assert first_manifest["stages"] == second_manifest["stages"]
    assert first_manifest["stages"] == [
        {"name": "design", "model_count": 2, "tests": []},
        {
            "name": "board",
            "model_count": 2,
            "tests": [
                {
                    "stage": "board",
                    "name": "every_board_has_an_outline",
                    "ok": True,
                    "message": "",
                }
            ],
        },
    ]
