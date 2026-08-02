def indicator_topology(check):
    check.net("+5V").connects("J1.+5V", "R1.1")
    check.net("LED_A").connects("R1.2", "D1.A")
    check.net("GND").connects("D1.K", "J1.GND")
    check.no_connection("+5V", "GND")


def schematic_is_complete(check):
    check.places("J1", "R1", "D1")


def board_is_complete(check):
    check.has_outline()
    check.places("J1", "R1", "D1")


def register_project_tests(project):
    project.design.test(indicator_topology)
    project.schematic.test(schematic_is_complete)
    project.board.test(board_is_complete)
