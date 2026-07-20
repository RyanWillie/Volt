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


def _custom_supply_contract(key: str, *, source: bool) -> volt.ComponentContract:
    standard = _supply_contract(key, source=source)
    schema = standard.feature_schemas[0]
    custom_schema = volt.FeatureSchema(
        key=schema.key,
        subject_kind=schema.subject_kind,
        roles=schema.roles,
        required_records=schema.required_records,
    )
    return volt.ComponentContract(
        key=key,
        pin_keys=standard.pin_keys,
        supply_domains=standard.supply_domains,
        feature_schemas=(custom_schema,),
        feature_bindings=standard.feature_bindings,
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


def _led_part(
    library: volt.Library, schema: volt.FeatureSchema | None = None
) -> volt.Part:
    junction = volt.ElectricalSubject.directed_relation("junction")
    diode_schema = schema or volt.FeatureSchema.diode_junction()
    return library.part(
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


def test_library_part_build_and_instantiation_use_one_native_exact_route():
    library = volt.Library("test.parts", version="2026.1")
    part = _led_part(library)

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

    standard_schema = volt.FeatureSchema.diode_junction()
    custom_schema = volt.FeatureSchema(
        standard_schema.key,
        standard_schema.subject_kind,
        standard_schema.roles,
        standard_schema.required_records,
    )
    custom_library = volt.Library("test.parts", version="2026.1")
    _led_part(custom_library, custom_schema)
    custom_result = custom_library.build()

    assert result.part("LED-RED").artifact.bytes == custom_result.part(
        "LED-RED"
    ).artifact.bytes
    assert result.part("LED-RED").component_sha256 == custom_result.part(
        "LED-RED"
    ).component_sha256
    assert tuple(result.diagnostics) == tuple(custom_result.diagnostics)


def test_standard_and_custom_supply_helpers_have_byte_and_diagnostic_parity():
    outcomes = []
    for custom in (False, True):
        library = volt.Library("test.parity")
        source = _supply_part(
            library,
            "REGULATOR",
            source=True,
            voltage=(5.0, 5.0),
            current=0.05,
            contract=(
                _custom_supply_contract("test/source@1", source=True)
                if custom
                else _supply_contract("test/source@1", source=True)
            ),
        )
        load = _supply_part(
            library,
            "MCU",
            source=False,
            voltage=(3.0, 3.6),
            current=0.1,
            contract=(
                _custom_supply_contract("test/load@1", source=False)
                if custom
                else _supply_contract("test/load@1", source=False)
            ),
        )
        result = library.build()
        design = volt.Design("standard custom parity")
        u1 = design.instantiate(source, ref="U1")
        u2 = design.instantiate(load, ref="U2")
        vdd = design.net("VDD", kind="power")
        gnd = design.net("GND", kind="ground")
        vdd += (u1[1], u2[1])
        gnd += (u1[2], u2[2])
        outcomes.append(
            (
                tuple(
                    (
                        result.part(name).component_sha256,
                        result.part(name).artifact.bytes,
                        result.part(name).exact_reference,
                    )
                    for name in ("REGULATOR", "MCU")
                ),
                design.to_json(),
                tuple(design.validate_selected_part_erc(result)),
            )
        )

    assert outcomes[0] == outcomes[1]


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

    with pytest.raises(TypeError):
        design.validate_selected_part_erc()

    report = design.validate_selected_part_erc(library)

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
    first_diagnostics = tuple(
        (
            diagnostic.severity,
            diagnostic.code,
            diagnostic.message,
            diagnostic.entities,
        )
        for diagnostic in first.validate()
    )
    second_diagnostics = tuple(
        (
            diagnostic.severity,
            diagnostic.code,
            diagnostic.message,
            diagnostic.entities,
        )
        for diagnostic in second.validate()
    )
    assert first_diagnostics == second_diagnostics
    assert first.to_json() == first.to_json()
    assert second.to_json() == second.to_json()


def test_selected_part_erc_resolution_is_explicit_and_exact():
    library = volt.Library("test.explicit")
    source = _supply_part(
        library, "REGULATOR", source=True, voltage=(3.3, 3.3), current=0.2
    )
    load = _supply_part(
        library, "MCU", source=False, voltage=(3.0, 3.6), current=0.1
    )
    design = volt.Design("explicit resolver")
    u1 = design.instantiate(source, ref="U1")
    u2 = design.instantiate(load, ref="U2")
    vdd = design.net("VDD", kind="power")
    gnd = design.net("GND", kind="ground")
    vdd += (u1[1], u2[1])
    gnd += (u1[2], u2[2])

    result = library.build()
    assert not design.validate_selected_part_erc(result).has_errors

    other = volt.Library("test.other")
    _supply_part(other, "REGULATOR", source=True, voltage=(3.3, 3.3), current=0.2)
    _supply_part(other, "MCU", source=False, voltage=(3.0, 3.6), current=0.1)
    with pytest.raises(volt.CrossReferenceError):
        design.validate_selected_part_erc(other)


def test_part_instantiation_has_no_legacy_physical_part_fallback():
    library = volt.Library("test.incomplete")
    part = library.part(
        "LOGICAL-ONLY",
        pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
        symbol=_symbol("test:logical-only", (("1", 1), ("2", 2))),
    )
    design = volt.Design("no exact-part fallback")

    with pytest.raises(ValueError, match="complete native exact part"):
        design.instantiate(part, ref="U1")

    assert json.loads(design.to_json())["components"] == []


def test_voltage_rating_shorthand_is_structurally_checked_by_native_lowering():
    library = volt.Library("test.rating-structure")
    library.part(
        "THREE-PIN",
        pins=(
            volt.PinSpec("1", 1),
            volt.PinSpec("2", 2),
            volt.PinSpec("3", 3),
        ),
        footprint=_footprint("THREE-PIN", 3),
        pads={1: "1", 2: "2", 3: "3"},
        manufacturer="Test",
        mpn="THREE-PIN",
        package="TEST-3",
        voltage_rating=5.0,
    )

    with pytest.raises(volt.InvalidArgumentError, match="two-pin part") as failure:
        library.build()

    assert failure.value.code == "InvalidArgument"


def test_native_conditions_tolerance_and_evidence_round_trip_deterministically():
    library = volt.Library("test.conditions")
    junction = volt.ElectricalSubject.directed_relation("junction")
    selector = volt.ElectricalRecordSelector(junction, "current", "characteristic")
    condition = volt.ElectricalCondition.equal(
        junction,
        "current",
        volt.ElectricalValueExpression.scaled_reference(selector, 1.0),
    )
    diode_schema = volt.FeatureSchema.diode_junction()
    library.part(
        "LED-CONDITIONED",
        pins=(volt.PinSpec("A", 2), volt.PinSpec("K", 1)),
        symbol=_symbol("test:led-conditioned", (("A", 2), ("K", 1))),
        footprint=_footprint("LED-CONDITIONED", 2),
        pads={2: "2", 1: "1"},
        manufacturer="Test",
        mpn="LED-CONDITIONED",
        package="TEST-2",
        contract=volt.ComponentContract(
            key="test/led-conditioned@1",
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
            volt.ElectricalRecord(
                subject=junction,
                observable="voltage",
                meaning="characteristic",
                value_kind="toleranced",
                value=2.0,
                tolerance_mode="percent",
                tolerance_minus=0.1,
                tolerance_plus=0.1,
                conditions=(condition,),
                evidence=("sha256:" + "0" * 64,),
            ),
            volt.ElectricalRecord(
                subject=junction,
                observable="current",
                meaning="characteristic",
                value_kind="quantity",
                value=0.02,
            ),
            volt.ElectricalRecord.absolute_current(junction, maximum=0.03),
            volt.ElectricalRecord.absolute_voltage(junction, minimum=-5.0),
        ),
    )

    first = library.build().part("LED-CONDITIONED").artifact.bytes
    second = library.build().part("LED-CONDITIONED").artifact.bytes
    record = next(
        item
        for item in json.loads(first)["electrical_records"]["records"]
        if (item["observable"], item["meaning"])
        == ("voltage", "characteristic")
    )

    assert first == second
    assert record["value"] == {
        "kind": "characteristic_envelope",
        "dimension": "voltage",
        "minimum": 1.8,
        "typical": 2.0,
        "maximum": 2.2,
    }
    assert record["conditions"]
    assert record["evidence"] == ["sha256:" + "0" * 64]


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
