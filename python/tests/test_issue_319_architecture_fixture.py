import json
from pathlib import Path

import pytest

import volt


CATALOGUE = volt.Library("volt.tests.issue319", version="1.0.0")
FIXTURE_MANUFACTURER = "Volt Test Fixtures"
EXPECTED_SYNTHETIC_PARTS = (
    ("VTF-3V3-REG", "VTF-REGULATOR-3P", 3),
    ("VTF-5V-SOURCE", "VTF-SOURCE-5V-2P", 2),
    ("VTF-LED", "VTF-STATUS-LED-2P", 2),
    ("VTF-LED-R", "VTF-LED-RESISTOR-2P", 2),
    ("VTF-MCU", "VTF-ESP32-LIKE-MCU-4P", 4),
)


def _footprint(name, pin_count):
    return volt.Footprint(
        ("volt.tests.issue319", name),
        pads=tuple(
            volt.FootprintPad.surface_mount(
                str(number),
                at=((number - 1) * 1.5, 0),
                size=(0.8, 0.8),
            )
            for number in range(1, pin_count + 1)
        ),
    )


def _symbol(name, pins):
    return volt.SchematicSymbolSpec.block(
        f"volt.tests.issue319:{name}",
        pins=tuple(
            volt.SchematicSymbolSpec.block_pin(
                pin.name,
                pin.number,
                side="left" if index % 2 == 0 else "right",
            )
            for index, pin in enumerate(pins)
        ),
        center_label=name,
    )


def _part(name, pins, *, mpn, prefix):
    pins = tuple(pins)
    return CATALOGUE.part(
        name,
        pins=pins,
        symbol=_symbol(name, pins),
        footprint=_footprint(name, len(pins)),
        pads={pin.number: str(pin.number) for pin in pins},
        manufacturer=FIXTURE_MANUFACTURER,
        mpn=mpn,
        package=f"Fixture-{len(pins)}-Pad",
        prefix=prefix,
    )


SOURCE = _part(
    "VTF-5V-SOURCE",
    (
        volt.PinSpec("VBUS", 1, role="power_output"),
        volt.PinSpec("GND", 2, role="ground"),
    ),
    mpn="VTF-SOURCE-5V-2P",
    prefix="J",
)
REGULATOR = _part(
    "VTF-3V3-REG",
    (
        volt.PinSpec("VIN", 1, role="power"),
        volt.PinSpec("GND", 2, role="ground"),
        volt.PinSpec("VOUT", 3, role="power_output"),
    ),
    mpn="VTF-REGULATOR-3P",
    prefix="U",
)
ESP32 = _part(
    "VTF-MCU",
    (
        volt.PinSpec("3V3", 1, role="power"),
        volt.PinSpec("GND", 2, role="ground"),
        volt.PinSpec("EN", 3, role="input", signal="digital"),
        volt.PinSpec("GPIO8", 4, role="output", signal="digital"),
    ),
    mpn="VTF-ESP32-LIKE-MCU-4P",
    prefix="U",
)
RESISTOR = _part(
    "VTF-LED-R",
    (
        volt.PinSpec("1", 1, role="passive"),
        volt.PinSpec("2", 2, role="passive"),
    ),
    mpn="VTF-LED-RESISTOR-2P",
    prefix="R",
)
LED = _part(
    "VTF-LED",
    (
        volt.PinSpec("A", 1, role="passive"),
        volt.PinSpec("K", 2, role="passive"),
    ),
    mpn="VTF-STATUS-LED-2P",
    prefix="D",
)


def _profile():
    return volt.CapabilityProfile(
        name="Issue 319 architecture fixture",
        source="Volt test fixture",
        as_of="2026-07-28",
        minimum_track_width=0.01,
        minimum_via_drill=0.01,
        minimum_via_annular=0.02,
    )


def _project():
    project = volt.Project(
        "issue-319-architecture",
        version="1.0.0",
        description="Purpose-built production-consumer architecture fixture",
    )
    project.use_library(CATALOGUE)

    @project.design
    def design():
        result = volt.Design("controller")
        source = result.instantiate(SOURCE, ref="J1").dnp(False)
        regulator = result.instantiate(REGULATOR, ref="U1").dnp(False)
        esp32 = result.instantiate(ESP32, ref="U2").dnp(False)
        resistor = result.instantiate(RESISTOR, ref="R1").dnp(False)
        led = result.instantiate(LED, ref="D1").dnp(False)

        vin = result.net("VIN", kind="power", voltage=5.0)
        supply = result.net("3V3", kind="power", voltage=3.3)
        gpio = result.net("STATUS_GPIO")
        led_a = result.net("LED_A")
        ground = result.net("GND", kind="ground")
        vin += source["VBUS"], regulator["VIN"]
        supply += regulator["VOUT"], esp32["3V3"], esp32["EN"]
        gpio += esp32["GPIO8"], resistor[1]
        led_a += resistor[2], led["A"]
        ground += source["GND"], regulator["GND"], esp32["GND"], led["K"]
        return result

    @project.schematic
    def schematic(context):
        design = context.design()
        sheet = design.schematic("Main", size=(480, 200), margins=(8, 8, 8, 8))
        with sheet.drawing(unit=10) as drawing:
            placed = {}
            for reference, position in (
                ("J1", (30, 40)),
                ("U1", (140, 40)),
                ("U2", (250, 40)),
                ("R1", (370, 30)),
                ("D1", (370, 100)),
            ):
                placed[reference] = drawing.place(
                    design.component(reference), at=position
                ).label_ref()
            nets = {net.name: net for net in design.nets()}
            left_stubs = (
                (nets["VIN"], placed["J1"]["VBUS"]),
                (nets["VIN"], placed["U1"]["VIN"]),
                (nets["3V3"], placed["U1"]["VOUT"]),
                (nets["3V3"], placed["U2"]["3V3"]),
                (nets["3V3"], placed["U2"]["EN"]),
                (nets["STATUS_GPIO"], placed["R1"][1]),
                (nets["LED_A"], placed["D1"]["A"]),
            )
            right_stubs = (
                (nets["GND"], placed["J1"]["GND"]),
                (nets["GND"], placed["U1"]["GND"]),
                (nets["GND"], placed["U2"]["GND"]),
                (nets["STATUS_GPIO"], placed["U2"]["GPIO8"]),
                (nets["LED_A"], placed["R1"][2]),
                (nets["GND"], placed["D1"]["K"]),
            )
            drawing.signal_tags(left_stubs, side="left", length=8)
            drawing.signal_tags(right_stubs, side="right", length=8)
        return sheet

    @project.board
    def board(context):
        design = context.design()
        result = design.add_board("Main")
        result.set_capability_profile(_profile())
        result.set_rectangular_outline(origin=(0, 0), size=(40, 20))
        front = result.add_layer("F.Cu", role="copper", side="top")
        back = result.add_layer("B.Cu", role="copper", side="bottom")
        result.set_layer_stack((front, back), thickness=1.6)
        for reference, position in (
            ("J1", (4, 10)),
            ("U1", (12, 10)),
            ("U2", (22, 10)),
            ("R1", (31, 8)),
            ("D1", (35, 8)),
        ):
            result.place(design.component(reference), at=position)
        return result

    return project


