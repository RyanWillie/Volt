import copy

import pytest

import volt


def _one_pin_design(name: str):
    design = volt.Design(name)
    definition = design.define_component("OnePin", pins=[volt.PinSpec("P", 1)])
    component = design.instantiate(definition, ref="U1")
    return design, design.net("SIG"), component[1]


def _module_port(design: volt.Design, name: str = "M"):
    module = design.define_module(f"{name}Module")
    module.port("IO")
    return design.instantiate(module, ref=name)["IO"]


def _assert_foreign_owner_error(error, message: str) -> None:
    assert str(error) == message
    assert error.code == "CrossReferenceViolation"
    assert error.entity is None
    assert isinstance(error, volt.CrossReferenceError)
    assert isinstance(error, RuntimeError)
    assert isinstance(error, ValueError)


@pytest.mark.parametrize("method", ["connect", "iadd"])
def test_foreign_same_index_pin_is_rejected_through_both_public_connect_forms(method):
    local, local_net, _local_pin = _one_pin_design("local")
    foreign, foreign_net, foreign_pin = _one_pin_design("foreign")
    assert foreign_pin.index == 0
    before_local = local.to_json().encode()
    before_foreign = foreign.to_json().encode()

    with pytest.raises(
        volt.CrossReferenceError, match="^Pin belongs to a different design$"
    ) as rejected:
        if method == "connect":
            local_net.connect(foreign_pin)
        else:
            local_net += foreign_pin

    _assert_foreign_owner_error(rejected.value, "Pin belongs to a different design")
    assert local.to_json().encode() == before_local
    assert foreign.to_json().encode() == before_foreign
    assert foreign_net.pins() == ()


def test_foreign_net_direction_rejects_local_pin_without_mutating_either_design():
    local, local_net, local_pin = _one_pin_design("local")
    foreign, foreign_net, _foreign_pin = _one_pin_design("foreign")
    before_local = local.to_json().encode()
    before_foreign = foreign.to_json().encode()

    with pytest.raises(volt.CrossReferenceError) as rejected:
        foreign_net.connect(local_pin)

    _assert_foreign_owner_error(rejected.value, "Pin belongs to a different design")
    assert local.to_json().encode() == before_local
    assert foreign.to_json().encode() == before_foreign
    assert local_net.pins() == ()


def test_foreign_module_instance_port_is_rejected_before_same_index_binding():
    local = volt.Design("local")
    local_net = local.net("PARENT")
    local_port = _module_port(local, "M")
    foreign = volt.Design("foreign")
    foreign_net = foreign.net("PARENT")
    foreign_port = _module_port(foreign, "M")
    assert foreign_port.port_index == local_port.port_index
    before_local = local.to_json().encode()
    before_foreign = foreign.to_json().encode()

    with pytest.raises(
        volt.CrossReferenceError,
        match="^Module instance port belongs to a different design$",
    ) as rejected:
        local_net += foreign_port

    _assert_foreign_owner_error(
        rejected.value, "Module instance port belongs to a different design"
    )
    assert local.to_json().encode() == before_local
    assert foreign.to_json().encode() == before_foreign

    with pytest.raises(volt.CrossReferenceError):
        foreign_net.connect(local_port)
    assert local.to_json().encode() == before_local
    assert foreign.to_json().encode() == before_foreign


def test_late_foreign_operand_leaves_earlier_valid_pin_byte_identically_unmodified():
    local, local_net, local_pin = _one_pin_design("local")
    foreign, _foreign_net, foreign_pin = _one_pin_design("foreign")
    before = local.to_json().encode()

    with pytest.raises(volt.CrossReferenceError):
        local_net.connect(local_pin, foreign_pin)

    assert local.to_json().encode() == before


def test_late_foreign_module_port_leaves_earlier_valid_pin_byte_identically_unmodified():
    local, local_net, local_pin = _one_pin_design("local")
    foreign = volt.Design("foreign")
    foreign_port = _module_port(foreign)
    before = local.to_json().encode()

    with pytest.raises(volt.CrossReferenceError):
        local_net.connect(local_pin, foreign_port)

    assert local.to_json().encode() == before


def test_late_kernel_invalid_pin_leaves_earlier_valid_pin_byte_identically_unmodified():
    design = volt.Design("atomic-kernel-state")
    definition = design.define_component(
        "TwoPin", pins=[volt.PinSpec("A", 1), volt.PinSpec("B", 2)]
    )
    component = design.instantiate(definition, ref="U1")
    target = design.net("TARGET")
    other = design.net("OTHER")
    other += component[2]
    before = design.to_json().encode()

    with pytest.raises(
        volt.InvalidStateError, match="^Pin is already connected to another net$"
    ) as rejected:
        target.connect(component[1], component[2])

    assert rejected.value.code == "InvalidState"
    assert design.to_json().encode() == before


def test_late_invalid_type_in_nested_bulk_input_is_atomic():
    design, net, pin = _one_pin_design("atomic-type")
    before = design.to_json().encode()

    with pytest.raises(
        TypeError, match="^Nets can only connect Pin or ModuleInstancePort handles$"
    ):
        net.connect([pin, (object(),)])

    assert design.to_json().encode() == before


def test_mixed_nested_bulk_inputs_and_copied_handles_keep_same_owner_behavior():
    design = volt.Design("mixed")
    definition = design.define_component(
        "GroupedPins", pins=[volt.PinSpec("IO", 1), volt.PinSpec("IO", 2)]
    )
    component = design.instantiate(definition, ref="U1")
    port = _module_port(design)
    net = design.net("BUS")

    copied_net = copy.copy(net)
    copied_group = copy.copy(component.pins("IO"))
    copied_port = copy.copy(port)
    copied_net.connect([copied_group, (copied_port,)])

    assert [pin.index for pin in net.pins()] == [0, 1]
    assert len(port._instance.port_bindings()) == 1
    assert port._instance.port_bindings()[0].parent_net == net.index


def test_owner_identity_survives_direct_pin_and_net_handle_copies():
    design, net, pin = _one_pin_design("copied-handles")

    copy.copy(net).connect(copy.copy(pin))

    assert [connected.index for connected in net.pins()] == [pin.index]


def test_duplicate_module_port_late_in_bulk_input_is_atomic():
    design, net, pin = _one_pin_design("duplicate-port")
    port = _module_port(design)
    before = design.to_json().encode()

    with pytest.raises(
        volt.InvalidStateError, match="^Module instance port is already bound$"
    ) as rejected:
        net.connect(pin, port, port)

    assert rejected.value.code == "InvalidState"
    assert design.to_json().encode() == before
