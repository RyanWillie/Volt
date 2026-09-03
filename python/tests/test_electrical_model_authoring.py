import gc
import hashlib
import json
import math
import os
import subprocess
import sys

import pytest

import volt


EVIDENCE_A = b"Illustrative passive parameter evidence A"
EVIDENCE_B = b"Illustrative passive parameter evidence B"
VARIANTS = (
    "absent",
    "resistor",
    "zero",
    "zero_unspecified",
    "capacitor",
    "inductor",
    "composite",
    "reversed",
    "renamed",
    "changed",
    "unspecified",
    "evidence_changed",
)


def _digest(data):
    return "sha256:" + hashlib.sha256(data).hexdigest()


def _fields():
    return dict(
        pins=(volt.PinSpec("A", 1, role=None), volt.PinSpec("B", 2, role=None)),
        contract=volt.ComponentContract(
            "test.authoring/passive@1", pin_keys=("A", "B")
        ),
        footprint=volt.Footprint(
            library="test.authoring",
            name="passive",
            pads=(
                volt.FootprintPad.surface_mount(
                    "1", at=(-0.5, 0.0), size=(0.5, 0.5), shape="rectangle"
                ),
                volt.FootprintPad.surface_mount(
                    "2", at=(0.5, 0.0), size=(0.5, 0.5), shape="rectangle"
                ),
            ),
        ),
        pads={1: "1", 2: "2"},
        manufacturer="Test",
        mpn="PARITY-2",
        package="TEST-2",
        source_name="parity",
        provenance=volt.PartProvenance(
            authored_by="volt.tests", derived_from="illustrative native parity fixture"
        ),
        evidence_assets=(EVIDENCE_A, EVIDENCE_B),
    )


def _builder():
    library = volt.Library("test.authoring", version="1")
    return library.electrical_model_builder("Passive authoring proof", **_fields())


def _model(builder, variant="resistor", *, equivalent=False):
    # Deliberately reverse declarations and duplicate evidence relative to the C++ fixture.
    b = builder.terminal("b", "B")
    a = builder.terminal("a", "A")
    evidence = [_digest(EVIDENCE_B), _digest(EVIDENCE_A), _digest(EVIDENCE_B)]
    if variant == "composite":
        y = builder.internal_node("after_esl")
        x = builder.internal_node("after_esr")
        tolerance = (
            volt.Tolerance.absolute(volt.farads(10e-6 * 0.1), volt.farads(10e-6 * 0.2))
            if equivalent
            else volt.Tolerance.percent(0.1, 0.2)
        )
        builder.capacitance(
            "storage", y, b,
            volt.ModelParameter(volt.farads(10e-6), tolerance, evidence),
        )
        builder.inductance(
            "esl", x, y,
            volt.ModelParameter(volt.henries(1e-9), volt.Tolerance.percent(-0.0)),
        )
        builder.resistance(
            "esr", a, x, volt.ModelParameter(volt.ohms(0.08), evidence=evidence)
        )
    elif variant == "capacitor":
        builder.capacitance(
            "body", a, b,
            volt.ModelParameter(
                volt.farads(2e-6), volt.Tolerance.percent(0.0, 0.2), evidence
            ),
        )
    elif variant == "inductor":
        builder.inductance(
            "body", a, b, volt.ModelParameter(volt.henries(3e-3), evidence=evidence)
        )
    else:
        zero = variant.startswith("zero")
        nominal = 0.0 if zero else 201.0 if variant == "changed" else 200.0
        if variant in ("zero_unspecified", "unspecified"):
            tolerance = None
        elif zero:
            tolerance = volt.Tolerance.absolute(volt.ohms(0.0), volt.ohms(0.0))
        elif equivalent:
            tolerance = volt.Tolerance.absolute(volt.ohms(2.0), volt.ohms(4.0))
        else:
            tolerance = volt.Tolerance.percent(0.01, 0.02)
        quantity = (
            volt.Quantity(volt.UnitDimension.RESISTANCE, nominal)
            if equivalent
            else volt.ohms(nominal)
        )
        builder.resistance(
            "renamed" if variant == "renamed" else "body",
            b if variant == "reversed" else a,
            a if variant == "reversed" else b,
            volt.ModelParameter(
                quantity,
                tolerance,
                [_digest(EVIDENCE_A)] if variant == "evidence_changed" else evidence,
            ),
        )
    return builder.build()


