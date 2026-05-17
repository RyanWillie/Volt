import json
from pathlib import Path
from tempfile import TemporaryDirectory

import volt


def test_python_schematic_json_round_trips_as_design_document():
    design = volt.Design("schematic-round-trip")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")

    schematic = design.schematic("Main")
    schematic.place(r1, at=(40, 20), symbol="resistor")
    schematic.wire(vcc, [(20, 20), (40, 20)])
    schematic.label(vcc, at=(20, 16))

    json_text = schematic.to_json()

    loaded = design.load_schematic_json(json_text)

    assert loaded.name == "Main"
    assert json.loads(loaded.to_json()) == json.loads(json_text)
    assert not loaded.validate().has_errors
    assert '<polyline class="wire-run" data-net="net:0" points="20,20 40,20"/>' in loaded.to_svg()

    with TemporaryDirectory() as directory:
        path = Path(directory) / "schematic.volt.json"
        loaded.write_json(path)
        assert json.loads(path.read_text(encoding="utf-8")) == json.loads(json_text)
        reloaded = design.load_schematic(path)
        assert json.loads(reloaded.to_json()) == json.loads(json_text)

def test_python_schematic_json_load_rejects_stale_logical_ids():
    design = volt.Design("schematic-stale-reference")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")

    schematic = design.schematic("Main")
    schematic.place(r1, at=(40, 20), symbol="resistor")
    schematic.wire(vcc, [(20, 20), (40, 20)])

    projection = json.loads(schematic.to_json())
    projection["wire_runs"][0]["net"] = "net:99"

    try:
        design.load_schematic_json(json.dumps(projection))
    except RuntimeError as error:
        assert str(error) == "Net reference points to a missing logical net: net:99"
    else:
        raise AssertionError("stale schematic logical references must fail at load time")

def test_python_schematic_writes_svg_projection():
    design = volt.Design("schematic-svg")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")

    schematic = design.schematic("Main")
    schematic.place(r1, at=(40, 20), symbol="resistor")
    schematic.wire(vcc, [(20, 20), (40, 20)])
    schematic.label(vcc, at=(20, 16))

    svg = schematic.to_svg()

    assert svg.startswith('<svg xmlns="http://www.w3.org/2000/svg"')
    assert 'class="schematic-sheet"' in svg
    assert 'class="symbol-instance"' in svg
    assert 'data-component="component:0"' in svg
    assert '<polyline class="wire-run" data-net="net:0" points="20,20 40,20"/>' in svg
    assert '<text class="net-label" data-net="net:0" x="20" y="16"' in svg
    assert ">VCC</text>" in svg
    assert '<text class="reference" x="0" y="-12">R1</text>' in svg
    assert "pin-anchor" not in svg
    assert "pin-label" not in svg

    with TemporaryDirectory() as directory:
        path = Path(directory) / "schematic.svg"
        schematic.write_svg(path)
        assert path.read_text(encoding="utf-8") == svg


def test_python_schematic_writes_one_svg_per_sheet():
    design = volt.Design("schematic-svg-pages")

    schematic = design.schematic("Power", size=(100, 80))
    design.schematic("USB and Connectors", size=(120, 90))

    pages = schematic.to_svg_pages()

    assert [page["name"] for page in pages] == ["Power", "USB and Connectors"]
    assert 'viewBox="0 0 100 80"' in pages[0]["svg"]
    assert 'data-sheet="sheet:0"' in pages[0]["svg"]
    assert 'data-sheet="sheet:1"' not in pages[0]["svg"]
    assert 'viewBox="0 0 120 90"' in pages[1]["svg"]
    assert 'data-sheet="sheet:1"' in pages[1]["svg"]

    with TemporaryDirectory() as directory:
        paths = schematic.write_svg_pages(directory)

        assert [path.name for path in paths] == ["Power.svg", "USB_and_Connectors.svg"]
        assert paths[0].read_text(encoding="utf-8") == pages[0]["svg"]
        assert paths[1].read_text(encoding="utf-8") == pages[1]["svg"]
