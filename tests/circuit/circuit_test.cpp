#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/instances.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/parts/parts.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/errors.hpp>
#include <volt/core/ids.hpp>

#include <support/circuit_test_helpers.hpp>

namespace {

template <typename Facade>
constexpr bool is_borrow_only_mutator_facade =
    !std::is_default_constructible_v<Facade> && !std::is_constructible_v<Facade, volt::Circuit &> &&
    !std::is_copy_constructible_v<Facade> && !std::is_move_constructible_v<Facade> &&
    !std::is_copy_assignable_v<Facade> && !std::is_move_assignable_v<Facade>;

static_assert(is_borrow_only_mutator_facade<volt::Circuit::ConnectivityMutator>);
static_assert(is_borrow_only_mutator_facade<volt::Circuit::HierarchyMutator>);
static_assert(is_borrow_only_mutator_facade<volt::Circuit::ElectricalMutator>);
static_assert(is_borrow_only_mutator_facade<volt::Circuit::IntentMutator>);
static_assert(is_borrow_only_mutator_facade<volt::Circuit::NetClassMutator>);

template <typename Circuit>
concept has_connectivity_mutator =
    requires(Circuit &&circuit) { std::forward<Circuit>(circuit).connectivity(); };

template <typename Circuit>
concept has_hierarchy_mutator =
    requires(Circuit &&circuit) { std::forward<Circuit>(circuit).hierarchy(); };

template <typename Circuit>
concept has_electrical_mutator =
    requires(Circuit &&circuit) { std::forward<Circuit>(circuit).electrical(); };

template <typename Circuit>
concept has_intent_mutator =
    requires(Circuit &&circuit) { std::forward<Circuit>(circuit).intent(); };

template <typename Circuit>
concept has_net_class_mutator =
    requires(Circuit &&circuit) { std::forward<Circuit>(circuit).net_classes(); };

// These five acquisition checks lock lvalue-only facade availability until deletion in #266.
static_assert(has_connectivity_mutator<volt::Circuit &>);
static_assert(!has_connectivity_mutator<volt::Circuit>);
static_assert(has_hierarchy_mutator<volt::Circuit &>);
static_assert(!has_hierarchy_mutator<volt::Circuit>);
static_assert(has_electrical_mutator<volt::Circuit &>);
static_assert(!has_electrical_mutator<volt::Circuit>);
static_assert(has_intent_mutator<volt::Circuit &>);
static_assert(!has_intent_mutator<volt::Circuit>);
static_assert(has_net_class_mutator<volt::Circuit &>);
static_assert(!has_net_class_mutator<volt::Circuit>);

volt::PhysicalPart make_resistor_physical_part(volt::PinDefId first_pin,
                                               volt::PinDefId second_pin) {
    return volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::PinPadMapping{first_pin, "1"},
            volt::PinPadMapping{second_pin, "2"},
        },
    };
}

} // namespace

TEST_CASE("Circuit starts with empty entity tables") {
    const volt::Circuit circuit;

    CHECK(circuit.pin_definition_count() == 0);
    CHECK(circuit.component_definition_count() == 0);
    CHECK(circuit.component_count() == 0);
    CHECK(circuit.pin_count() == 0);
    CHECK(circuit.net_count() == 0);
}

TEST_CASE("Circuit stores pin definitions in deterministic order") {
    volt::Circuit circuit;
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")},
    });
    const auto &pins = circuit.get(definition).pins();
    const auto first = pins[0];
    const auto second = pins[1];

    CHECK(first == volt::PinDefId{0});
    CHECK(second == volt::PinDefId{1});
    CHECK(circuit.pin_definition(first).name() == "1");
    CHECK(circuit.pin_definition(second).number() == "2");
    CHECK(circuit.pin_definition_count() == 2);
}

TEST_CASE("Circuit stores component definitions") {
    volt::Circuit circuit;
    const auto resistor = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {volt::test::passive_pin("A", "1"), volt::test::passive_pin("B", "2")},
    });

    CHECK(resistor == volt::ComponentDefId{0});
    CHECK(circuit.component_definition(resistor).name() == "Resistor");
    REQUIRE(circuit.component_definition(resistor).pins().size() == 2);
    CHECK(circuit.component_definition_count() == 1);
}