def _directory_bytes(root: Path) -> dict[str, bytes]:
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def test_issue_319_part_library_bundle_builds_and_reopens_offline():
    first = CATALOGUE.build()
    second = CATALOGUE.build()

    assert first.ok
    assert first.bundle_bytes == second.bundle_bytes
    assert volt._volt.part_library_bundle_part_keys(first.bundle_bytes) == [
        name for name, _mpn, _pad_count in EXPECTED_SYNTHETIC_PARTS
    ]
    for name, mpn, pad_count in EXPECTED_SYNTHETIC_PARTS:
        definition = json.loads(first.part(name).artifact.bytes)
        orderable = definition["orderable_part"]
        pad_labels = [pad["label"] for pad in orderable["footprint"]["pads"]]
        assert definition["identity"]["name"] == name
        assert orderable["manufacturer"] == FIXTURE_MANUFACTURER
        assert orderable["mpn"] == mpn
        assert orderable["package"] == f"Fixture-{pad_count}-Pad"
        assert pad_labels == [str(index) for index in range(1, pad_count + 1)]
        assert orderable["terminal_pad_mappings"] == [
            {"terminal": label, "pads": [label]} for label in pad_labels
        ]
        assert definition["pin_terminal_mappings"] == [
            {"pin_key": f"pin/{index}", "terminals": [label]}
            for index, label in enumerate(pad_labels)
        ]
    inspection = volt._volt.part_library_bundle_inspect(first.bundle_bytes)
    components = {
        entry["id"]: entry
        for entry in inspection["entries"]
        if entry["role"] == "component_definition"
    }
    assert components
    assert all(
        any(dependency.startswith("asset:symbol:") for dependency in entry["dependencies"])
        for entry in components.values()
    )
    assert volt._volt.part_library_bundle_digest(first.bundle_bytes) == inspection["bundle_digest"]


def test_issue_319_fixture_proves_v2_compiled_scene_and_manufacturing_reopen(tmp_path):
    result = _project().run()
    assert result.ok
    assert not result.diagnostics.errors()

    first = tmp_path / "first.volt"
    second = tmp_path / "second.volt"
    result.write(first)
    _project().run().write(second)
    assert _directory_bytes(first) == _directory_bytes(second)

    bundle = volt.ProjectBundle.open(first)
    loaded = bundle.graph.loaded_project
    assert [model.design for model in loaded.circuits] == ["controller"]
    assert [model.name for model in loaded.schematics] == ["Main"]
    assert [model.name for model in loaded.boards] == ["Main"]
    assert len(loaded.compiled_boards) == 1
    assert len(loaded.board_scenes) == 1
    compiled = loaded.compiled_boards[0]
    assert compiled.identity["board"] == "Main"
    assert loaded.board_scenes[0].compiled_board.identity == compiled.identity
    with pytest.raises(AttributeError):
        compiled.identity = {"board": "mutated"}

    profile_path = tmp_path / "profiles" / "issue-319.volt.json"
    profile_path.parent.mkdir()
    profile_path.write_text("{}\n", encoding="utf-8")
    profile = {
        "path": "profiles/issue-319.volt.json",
        "resolved_path": str(profile_path),
    }
    first_manufacturing = tmp_path / "first-manufacturing"
    second_manufacturing = tmp_path / "second-manufacturing"
    result.write_manufacturing_package(
        first_manufacturing,
        manufacturing_profile=profile,
        archive=True,
    )
    result.write_manufacturing_package(
        second_manufacturing,
        manufacturing_profile=profile,
        archive=True,
    )
    assert _directory_bytes(first_manufacturing) == _directory_bytes(second_manufacturing)
    assert first_manufacturing.with_suffix(".zip").read_bytes() == (
        second_manufacturing.with_suffix(".zip").read_bytes()
    )
    manifest = json.loads(
        first_manufacturing.joinpath("manufacturing", "manifest.json").read_text(
            encoding="utf-8"
        )
    )
    assert manifest["format"] == "volt.manufacturing_package"
    assert manifest["board"]["compiled_board_provenance_digest"].startswith("sha256:")