def _authored(variant="resistor", *, equivalent=False):
    library = volt.Library("test.authoring", version="1")
    fields = _fields()
    model = None
    if variant != "absent":
        builder = library.electrical_model_builder("Passive authoring proof", **fields)
        model = _model(builder, variant, equivalent=equivalent)
    part = library.part("Passive authoring proof", **fields, electrical_model=model)
    return library, part, library.build()


@pytest.fixture(scope="module")
def native_artifacts(tmp_path_factory):
    destination = tmp_path_factory.mktemp("native-authoring") / "artifacts"
    subprocess.run(
        [os.environ["VOLT_PART_MODEL_AUTHORING_FIXTURE"], str(destination)],
        check=True,
        capture_output=True,
    )
    return destination


@pytest.mark.parametrize("variant", VARIANTS)
def test_python_exact_part_hash_and_artifact_bytes_match_independent_cpp(native_artifacts, variant):
    _library, part, result = _authored(variant)
    actual = result.part(part.name)
    expected = (native_artifacts / f"{variant}.part.json").read_bytes()

    assert actual.artifact.bytes == expected
    assert actual.artifact.sha256 == _digest(expected)
    assert actual.component_sha256 == (native_artifacts / "component.digest").read_text()
    assert actual.exact_reference["part_digest"] == (
        native_artifacts / f"{variant}.digest"
    ).read_text()
    assert result.bundle_bytes == (native_artifacts / f"{variant}.voltlib").read_bytes()
    assert (part.electrical_model is None) == (variant == "absent")


@pytest.mark.parametrize("variant", ("resistor", "composite"))
def test_native_absolute_tolerance_and_si_quantity_helpers_have_exact_parity(
    native_artifacts, variant
):
    _library, part, result = _authored(variant, equivalent=True)
    assert result.part(part.name).artifact.bytes == (
        native_artifacts / f"{variant}.part.json"
    ).read_bytes()


def test_identity_distinguishes_parameters_uncertainty_orientation_keys_and_evidence(
    native_artifacts,
):
    digests = [(native_artifacts / f"{variant}.digest").read_text() for variant in VARIANTS]
    assert len(set(digests)) == len(VARIANTS)
    _library, part, result = _authored()
    model = part.electrical_model
    element = model.elements[0]
    parameter = element.parameter
    assert str(element.key) == "body"
    assert isinstance(element, volt.ResistanceElement)
    assert parameter.nominal == volt.ohms(200.0)
    assert parameter.tolerance.mode == volt.ToleranceMode.ABSOLUTE
    assert parameter.tolerance.minus == volt.ohms(2.0)
    assert parameter.tolerance.plus == volt.ohms(4.0)
    assert parameter.bounds.minimum == volt.ohms(198.0)
    assert parameter.bounds.maximum == volt.ohms(204.0)
    assert [str(value) for value in parameter.evidence] == sorted(
        [_digest(EVIDENCE_A), _digest(EVIDENCE_B)]
    )
    assert str(model.implemented_component) == result.part(part.name).component_sha256