TEST_CASE("Circuit stores component instances and concrete pin instances") {
    volt::Circuit circuit;
    const auto component_def = circuit.define_component(volt::ComponentSpec{
        .name = "Regulator",
        .pins = {volt::PinSpec{.name = "VDD",
                               .number = "1",
                               .requirement = volt::ConnectionRequirement::Required,
                               .terminal_kind = volt::ElectricalTerminalKind::Power,
                               .direction = volt::ElectricalDirection::Input}},
    });
    const auto pin_def = circuit.get(component_def).pins().front();
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto pin = volt::queries::pin_by_definition(circuit, component, pin_def).value();

    CHECK(component == volt::ComponentId{0});
    CHECK(circuit.component(component).reference() == volt::ReferenceDesignator{"U1"});
    CHECK(pin == volt::PinId{0});
    CHECK(circuit.pin(pin).component() == component);
    CHECK(circuit.pin(pin).definition() == pin_def);
    CHECK(circuit.component_count() == 1);
    CHECK(circuit.pin_count() == 1);
}

TEST_CASE("Circuit rejects component instances that reference missing definitions") {
    volt::Circuit circuit;

    try {
        static_cast<void>(circuit.instantiate_component(
            volt::ComponentDefId{9},
            volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U_MISSING"}}));
        FAIL("Unknown component definition must throw");
    } catch (const volt::KernelRangeError &error) {
        CHECK(error.code() == volt::ErrorCode::UnknownEntity);
        CHECK(std::string{error.what()} ==
              "Component definition ID does not belong to this circuit");
        REQUIRE(error.entity().has_value());
        CHECK(error.entity()->kind() == volt::EntityKind::ComponentDef);
        CHECK(error.entity()->index() == 9);
    }
}

TEST_CASE("Legacy connectivity facade rejects raw pin instances with missing IDs") {
    volt::Circuit circuit;
    const auto component_def =
        volt::test::define_component(circuit, "Regulator", {volt::test::passive_pin("VDD", "1")});
    const auto pin_def = circuit.get(component_def).pins().front();
    const auto component = volt::test::instantiate_component(circuit, component_def, "U1");

    // Raw pin insertion exists only on the transitional facade and remains locked until #266.
    CHECK_THROWS_AS(
        circuit.connectivity().add_pin(volt::PinInstance{volt::ComponentId{42}, pin_def}),
        std::out_of_range);
    CHECK_THROWS_AS(
        circuit.connectivity().add_pin(volt::PinInstance{component, volt::PinDefId{42}}),
        std::out_of_range);
}

TEST_CASE("Circuit stores nets") {
    volt::Circuit circuit;
    const auto component_def =
        volt::test::define_component(circuit, "Connector", {volt::test::passive_pin("GND", "1")});
    const auto pin_def = circuit.get(component_def).pins().front();
    const auto component = volt::test::instantiate_component(circuit, component_def, "J1");
    const auto pin = volt::queries::pin_by_definition(circuit, component, pin_def).value();
    const auto net_id =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"GND"}, .kind = volt::NetKind::Ground});
    CHECK(circuit.connect(net_id, pin));

    CHECK(net_id == volt::NetId{0});
    CHECK(circuit.net(net_id).name() == volt::NetName{"GND"});
    REQUIRE(circuit.net(net_id).pins().size() == 1);
    CHECK(circuit.net_count() == 1);
}

TEST_CASE("Legacy connectivity facade rejects preconnected nets with missing pins") {
    volt::Circuit circuit;
    auto net = volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground};
    net.connect(volt::PinId{99});

    // Preconnected Net insertion exists only on the transitional facade and remains until #266.
    CHECK_THROWS_AS(circuit.connectivity().add_net(std::move(net)), std::out_of_range);
}

