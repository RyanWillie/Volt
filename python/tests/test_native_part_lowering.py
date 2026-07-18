import json

import pytest

import volt


def _footprint(name: str, pin_count: int) -> volt.Footprint:
    return volt.Footprint(
        library="Test",
        name=name,
        pads=tuple(
            volt.FootprintPad.surface_mount(
                str(index), at=(float(index), 0.0), size=(0.6, 0.6)
            )
            for index in range(1, pin_count + 1)
        ),
    )


def _symbol(name: str, pins: tuple[tuple[str, int], ...]) -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec(
        name,
        pins=tuple(
            volt.SchematicSymbolSpec.pin(pin_name, number, (index * 10.0, 0.0))
            for index, (pin_name, number) in enumerate(pins)
        ),
        primitives=(),
    )


def _supply_contract(key: str, *, source: bool) -> volt.ComponentContract:
    schema = (
        volt.FeatureSchema.supply_source()
        if source
        else volt.FeatureSchema.supply_consumer()
    )
    return volt.ComponentContract(
        key=key,
        pin_keys=("P", "N"),
        supply_domains=(volt.ContractSupplyDomain("supply", ("P",), ("N",)),),
        feature_schemas=(schema,),
        feature_bindings=(
            volt.FeatureBinding(
                "supply",
                schema.key,
                volt.ElectricalSubject.supply_domain("supply"),
                (
                    volt.FeatureRoleBinding("positive", ("P",)),
                    volt.FeatureRoleBinding("return", ("N",)),
                ),
            ),
        ),
    )


def _supply_part(
    library: volt.Library,
    name: str,
    *,
    source: bool,
    voltage: tuple[float, float],
    current: float,
    contract: volt.ComponentContract | None = None,
) -> volt.Part:
    subject = volt.ElectricalSubject.supply_domain("supply")
    records = (
        (
            volt.ElectricalRecord.provided_voltage(subject, *voltage),
            volt.ElectricalRecord.current_capability(subject, current),
        )
        if source
        else (
            volt.ElectricalRecord.accepted_voltage(subject, *voltage),
            volt.ElectricalRecord.current_requirement(subject, current),
        )
    )
    return library.part(
        name,
        pins=(volt.PinSpec("P", 1), volt.PinSpec("N", 2)),
        symbol=_symbol(f"test:{name}", (("P", 1), ("N", 2))),
        footprint=_footprint(name, 2),
        pads={1: "1", 2: "2"},
        manufacturer="Test",
        mpn=name,
        package="TEST-2",
        contract=contract
        or _supply_contract(f"test/{'source' if source else 'load'}@1", source=source),
        electrical_records=records,
    )


def test_library_part_build_and_instantiation_use_one_native_exact_route():
    library = volt.Library("test.parts", version="2026.1")
    junction = volt.ElectricalSubject.directed_relation("junction")
    diode_schema = volt.FeatureSchema.diode_junction()
    part = library.part(
        "LED-RED",
        pins=(volt.PinSpec("A", 2), volt.PinSpec("K", 1)),
        symbol=_symbol("test:led", (("A", 2), ("K", 1))),
        footprint=_footprint("LED-RED", 2),
        pads={2: "2", 1: "1"},
        manufacturer="Test",
        mpn="LED-RED",
        package="TEST-2",
        prefix="D",
        contract=volt.ComponentContract(
            key="test/led@1",
            pin_keys=("A", "K"),
            relations=(volt.ContractDirectedRelation("junction", "A", "K"),),
            feature_schemas=(diode_schema,),
            feature_bindings=(
                volt.FeatureBinding(
                    "junction",
                    diode_schema.key,
                    junction,
                    (
                        volt.FeatureRoleBinding("positive", ("A",)),
                        volt.FeatureRoleBinding("negative", ("K",)),
                    ),
                ),
            ),
        ),
        electrical_records=(
            volt.ElectricalRecord.characteristic_voltage(junction, 1.8, 2.0, 2.2),
            volt.ElectricalRecord.absolute_current(junction, maximum=0.03),
            volt.ElectricalRecord.absolute_voltage(junction, minimum=-5.0),
        ),
    )

    result = library.build()
    design = volt.Design("native exact selection")
    d1 = design.instantiate(part, ref="D1")
    d2 = design.instantiate(part, ref="D2")

    assert result.ok
    assert result.part("LED-RED").exact_reference["library_digest"] == result.digest
    payload = json.loads(design.to_json())
    assert len(payload["component_definitions"]) == 1
    selections = [component["selected_library_part"] for component in payload["components"]]
    assert selections[0] == selections[1] == result.part("LED-RED").exact_reference
    assert all("selected_physical_part" not in component for component in payload["components"])
    assert d1.reference == "D1"
    assert d2.reference == "D2"


