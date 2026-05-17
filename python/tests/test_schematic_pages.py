import json

import volt


def test_python_schematic_page_metadata_and_regions_are_kernel_owned():
    design = volt.Design("schematic-pages")
    vcc = design.net("VCC", kind="power")
    r1 = design.R("10k", ref="R1")
    logical_before = design.to_json()

    sheet = design.schematic(
        "Main",
        size="A4",
        orientation="landscape",
        title="STM32 USB Buck Board",
        number="1/1",
        revision="2.0",
        margins=(12, 10, 12, 10),
        coordinate_zones=(10, 6),
        grid=2.5,
    )
    power = sheet.region(
        "Power Circuitry",
        x=10,
        y=12,
        w=260,
        h=55,
        style={"accent": "orange"},
    )

    with power.drawing(at=(1, 2), unit=5) as drawing:
        placed = drawing.place(r1, symbol="resistor")
    with power.drawing(at=(0, 10), unit=5) as drawing:
        drawing.wire(vcc).to((10, 10)).direct()
    power.wire(vcc, [(0, 0), (20, 0)])
    power.label(vcc, at=(20, 3))

    projection = json.loads(sheet.to_json())

    assert placed.pin_anchor(1) == (11.0, 14.0)
    assert projection["sheets"][0]["metadata"] == {
        "title": "STM32 USB Buck Board",
        "orientation": "Landscape",
        "size": {"width": 297.0, "height": 210.0},
        "title_block": [
            {"key": "Number", "value": "1/1"},
            {"key": "Revision", "value": "2.0"},
        ],
        "frame": {
            "visible": True,
            "margins": {"left": 12.0, "top": 10.0, "right": 12.0, "bottom": 10.0},
        },
        "coordinate_zones": {"columns": 10, "rows": 6, "visible": True},
        "grid": {"spacing": 2.5, "visible": True},
    }
    assert projection["sheets"][0]["regions"] == [
        {
            "name": "Power Circuitry",
            "title": "Power Circuitry",
            "bounds": {"x": 10.0, "y": 12.0, "width": 260.0, "height": 55.0},
            "style": {"accent": "orange"},
        }
    ]
    assert projection["symbol_instances"][0]["position"] == {"x": 11.0, "y": 14.0}
    assert projection["symbol_instances"][0]["authored_region"] == "Power Circuitry"
    assert projection["wire_runs"][0]["points"] == [
        {"x": 10.0, "y": 22.0},
        {"x": 20.0, "y": 22.0},
    ]
    assert projection["wire_runs"][0]["authored_region"] == "Power Circuitry"
    assert projection["wire_runs"][1]["points"] == [
        {"x": 10.0, "y": 12.0},
        {"x": 30.0, "y": 12.0},
    ]
    assert projection["wire_runs"][1]["authored_region"] == "Power Circuitry"
    assert projection["net_labels"][0]["position"] == {"x": 30.0, "y": 15.0}
    assert projection["net_labels"][0]["authored_region"] == "Power Circuitry"
    assert json.loads(design.load_schematic_json(sheet.to_json()).to_json()) == projection
    assert design.to_json() == logical_before


def test_python_schematic_sheet_backwards_compatibility_keeps_default_page():
    design = volt.Design("schematic-backwards")

    sheet = design.schematic("Legacy")

    projection = json.loads(sheet.to_json())
    assert sheet.name == "Legacy"
    assert projection["sheets"][0]["metadata"]["title"] == "Legacy"
    assert projection["sheets"][0]["metadata"]["orientation"] == "Landscape"
    assert projection["sheets"][0]["metadata"]["size"] == {"width": 297.0, "height": 210.0}
    assert projection["sheets"][0]["regions"] == []


def test_python_schematic_visibility_flags_require_booleans():
    design = volt.Design("schematic-page-visibility")

    invalid_cases = [
        (
            "coordinate_zones",
            {"columns": 10, "rows": 6, "visible": "false"},
            "Coordinate zone visibility must be a boolean",
        ),
        (
            "grid",
            {"spacing": 2.5, "visible": 1},
            "Schematic grid visibility must be a boolean",
        ),
    ]
    for field, value, message in invalid_cases:
        try:
            design.schematic("Main", **{field: value})
        except TypeError as error:
            assert message in str(error)
        else:
            raise AssertionError(f"expected non-boolean {field} visibility to be rejected")

    sheet = design.schematic(
        "Main",
        coordinate_zones={"columns": 10, "rows": 6, "visible": False},
        grid={"spacing": 2.5, "visible": False},
    )
    projection = json.loads(sheet.to_json())
    assert projection["sheets"][0]["metadata"]["coordinate_zones"] == {
        "columns": 10,
        "rows": 6,
        "visible": False,
    }
    assert projection["sheets"][0]["metadata"]["grid"] == {
        "spacing": 2.5,
        "visible": False,
    }


def test_python_schematic_regions_require_unique_name_per_sheet():
    design = volt.Design("schematic-region-uniqueness")
    sheet = design.schematic("Main")

    first = sheet.region("Power", x=10, y=12, w=260, h=55)
    same = sheet.region("Power", x=10, y=12, w=260, h=55)
    assert same.index == first.index

    try:
        sheet.region("Power", x=12, y=12, w=260, h=55)
    except ValueError as error:
        assert "already exists with different metadata" in str(error)
    else:
        raise AssertionError("expected duplicate region names with different bounds to be rejected")