TEST_CASE("Circuit connects existing pins to existing nets") {
    volt::Circuit circuit;
    const auto component_def =
        volt::test::define_component(circuit, "Resistor", {volt::test::passive_pin("1", "1")});
    const auto pin_def = circuit.get(component_def).pins().front();
    const auto component = volt::test::instantiate_component(circuit, component_def, "R1");
    const auto pin = volt::queries::pin_by_definition(circuit, component, pin_def).value();
    const auto net = circuit.add_net(volt::NetSpec{.name = volt::NetName{"NET_A"}});

    CHECK(circuit.connect(net, pin));
    CHECK_FALSE(circuit.connect(net, pin));
    REQUIRE(circuit.net(net).pins().size() == 1);
    CHECK(circuit.net(net).pins().front() == pin);
    REQUIRE(volt::queries::net_of(circuit, pin).has_value());
    CHECK(volt::queries::net_of(circuit, pin).value() == net);
}

TEST_CASE("Circuit rejects connect operations with missing IDs") {
    volt::Circuit circuit;
    const auto net = circuit.add_net(volt::NetSpec{.name = volt::NetName{"NET_A"}});

    try {
        static_cast<void>(circuit.connect(volt::NetId{99}, volt::PinId{0}));
        FAIL("Unknown net must throw");
    } catch (const volt::KernelRangeError &error) {
        CHECK(error.code() == volt::ErrorCode::UnknownEntity);
        CHECK(std::string{error.what()} == "Net ID does not belong to this circuit");
        REQUIRE(error.entity().has_value());
        CHECK(error.entity()->kind() == volt::EntityKind::Net);
        CHECK(error.entity()->index() == 99);
    }

    try {
        static_cast<void>(circuit.connect(net, volt::PinId{99}));
        FAIL("Unknown pin must throw");
    } catch (const volt::KernelRangeError &error) {
        CHECK(error.code() == volt::ErrorCode::UnknownEntity);
        CHECK(std::string{error.what()} == "Pin ID does not belong to this circuit");
        REQUIRE(error.entity().has_value());
        CHECK(error.entity()->kind() == volt::EntityKind::Pin);
        CHECK(error.entity()->index() == 99);
    }
}

TEST_CASE("Circuit enforces one net per concrete pin") {
    volt::Circuit circuit;
    const auto component_def =
        volt::test::define_component(circuit, "Resistor", {volt::test::passive_pin("1", "1")});
    const auto pin_def = circuit.get(component_def).pins().front();
    const auto component = volt::test::instantiate_component(circuit, component_def, "R1");
    const auto pin = volt::queries::pin_by_definition(circuit, component, pin_def).value();
    const auto first_net = circuit.add_net(volt::NetSpec{.name = volt::NetName{"NET_A"}});
    const auto second_net = circuit.add_net(volt::NetSpec{.name = volt::NetName{"NET_B"}});

    CHECK(circuit.connect(first_net, pin));
    try {
        static_cast<void>(circuit.connect(second_net, pin));
        FAIL("A pin on another net must throw");
    } catch (const volt::KernelLogicError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidState);
        CHECK(std::string{error.what()} == "Pin is already connected to another net");
    }
    CHECK(circuit.net(first_net).contains(pin));
    CHECK_FALSE(circuit.net(second_net).contains(pin));
}

TEST_CASE("Circuit disconnects a pin from its current net") {
    volt::Circuit circuit;
    const auto component_def =
        volt::test::define_component(circuit, "Resistor", {volt::test::passive_pin("1", "1")});
    const auto pin_def = circuit.get(component_def).pins().front();
    const auto component = volt::test::instantiate_component(circuit, component_def, "R1");
    const auto pin = volt::queries::pin_by_definition(circuit, component, pin_def).value();
    const auto net = circuit.add_net(volt::NetSpec{.name = volt::NetName{"NET_A"}});
    circuit.connect(net, pin);

    CHECK(circuit.disconnect(pin));
    CHECK_FALSE(circuit.disconnect(pin));
    CHECK_FALSE(volt::queries::net_of(circuit, pin).has_value());
    CHECK(circuit.net(net).pins().empty());

    try {
        static_cast<void>(circuit.disconnect(volt::PinId{99}));
        FAIL("Unknown pin must throw");
    } catch (const volt::KernelRangeError &error) {
        CHECK(error.code() == volt::ErrorCode::UnknownEntity);
        CHECK(std::string{error.what()} == "Pin ID does not belong to this circuit");
        REQUIRE(error.entity().has_value());
        CHECK(error.entity()->kind() == volt::EntityKind::Pin);
        CHECK(error.entity()->index() == 99);
    }
}