def test_standard_and_custom_feature_schema_lower_to_same_native_component_identity():
    standard = _supply_contract("test/load@1", source=False)
    custom_schema = volt.FeatureSchema(
        key="volt.feature/supply-consumer@1",
        subject_kind="supply_domain",
        roles=(
            volt.FeatureRole("positive", "one_or_more"),
            volt.FeatureRole("return", "one_or_more"),
        ),
        required_records=(
            volt.CanonicalRecordRequirement("current", "requirement"),
            volt.CanonicalRecordRequirement("voltage", "accepted_range"),
        ),
    )
    custom = volt.ComponentContract(
        key="test/load@1",
        pin_keys=("P", "N"),
        supply_domains=(volt.ContractSupplyDomain("supply", ("P",), ("N",)),),
        feature_schemas=(custom_schema,),
        feature_bindings=standard.feature_bindings,
    )

    identities = []
    for contract in (standard, custom):
        library = volt.Library("test.parity")
        _supply_part(
            library,
            "LOAD",
            source=False,
            voltage=(3.0, 3.6),
            current=0.1,
            contract=contract,
        )
        result = library.build().part("LOAD")
        identities.append((result.component_sha256, result.artifact.sha256))

    assert identities[0] == identities[1]


def test_native_voltage_current_records_drive_selected_part_erc():
    library = volt.Library("test.power")
    source = _supply_part(
        library, "REGULATOR", source=True, voltage=(5.0, 5.0), current=0.05
    )
    load = _supply_part(
        library, "MCU", source=False, voltage=(3.0, 3.6), current=0.1
    )
    design = volt.Design("native erc")
    u1 = design.instantiate(source, ref="U1")
    u2 = design.instantiate(load, ref="U2")
    vdd = design.net("VDD", kind="power")
    gnd = design.net("GND", kind="ground")
    vdd += (u1[1], u2[1])
    gnd += (u1[2], u2[2])

    report = design.validate_selected_part_erc()

    codes = {diagnostic.code for diagnostic in report}
    assert "SELECTED_PART_VOLTAGE_ABOVE_ACCEPTED_RANGE" in codes
    assert "SELECTED_PART_CURRENT_CAPABILITY_INSUFFICIENT" in codes


def test_invalid_exact_mapping_is_rejected_by_the_native_boundary():
    library = volt.Library("test.invalid")
    library.part(
        "BROKEN",
        pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
        symbol=_symbol("test:broken", (("1", 1), ("2", 2))),
        footprint=_footprint("BROKEN", 2),
        pads={1: "1"},
        manufacturer="Test",
        mpn="BROKEN",
        package="TEST-2",
    )

    with pytest.raises(volt.CrossReferenceError):
        library.build()


def test_exact_part_reference_is_shared_by_two_named_boards():
    library = volt.Library("test.two-boards")
    part = library.part(
        "R-1K",
        pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
        symbol=_symbol("test:r", (("1", 1), ("2", 2))),
        package="TEST-2",
        footprint=_footprint("R-1K", 2),
        pads={1: "1", 2: "2"},
        manufacturer="Test",
        mpn="R-1K",
        voltage_rating=50.0,
    )
    design = volt.Design("exact selection on two boards")
    component = design.instantiate(part, ref="R1")
    first = design.add_board("First")
    second = design.add_board("Second")
    first.place(component, at=(1.0, 1.0))
    second.place(component, at=(2.0, 2.0))

    selected = json.loads(design.to_json())["components"][0]
    artifact = json.loads(library.build().part("R-1K").artifact.bytes)
    voltage_record = artifact["electrical_records"]["records"][0]

    assert "selected_library_part" in selected
    assert "selected_physical_part" not in selected
    assert not hasattr(part, "voltage_rating")
    assert voltage_record["subject"] == {
        "kind": "directed_relation",
        "from": 0,
        "to": 1,
    }
    assert (voltage_record["observable"], voltage_record["meaning"]) == (
        "voltage",
        "absolute_limit",
    )
    assert voltage_record["value"] == {
        "kind": "range",
        "dimension": "voltage",
        "maximum": 50.0,
    }
    assert {diagnostic.code for diagnostic in first.validate()} == {
        diagnostic.code for diagnostic in second.validate()
    }


def test_opaque_ratings_and_exact_part_power_are_explicitly_rejected():
    with pytest.raises(TypeError, match="ratings"):
        volt.Part(name="P", pins=(volt.PinSpec("1", 1),), ratings={"mystery": 1})

    with pytest.raises(NotImplementedError, match="Power"):
        volt.Part(
            name="R",
            pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
            manufacturer="Test",
            mpn="R-1K",
            package="TEST-2",
            footprint=_footprint("R-1K", 2),
            pads={1: "1", 2: "2"},
            power_rating=0.25,
        )