def _invalid_case(case):
    builder = _builder()
    a = builder.terminal("a", "A")
    b = builder.terminal("b", "B")
    parameter = volt.ModelParameter(volt.ohms(1.0))
    foreign = _builder()
    foreign_a = foreign.terminal("a", "A")
    if case == "wrong_dimension":
        builder.resistance(
            "body", a, b,
            volt.ModelParameter(volt.Quantity(volt.UnitDimension.VOLTAGE, 1.0)),
        )
    elif case == "negative_r":
        builder.resistance("body", a, b, volt.ModelParameter(volt.ohms(-1.0)))
    elif case == "zero_c":
        builder.capacitance("body", a, b, volt.ModelParameter(volt.farads(0.0)))
    elif case == "zero_l":
        builder.inductance("body", a, b, volt.ModelParameter(volt.henries(0.0)))
    elif case == "nan":
        volt.Quantity(volt.UnitDimension.RESISTANCE, math.nan)
    elif case == "infinity":
        volt.Tolerance.percent(math.inf)
    elif case == "negative_tolerance":
        volt.Tolerance.percent(-0.01)
    elif case == "tolerance_dimension":
        volts = volt.Quantity(volt.UnitDimension.VOLTAGE, 0.0)
        volt.ModelParameter(volt.ohms(1.0), volt.Tolerance.absolute(volts, volts))
    elif case == "overflow":
        volt.ModelParameter(volt.ohms(sys.float_info.max), volt.Tolerance.percent(0.0, 1.0))
    elif case == "bounds":
        builder.resistance(
            "body", a, b,
            volt.ModelParameter(volt.ohms(1.0), volt.Tolerance.percent(1.01, 0.0)),
        )
    elif case == "foreign_handle":
        builder.resistance("body", foreign_a, b, parameter)
    elif case == "duplicate_terminal":
        builder.terminal("a", "B")
    elif case == "duplicate_pin":
        builder.terminal("other", "A")
    elif case == "foreign_pin":
        builder.terminal("other", "C")
    elif case == "same_endpoint":
        builder.resistance("body", a, a, parameter)
    elif case == "duplicate_element":
        builder.resistance("body", a, b, parameter)
        builder.resistance("body", a, b, parameter)
    elif case == "missing_terminal":
        incomplete = _builder()
        one = incomplete.terminal("a", "A")
        node = incomplete.internal_node("x")
        incomplete.resistance("body", one, node, parameter)
        incomplete.build()
    elif case == "unused_node":
        builder.resistance("body", a, b, parameter)
        builder.internal_node("unused")
        builder.build()
    else:
        raise AssertionError(f"Unknown native failure case: {case}")


def test_python_failures_preserve_independently_observed_native_codes_and_messages(
    native_artifacts,
):
    exceptions = {
        "ValueError": ValueError,
        "InvalidArgument": volt.InvalidArgumentError,
        "DuplicateName": volt.DuplicateNameError,
        "CrossReferenceViolation": volt.CrossReferenceError,
    }
    cases = (native_artifacts / "errors.tsv").read_text().splitlines()
    assert len(cases) == 18
    for row in cases:
        case, code, message = row.split("\t")
        with pytest.raises(exceptions[code]) as error:
            _invalid_case(case)
        assert str(error.value) == message, case
        if code != "ValueError":
            assert error.value.code == code, case
            assert error.value.entity is None


@pytest.mark.parametrize("value", (1, 1.0, True, "200 ohm", {}, None))
def test_parameters_require_native_dimensioned_quantities(value):
    with pytest.raises(TypeError):
        volt.ModelParameter(value)
    builder = _builder()
    a = builder.terminal("a", "A")
    b = builder.terminal("b", "B")
    with pytest.raises(TypeError):
        builder.resistance("body", a, b, value)


@pytest.mark.parametrize("value", ({}, {"elements": []}, False, 0, "model", []))
def test_part_rejects_python_owned_model_payloads(value):
    with pytest.raises(TypeError, match="native PartElectricalModel"):
        volt.Part(name="Passive authoring proof", **_fields(), electrical_model=value)


@pytest.mark.parametrize("asset", (bytearray(b"mutable"), "text", {}, 1))
def test_part_evidence_assets_require_immutable_bytes(asset):
    fields = _fields()
    fields["evidence_assets"] = (asset,)
    with pytest.raises(TypeError, match="immutable bytes"):
        volt.Part(name="Passive authoring proof", **fields)