TEST_CASE("Circuit assigns and reads a selected physical part for a component") {
    volt::Circuit circuit;
    const auto component_def = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    const auto &pins = circuit.get(component_def).pins();
    const auto first_pin = pins[0];
    const auto second_pin = pins[1];
    const auto component = volt::test::instantiate_component(circuit, component_def, "R1");

    CHECK_FALSE(circuit.selected_physical_part(component).has_value());

    circuit.update(component,
                   volt::SelectPhysicalPart{make_resistor_physical_part(first_pin, second_pin)});

    const auto &selected_part = circuit.selected_physical_part(component);
    REQUIRE(selected_part.has_value());
    CHECK(selected_part->manufacturer_part().manufacturer() == "Yageo");
    CHECK(selected_part->manufacturer_part().part_number() == "RC0603FR-07330RL");
    CHECK(selected_part->package().value() == "0603");
    CHECK(selected_part->footprint().name() == "R_0603_1608Metric");
}

TEST_CASE("Circuit stores component assembly intent and selected part alternates") {
    volt::Circuit circuit;
    const auto component_def = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    const auto &pins = circuit.get(component_def).pins();
    const auto first_pin = pins[0];
    const auto second_pin = pins[1];
    const auto component = volt::test::instantiate_component(circuit, component_def, "R1");

    CHECK_FALSE(circuit.component_dnp(component).has_value());
    circuit.update(component, volt::SetAssemblyIntent{.selection_override = true});
    CHECK_FALSE(circuit.component_dnp(component).has_value());
    CHECK(circuit.is_component_selection_override(component));
    circuit.update(component, volt::SetAssemblyIntent{.selection_override = false});
    CHECK_FALSE(circuit.is_component_selection_override(component));
    CHECK(circuit.component_assembly_intents().empty());

    circuit.update(component, volt::SetAssemblyIntent{.dnp = true});
    circuit.update(component, volt::SetAssemblyIntent{.selection_override = true});

    circuit.update(component, volt::SelectPhysicalPart{volt::PhysicalPart{
                                  volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                  volt::PackageRef{"0603"},
                                  volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                  std::vector{volt::PinPadMapping{first_pin, "1"},
                                              volt::PinPadMapping{second_pin, "2"}},
                                  {},
                                  std::nullopt,
                                  std::vector<std::string>{"RC0603FR-07330RLA"},
                              }});

    CHECK(circuit.component_dnp(component) == std::optional<bool>{true});
    CHECK(circuit.is_component_selection_override(component));
    REQUIRE(circuit.selected_physical_part(component).has_value());
    CHECK(circuit.selected_physical_part(component)->approved_alternate_mpns() ==
          std::vector<std::string>{"RC0603FR-07330RLA"});
}

TEST_CASE("Circuit sets typed electrical attributes on selected physical parts") {
    volt::Circuit circuit;
    const auto component_def = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    const auto &pins = circuit.get(component_def).pins();
    const auto first_pin = pins[0];
    const auto second_pin = pins[1];
    const auto component = volt::test::instantiate_component(circuit, component_def, "R1");
    circuit.update(component,
                   volt::SelectPhysicalPart{make_resistor_physical_part(first_pin, second_pin)});
    const auto voltage_rating = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    circuit.update(component, volt::SetSelectedPartElectricalAttribute{
                                  voltage_rating, volt::ElectricalAttributeValue{volt::Quantity{
                                                      volt::UnitDimension::Voltage, 75.0}}});

    REQUIRE(circuit.selected_physical_part(component).has_value());
    CHECK(circuit.selected_physical_part(component)
              .value()
              .electrical_attributes()
              .get(volt::ElectricalAttributeName{"voltage_rating"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 75.0});
}

TEST_CASE("Circuit sets component instance properties through an explicit mutation API") {
    volt::Circuit circuit;
    const auto component_def =
        volt::test::define_component(circuit, "Test point", {volt::test::passive_pin("1", "1")});
    const auto component = volt::test::instantiate_component(circuit, component_def, "TP1");

    circuit.update(component, volt::SetComponentProperty{volt::PropertyKey{"value"},
                                                         volt::PropertyValue{"VCC"}});
    circuit.update(component, volt::SetComponentProperty{volt::PropertyKey{"fitted"},
                                                         volt::PropertyValue{true}});

    CHECK(circuit.component(component).properties().get(volt::PropertyKey{"value"}) ==
          volt::PropertyValue{"VCC"});
    CHECK(circuit.component(component).properties().get(volt::PropertyKey{"fitted"}) ==
          volt::PropertyValue{true});
}

TEST_CASE("Circuit sets typed electrical attributes on component instances") {
    volt::Circuit circuit;
    const auto component_def =
        volt::test::define_component(circuit, "Resistor", {volt::test::passive_pin("1", "1")});
    const auto component = volt::test::instantiate_component(circuit, component_def, "R1");
    const auto resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };

    circuit.update(component, volt::SetComponentElectricalAttribute{
                                  resistance, volt::ElectricalAttributeValue{volt::Quantity{
                                                  volt::UnitDimension::Resistance, 330.0}}});

    CHECK(circuit.component_electrical_attributes(component)
              .get(volt::ElectricalAttributeName{"resistance"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Resistance, 330.0});
}

TEST_CASE("Circuit sets typed electrical attributes on pin definitions") {
    volt::Circuit circuit;
    const auto voltage_range = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_range"},
        volt::ElectricalAttributeOwner::PinSpec,
        volt::ElectricalAttributeKind::Constraint,
        volt::UnitDimension::Voltage,
    };

    auto pin = volt::PinSpec{
        .name = "VCC",
        .number = "8",
        .terminal_kind = volt::ElectricalTerminalKind::Power,
        .direction = volt::ElectricalDirection::Input,
    };
    pin.electrical_attributes.push_back(volt::ElectricalAttributeAssignment{
        voltage_range,
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 4.5},
                                         volt::Quantity{volt::UnitDimension::Voltage, 16.0})},
    });
    const auto definition =
        circuit.define_component(volt::ComponentSpec{.name = "Supply", .pins = {std::move(pin)}});
    const auto pin_definition = circuit.get(definition).pins().front();

    const auto &stored_range = circuit.pin_definition_electrical_attributes(pin_definition)
                                   .get(volt::ElectricalAttributeName{"voltage_range"})
                                   .as_range();
    REQUIRE(stored_range.minimum().has_value());
    REQUIRE(stored_range.maximum().has_value());
    CHECK(stored_range.minimum().value() == volt::Quantity{volt::UnitDimension::Voltage, 4.5});
    CHECK(stored_range.maximum().value() == volt::Quantity{volt::UnitDimension::Voltage, 16.0});
}

TEST_CASE("Circuit sets typed electrical attributes on nets") {
    volt::Circuit circuit;
    const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"3V3"}, .kind = volt::NetKind::Power});
    const auto voltage = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    circuit.update(
        net, volt::SetNetElectricalAttribute{voltage, volt::ElectricalAttributeValue{volt::Quantity{
                                                          volt::UnitDimension::Voltage, 3.3}}});

    CHECK(circuit.net_electrical_attributes(net)
              .get(volt::ElectricalAttributeName{"voltage"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 3.3});
}