def test_builder_requires_owned_handles_and_canonical_evidence_references():
    builder = _builder()
    a, b = builder.terminal("a", "A"), builder.terminal("b", "B")
    with pytest.raises(TypeError):
        builder.resistance("body", a.key, b.key, volt.ModelParameter(volt.ohms(1.0)))
    for key_type in (volt.ModelTerminalKey, volt.ModelInternalNodeKey, volt.ModelElementKey):
        with pytest.raises(volt.InvalidArgumentError):
            key_type("")
    with pytest.raises(ValueError):
        volt.ModelParameter(volt.ohms(1.0), evidence=["not-a-content-hash"])
    reference = volt.ContentHash(_digest(EVIDENCE_A))
    parameter = volt.ModelParameter(volt.ohms(1.0), evidence=[reference, reference])
    assert tuple(str(item) for item in parameter.evidence) == (_digest(EVIDENCE_A),)
    with pytest.raises(TypeError):
        builder.terminal(volt.ModelInternalNodeKey("wrong-kind"), "A")
    with pytest.raises(TypeError):
        builder.internal_node(volt.ModelTerminalKey("wrong-kind"))
    with pytest.raises(TypeError):
        builder.resistance(volt.ModelTerminalKey("wrong-kind"), a, b, parameter)


@pytest.mark.parametrize(
    "element_type,quantity",
    (("ResistanceElement", "ohms"), ("CapacitanceElement", "farads"),
     ("InductanceElement", "henries")),
)
def test_direct_native_elements_require_typed_endpoints_and_dimensioned_parameters(
    element_type, quantity
):
    constructor = getattr(volt, element_type)
    a = volt.ModelTerminalKey("a")
    b = volt.ModelInternalNodeKey("b")
    parameter = volt.ModelParameter(getattr(volt, quantity)(1.0))
    element = constructor("body", a, b, parameter)
    assert element.from_ == a
    assert element.to == b
    assert element.parameter.nominal == getattr(volt, quantity)(1.0)
    for bad_parameter in (1.0, getattr(volt, quantity)(1.0)):
        with pytest.raises(TypeError):
            constructor("body", a, b, bad_parameter)
    with pytest.raises(TypeError):
        constructor("body", "a", "b", parameter)
    with pytest.raises(volt.InvalidArgumentError):
        constructor("body", a, a, parameter)
    with pytest.raises(volt.InvalidArgumentError):
        constructor("body", a, b, volt.ModelParameter(volt.seconds(1.0)))
    with pytest.raises(AttributeError):
        element.parameter = parameter


@pytest.mark.parametrize(
    "helper,dimension",
    (("ohms", "RESISTANCE"), ("farads", "CAPACITANCE"), ("henries", "INDUCTANCE"),
     ("hertz", "FREQUENCY"), ("seconds", "TIME")),
)
def test_si_helpers_return_native_quantity_without_arithmetic_semantics(helper, dimension):
    quantity = getattr(volt, helper)(2.0)
    assert type(quantity) is volt.Quantity
    assert quantity == volt.Quantity(getattr(volt.UnitDimension, dimension), 2.0)
    with pytest.raises(TypeError):
        _ = quantity + quantity
    with pytest.raises(AttributeError):
        quantity.value = 4.0


@pytest.mark.parametrize("value", (math.nan, math.inf, -math.inf))
def test_all_si_helpers_reject_nonfinite_quantities(value):
    for helper in (volt.ohms, volt.farads, volt.henries, volt.hertz, volt.seconds):
        with pytest.raises(ValueError, match="finite"):
            helper(value)