TEST_CASE("Circuit rejects component property mutation for missing components") {
    volt::Circuit circuit;

    CHECK_THROWS_AS(circuit.update(volt::ComponentId{99},
                                   volt::SetComponentProperty{volt::PropertyKey{"value"},
                                                              volt::PropertyValue{"VCC"}}),
                    std::out_of_range);
}

TEST_CASE("Circuit rejects incompatible component electrical attributes") {
    volt::Circuit circuit;
    const auto component_def =
        volt::test::define_component(circuit, "Resistor", {volt::test::passive_pin("1", "1")});
    const auto component = volt::test::instantiate_component(circuit, component_def, "R1");
    const auto resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };
    const auto selected_part_rating = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    CHECK_THROWS_AS(circuit.update(component,
                                   volt::SetComponentElectricalAttribute{
                                       resistance, volt::ElectricalAttributeValue{volt::Quantity{
                                                       volt::UnitDimension::Voltage, 3.3}}}),
                    std::invalid_argument);
    try {
        circuit.update(component, volt::SetComponentElectricalAttribute{
                                      resistance, volt::ElectricalAttributeValue{volt::Quantity{
                                                      volt::UnitDimension::Voltage, 3.3}}});
        FAIL("Electrical attribute dimension mismatch must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
    }
    CHECK_THROWS_AS(
        circuit.update(component,
                       volt::SetComponentElectricalAttribute{
                           selected_part_rating, volt::ElectricalAttributeValue{volt::Quantity{
                                                     volt::UnitDimension::Voltage, 75.0}}}),
        std::logic_error);
    CHECK_THROWS_AS(circuit.update(volt::ComponentId{99},
                                   volt::SetComponentElectricalAttribute{
                                       resistance, volt::ElectricalAttributeValue{volt::Quantity{
                                                       volt::UnitDimension::Resistance, 330.0}}}),
                    std::out_of_range);
}

TEST_CASE("Circuit rejects incompatible pin definition electrical attributes") {
    volt::Circuit circuit;
    const auto voltage_range = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_range"},
        volt::ElectricalAttributeOwner::PinSpec,
        volt::ElectricalAttributeKind::Constraint,
        volt::UnitDimension::Voltage,
    };
    const auto component_resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };

    auto wrong_dimension = volt::PinSpec{.name = "VCC", .number = "8"};
    wrong_dimension.electrical_attributes.push_back(volt::ElectricalAttributeAssignment{
        voltage_range,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Current, 0.01}},
    });
    CHECK_THROWS_AS(circuit.define_component(volt::ComponentSpec{
                        .name = "Wrong dimension", .pins = {std::move(wrong_dimension)}}),
                    std::invalid_argument);

    auto wrong_owner = volt::PinSpec{.name = "VCC", .number = "8"};
    wrong_owner.electrical_attributes.push_back(volt::ElectricalAttributeAssignment{
        component_resistance,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}},
    });
    CHECK_THROWS_AS(circuit.define_component(volt::ComponentSpec{.name = "Wrong owner",
                                                                 .pins = {std::move(wrong_owner)}}),
                    std::logic_error);
}

TEST_CASE("Circuit rejects incompatible net electrical attributes") {
    volt::Circuit circuit;
    const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"3V3"}, .kind = volt::NetKind::Power});
    const auto voltage = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };
    const auto component_resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };

    CHECK_THROWS_AS(circuit.update(net,
                                   volt::SetNetElectricalAttribute{
                                       voltage, volt::ElectricalAttributeValue{volt::Quantity{
                                                    volt::UnitDimension::Current, 0.01}}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        circuit.update(
            net, volt::SetNetElectricalAttribute{component_resistance,
                                                 volt::ElectricalAttributeValue{volt::Quantity{
                                                     volt::UnitDimension::Resistance, 330.0}}}),
        std::logic_error);
    CHECK_THROWS_AS(circuit.update(volt::NetId{99},
                                   volt::SetNetElectricalAttribute{
                                       voltage, volt::ElectricalAttributeValue{volt::Quantity{
                                                    volt::UnitDimension::Voltage, 3.3}}}),
                    std::out_of_range);
}

TEST_CASE("Circuit rejects selected-part operations for missing components") {
    volt::Circuit circuit;
    const auto definition = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    const auto &pins = circuit.get(definition).pins();
    const auto first_pin = pins[0];
    const auto second_pin = pins[1];

    CHECK_THROWS_AS(circuit.update(volt::ComponentId{99},
                                   volt::SelectPhysicalPart{
                                       make_resistor_physical_part(first_pin, second_pin)}),
                    std::out_of_range);
    CHECK_THROWS_AS(circuit.selected_physical_part(volt::ComponentId{99}), std::out_of_range);
}

TEST_CASE("Circuit rejects incompatible selected part electrical attributes") {
    volt::Circuit circuit;
    const auto component_def = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    const auto &pins = circuit.get(component_def).pins();
    const auto first_pin = pins[0];
    const auto second_pin = pins[1];
    const auto component = volt::test::instantiate_component(circuit, component_def, "R1");
    const auto voltage_rating = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };
    const auto component_resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };

    CHECK_THROWS_AS(
        circuit.update(component,
                       volt::SetSelectedPartElectricalAttribute{
                           voltage_rating, volt::ElectricalAttributeValue{volt::Quantity{
                                               volt::UnitDimension::Voltage, 75.0}}}),
        std::logic_error);

    circuit.update(component,
                   volt::SelectPhysicalPart{make_resistor_physical_part(first_pin, second_pin)});

    CHECK_THROWS_AS(
        circuit.update(component,
                       volt::SetSelectedPartElectricalAttribute{
                           voltage_rating, volt::ElectricalAttributeValue{volt::Quantity{
                                               volt::UnitDimension::Current, 1.0}}}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        circuit.update(component,
                       volt::SetSelectedPartElectricalAttribute{
                           component_resistance, volt::ElectricalAttributeValue{volt::Quantity{
                                                     volt::UnitDimension::Resistance, 330.0}}}),
        std::logic_error);
}

TEST_CASE("Circuit rejects selected parts with mappings outside the component definition") {
    volt::Circuit circuit;
    const auto component_def = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    const auto &pins = circuit.get(component_def).pins();
    const auto first_pin = pins[0];
    const auto foreign_definition =
        volt::test::define_component(circuit, "Foreign", {volt::test::passive_pin("3", "3")});
    const auto foreign_pin = circuit.get(foreign_definition).pins().front();
    const auto component = volt::test::instantiate_component(circuit, component_def, "R1");

    CHECK_THROWS_AS(circuit.update(component, volt::SelectPhysicalPart{make_resistor_physical_part(
                                                  first_pin, foreign_pin)}),
                    std::logic_error);
    CHECK_FALSE(circuit.selected_physical_part(component).has_value());
}

TEST_CASE("Circuit rejects selected parts that do not map every component-definition pin") {
    volt::Circuit circuit;
    const auto component_def = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    const auto &pins = circuit.get(component_def).pins();
    const auto first_pin = pins[0];
    const auto component = volt::test::instantiate_component(circuit, component_def, "R1");
    auto incomplete_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::PinPadMapping{first_pin, "1"},
        },
    };

    CHECK_THROWS_AS(circuit.update(component, volt::SelectPhysicalPart{std::move(incomplete_part)}),
                    std::logic_error);
    CHECK_FALSE(circuit.selected_physical_part(component).has_value());
}