@pytest.mark.parametrize("method,helper", (("capacitance", "farads"), ("inductance", "henries")))
def test_positive_storage_values_allow_zero_deviations_but_not_zero_bounds(method, helper):
    for minus, plus in ((0.0, 0.0), (0.0, 0.2), (0.2, 0.0)):
        builder = _builder()
        a, b = builder.terminal("a", "A"), builder.terminal("b", "B")
        getattr(builder, method)("body", a, b, volt.ModelParameter(
            getattr(volt, helper)(1.0), volt.Tolerance.percent(minus, plus)
        ))
        assert len(builder.build().elements) == 1
    for nominal, tolerance in ((-0.0, None), (-1.0, None), (1.0, volt.Tolerance.percent(1.0, 0.0))):
        builder = _builder()
        a, b = builder.terminal("a", "A"), builder.terminal("b", "B")
        with pytest.raises(volt.InvalidArgumentError):
            getattr(builder, method)(
                "body", a, b, volt.ModelParameter(getattr(volt, helper)(nominal), tolerance)
            )


def test_unspecified_tolerance_is_absent_and_explicit_zero_is_positive_zero():
    unspecified = volt.ModelParameter(volt.ohms(-0.0))
    assert unspecified.tolerance is None
    assert unspecified.bounds is None
    explicit = volt.ModelParameter(volt.ohms(-0.0), volt.Tolerance.percent(-0.0))
    for quantity in (unspecified.nominal, explicit.nominal, explicit.tolerance.minus,
                     explicit.tolerance.plus, explicit.bounds.minimum, explicit.bounds.maximum):
        assert quantity.value == 0.0
        assert math.copysign(1.0, quantity.value) == 1.0


def test_composite_internal_nodes_are_typed_and_never_ordinary_connectivity_handles():
    model = _model(_builder(), "composite")
    elements = {str(element.key): element for element in model.elements}
    assert list(elements) == ["esl", "esr", "storage"]
    assert [str(node.key) for node in model.internal_nodes] == ["after_esl", "after_esr"]
    assert isinstance(elements["esr"].from_, volt.ModelTerminalKey)
    assert isinstance(elements["esr"].to, volt.ModelInternalNodeKey)
    assert elements["esr"].to == elements["esl"].from_
    assert elements["esl"].to == elements["storage"].from_
    design = volt.Design("ordinary connectivity")
    net = design.net("signal")
    with pytest.raises(TypeError):
        net.connect(elements["esl"].from_)


def test_identical_spelling_in_distinct_key_types_does_not_join_nodes():
    builder = _builder()
    a = builder.terminal(volt.ModelTerminalKey("shared"), "A")
    b = builder.terminal("b", "B")
    x = builder.internal_node(volt.ModelInternalNodeKey("shared"))
    builder.resistance(volt.ModelElementKey("shared"), a, x, volt.ModelParameter(volt.ohms(1.0)))
    builder.resistance("xb", x, b, volt.ModelParameter(volt.ohms(1.0)))
    element = builder.build().elements[0]
    assert str(element.from_) == str(element.to) == "shared"
    assert type(element.from_) is volt.ModelTerminalKey
    assert type(element.to) is volt.ModelInternalNodeKey
    assert element.from_ != element.to


def test_builder_and_value_ownership_survives_collection_gc_and_later_mutation():
    builder = _builder()
    a, b = builder.terminal("a", "A"), builder.terminal("b", "B")
    builder.resistance(
        "body", a, b,
        volt.ModelParameter(volt.ohms(200.0), volt.Tolerance.percent(0.01), [_digest(EVIDENCE_A)]),
    )
    model = builder.build()
    terminal, element = model.terminals[0], model.elements[0]
    nominal = element.parameter.nominal
    tolerance = element.parameter.tolerance
    evidence = element.parameter.evidence[0]
    terminal_key, element_key, endpoint = terminal.key, element.key, element.from_
    builder.capacitance("later", a, b, volt.ModelParameter(volt.farads(1e-9)))
    assert len(builder.build().elements) == 2
    assert len(model.elements) == 1
    returned = model.elements
    with pytest.raises(TypeError):
        returned[0] = None
    assert len(model.elements) == 1
    del model, terminal, element, builder, a, b
    gc.collect()
    assert nominal == volt.ohms(200.0)
    assert tolerance.minus == volt.ohms(2.0)
    assert str(evidence) == _digest(EVIDENCE_A)
    assert str(terminal_key) == str(endpoint) == "a"
    assert str(element_key) == "body"