TEST_CASE("Circuit copies keep name lookups independent and uniqueness enforced") {
    volt::Circuit circuit;
    const auto component_def =
        volt::test::define_component(circuit, "Regulator", {volt::test::passive_pin("VDD", "1")});
    const auto component = volt::test::instantiate_component(circuit, component_def, "U1");
    const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"VCC"}, .kind = volt::NetKind::Power});
    const auto pin = volt::queries::pin_by_number(circuit, component, "1").value();
    CHECK(circuit.connect(net, pin));

    auto copy = circuit;

    CHECK(volt::queries::component_by_reference(copy, volt::ReferenceDesignator{"U1"}) ==
          component);
    CHECK(volt::queries::net_by_name(copy, volt::NetName{"VCC"}) == net);
    CHECK(volt::queries::net_of(copy, pin) == net);
    CHECK_THROWS_AS(copy.instantiate_component(
                        component_def,
                        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}}),
                    std::logic_error);
    CHECK_THROWS_AS(
        copy.add_net(volt::NetSpec{.name = volt::NetName{"VCC"}, .kind = volt::NetKind::Power}),
        std::logic_error);

    const auto copy_only = copy.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto copy_only_net =
        copy.add_net(volt::NetSpec{.name = volt::NetName{"GND"}, .kind = volt::NetKind::Ground});
    CHECK(copy.disconnect(pin));

    CHECK(volt::queries::component_by_reference(copy, volt::ReferenceDesignator{"U2"}) ==
          copy_only);
    CHECK(volt::queries::net_by_name(copy, volt::NetName{"GND"}) == copy_only_net);
    CHECK_FALSE(volt::queries::net_of(copy, pin).has_value());

    CHECK_FALSE(volt::queries::component_by_reference(circuit, volt::ReferenceDesignator{"U2"})
                    .has_value());
    CHECK_FALSE(volt::queries::net_by_name(circuit, volt::NetName{"GND"}).has_value());
    CHECK(volt::queries::net_of(circuit, pin) == net);
    CHECK(circuit.component_count() == 1);
    CHECK(circuit.net_count() == 1);
}

TEST_CASE("Moved-from circuits reset to empty and stay safely usable") {
    volt::Circuit circuit;
    const auto component_def =
        volt::test::define_component(circuit, "Regulator", {volt::test::passive_pin("VDD", "1")});
    [[maybe_unused]] const auto component =
        volt::test::instantiate_component(circuit, component_def, "U1");
    [[maybe_unused]] const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"VCC"}, .kind = volt::NetKind::Power});

    const auto moved = std::move(circuit);

    CHECK(moved.component_count() == 1);
    CHECK(moved.net_count() == 1);
    CHECK(circuit.component_count() == 0);
    CHECK(circuit.pin_count() == 0);
    CHECK(circuit.net_count() == 0);
    CHECK_FALSE(volt::queries::component_by_reference(circuit, volt::ReferenceDesignator{"U1"})
                    .has_value());

    const auto reused_def =
        volt::test::define_component(circuit, "Regulator", {volt::test::passive_pin("VDD", "1")});
    [[maybe_unused]] const auto reused =
        circuit.instantiate_component(reused_def, volt::ReferenceDesignator{"U1"});
    CHECK(circuit.component_count() == 1);
    CHECK(moved.component_count() == 1);

    volt::Circuit assigned;
    assigned = std::move(circuit);
    CHECK(assigned.component_count() == 1);
    CHECK(circuit.component_count() == 0);
    [[maybe_unused]] const auto after_move_assign =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"VCC"}, .kind = volt::NetKind::Power});
    CHECK(circuit.net_count() == 1);
    CHECK(assigned.net_count() == 0);
}

TEST_CASE("Structural rejections carry machine-readable error codes") {
    volt::Circuit circuit;
    const auto component_def =
        volt::test::define_component(circuit, "Regulator", {volt::test::passive_pin("VDD", "1")});
    [[maybe_unused]] const auto component =
        volt::test::instantiate_component(circuit, component_def, "U1");

    try {
        [[maybe_unused]] const auto duplicate =
            circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
        FAIL("Duplicate reference designator must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::DuplicateName);
    }

    try {
        circuit.update(volt::ComponentId{42}, volt::SetComponentProperty{volt::PropertyKey{"k"},
                                                                         volt::PropertyValue{"v"}});
        FAIL("Unknown component ID must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::UnknownEntity);
        REQUIRE(error.entity().has_value());
        CHECK(error.entity()->kind() == volt::EntityKind::Component);
        CHECK(error.entity()->index() == 42);
    }

    try {
        [[maybe_unused]] const auto empty_name = volt::NetName{""};
        FAIL("Empty net name must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
    }
}