def test_stale_and_foreign_private_handles_remain_foreign_after_gc():
    first = _builder()
    stale = first.terminal("a", "A")
    stale_node = first.internal_node("x")
    del first
    gc.collect()
    for _ in range(20):
        next_builder = _builder()
        a, b = next_builder.terminal("a", "A"), next_builder.terminal("b", "B")
        next_builder.internal_node("x")
        for from_, to in ((stale, b), (a, stale_node)):
            with pytest.raises(volt.CrossReferenceError) as error:
                next_builder.resistance("body", from_, to, volt.ModelParameter(volt.ohms(1.0)))
            assert error.value.code == "CrossReferenceViolation"
    assert str(stale.key) == "a"
    assert str(stale_node.key) == "x"


def test_part_model_and_snapshot_inspection_outlive_library_growth_and_gc():
    library, part, result = _authored("composite")
    model = result._snapshot.electrical_model("parity")
    expected = result.part(part.name).artifact.bytes
    design = volt.Design("selected exact model")
    selected = design.instantiate(part, ref="C1")
    design.net("A").connect(selected["A"])
    design.net("B").connect(selected["B"])
    before = json.loads(design.to_json())["components"][0]["selected_library_part"]
    for index in range(20):
        library.part(f"Extra {index}", pins=(volt.PinSpec("P", 1),))
    del part, library, result
    gc.collect()
    assert len(model.elements) == 3
    assert model.elements[0].parameter.nominal == volt.henries(1e-9)
    assert model.elements[0].parameter.tolerance.minus == volt.henries(0.0)
    assert json.loads(expected)["electrical_model"]["internal_nodes"] == [
        {"key": "after_esl"}, {"key": "after_esr"}
    ]
    assert json.loads(design.to_json())["components"][0]["selected_library_part"] == before


def test_model_attachment_checks_exact_component_and_never_promotes_display_text():
    model = _model(_builder())
    library = volt.Library("test.authoring", version="1")
    fields = _fields()
    fields["contract"] = volt.ComponentContract("test.authoring/foreign@1", pin_keys=("A", "B"))
    with pytest.raises(volt.CrossReferenceError):
        library.part("Passive authoring proof", **fields, electrical_model=model)
        library.build()
    absent = volt.Library("test.display", version="1")
    fields = _fields()
    fields["value"] = "200 ohm 1%"
    part = absent.part("Passive authoring proof", **fields)
    document = json.loads(absent.build().part(part.name).artifact.bytes)
    assert document["electrical_model"] is None


def test_instance_attributes_and_display_properties_never_override_intrinsic_parameters():
    library, part, result = _authored("resistor")
    expected = result.part(part.name).artifact.bytes
    design = volt.Design("independent occurrence intent")
    instance = design.instantiate(part, ref="R1", properties={"value": "display only 999k"})
    for name, dimension, value in (
        ("resistance", "resistance", 999.0),
        ("capacitance", "capacitance", 1.0),
        ("inductance", "inductance", 2.0),
    ):
        design._circuit.set_component_quantity(instance.index, name, dimension, value)
    design._circuit.set_component_percent_tolerance(instance.index, 0.5)
    occurrence = json.loads(design.to_json())["components"][0]
    assert occurrence["electrical_attributes"]["resistance"]["value"] == 999.0
    assert occurrence["electrical_attributes"]["tolerance"]["minus"] == 0.5
    assert part.electrical_model.elements[0].parameter.nominal == volt.ohms(200.0)
    assert library.build().part(part.name).artifact.bytes == expected


def test_library_bundle_requires_evidence_closure_for_authored_model():
    fields = _fields()
    fields["evidence_assets"] = ()
    library = volt.Library("test.authoring", version="1")
    builder = library.electrical_model_builder("Passive authoring proof", **fields)
    library.part("Passive authoring proof", **fields, electrical_model=_model(builder))
    with pytest.raises((ValueError, RuntimeError), match="[Ee]vidence|asset"):
        library.build().bundle_bytes
