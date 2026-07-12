#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

#include <support/circuit_test_helpers.hpp>

namespace {

[[nodiscard]] volt::PhysicalPart resistor_part(volt::PinDefId first, volt::PinDefId second) {
    return volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{volt::PinPadMapping{first, "1"}, volt::PinPadMapping{second, "2"}},
    };
}

template <typename Operation>
void check_failure_is_byte_atomic(volt::Circuit &circuit, volt::ErrorCode expected_code,
                                  std::string_view expected_message, Operation operation) {
    const auto before = volt::io::write_logical_circuit(circuit);
    try {
        operation();
        FAIL("Invalid typed update must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == expected_code);
        CHECK(std::string{error.what()} == std::string{expected_message});
    }
    CHECK(volt::io::write_logical_circuit(circuit) == before);
}

} // namespace

TEST_CASE("Circuit closed typed updates preserve every progressive semantic") {
    auto circuit = volt::Circuit{};
    const auto definition = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    const auto &definition_pins = circuit.get(definition).pins();
    const auto component = volt::test::instantiate_component(circuit, definition, "R1");
    const auto net = volt::test::add_net(circuit, "VCC", volt::NetKind::Power);
    const auto component_attribute = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };
    const auto selected_part_attribute = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::Constraint,
        volt::UnitDimension::Voltage,
    };
    const auto net_attribute = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };
    auto net_class = volt::NetClass{volt::NetClassName{"Power"}};
    net_class.set_track_width_mm(0.5);
    const auto net_class_id =
        circuit.define_net_class(volt::NetClassSpec{.net_class = std::move(net_class)});

    circuit.update(component, volt::SetComponentProperty{volt::PropertyKey{"value"},
                                                         volt::PropertyValue{"330 ohm"}});
    circuit.update(component,
                   volt::SetComponentElectricalAttribute{
                       component_attribute, volt::ElectricalAttributeValue{volt::Quantity{
                                                volt::UnitDimension::Resistance, 330.0}}});
    circuit.update(component,
                   volt::SelectPhysicalPart{resistor_part(definition_pins[0], definition_pins[1])});
    circuit.update(component,
                   volt::SetSelectedPartElectricalAttribute{
                       selected_part_attribute, volt::ElectricalAttributeValue{volt::Quantity{
                                                    volt::UnitDimension::Voltage, 75.0}}});
    circuit.update(component, volt::SetAssemblyIntent{.dnp = true, .selection_override = true});
    circuit.update(net, volt::SetNetElectricalAttribute{
                            net_attribute, volt::ElectricalAttributeValue{
                                               volt::Quantity{volt::UnitDimension::Voltage, 3.3}}});
    circuit.update(net, volt::AssignNetClass{net_class_id});
    circuit.update(net, volt::MarkIntentionalStub{});
    circuit.mark_no_connect(volt::queries::pin_by_number(circuit, component, "2").value());

    CHECK(circuit.get(component).properties().get(volt::PropertyKey{"value"}) ==
          volt::PropertyValue{"330 ohm"});
    CHECK(circuit.component_electrical_attributes(component)
              .get(volt::ElectricalAttributeName{"resistance"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Resistance, 330.0});
    REQUIRE(circuit.selected_physical_part(component).has_value());
    CHECK(circuit.selected_physical_part(component)
              ->electrical_attributes()
              .get(volt::ElectricalAttributeName{"voltage_rating"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 75.0});
    CHECK(circuit.component_dnp(component) == true);
    CHECK(circuit.is_component_selection_override(component));
    CHECK(circuit.net_electrical_attributes(net)
              .get(volt::ElectricalAttributeName{"voltage"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 3.3});
    CHECK(circuit.net_class_for_net(net) == net_class_id);
    CHECK(circuit.is_intentional_stub_net(net));
    CHECK(circuit.is_intentional_no_connect_pin(
        volt::queries::pin_by_number(circuit, component, "2").value()));

    const auto serialized = volt::io::write_logical_circuit(circuit);
    circuit.update(component, volt::SetComponentProperty{volt::PropertyKey{"value"},
                                                         volt::PropertyValue{"330 ohm"}});
    circuit.update(component,
                   volt::SetComponentElectricalAttribute{
                       component_attribute, volt::ElectricalAttributeValue{volt::Quantity{
                                                volt::UnitDimension::Resistance, 330.0}}});
    circuit.update(component,
                   volt::SelectPhysicalPart{resistor_part(definition_pins[0], definition_pins[1])});
    circuit.update(component,
                   volt::SetSelectedPartElectricalAttribute{
                       selected_part_attribute, volt::ElectricalAttributeValue{volt::Quantity{
                                                    volt::UnitDimension::Voltage, 75.0}}});
    circuit.update(component, volt::SetAssemblyIntent{.dnp = true, .selection_override = true});
    circuit.update(net, volt::SetNetElectricalAttribute{
                            net_attribute, volt::ElectricalAttributeValue{
                                               volt::Quantity{volt::UnitDimension::Voltage, 3.3}}});
    circuit.update(net, volt::AssignNetClass{net_class_id});
    circuit.update(net, volt::MarkIntentionalStub{});
    circuit.mark_no_connect(volt::queries::pin_by_number(circuit, component, "2").value());

    CHECK(volt::io::write_logical_circuit(circuit) == serialized);
    CHECK(volt::io::write_logical_circuit(volt::io::read_logical_circuit_text(serialized)) ==
          serialized);
}

TEST_CASE("Circuit typed update failures leave canonical bytes unchanged") {
    auto circuit = volt::Circuit{};
    const auto definition =
        volt::test::define_component(circuit, "Test point", {volt::test::passive_pin("1", "1")});
    const auto component = volt::test::instantiate_component(circuit, definition, "TP1");
    const auto &definition_pins = circuit.get(definition).pins();
    const auto net = volt::test::add_net(circuit, "TEST");
    const auto component_attribute = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };
    const auto net_attribute = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    const auto missing_component = volt::ComponentId{99};
    const auto missing_net = volt::NetId{99};
    const auto missing_component_message = "Component ID does not belong to this circuit";
    const auto missing_net_message = "Net ID does not belong to this circuit";

    check_failure_is_byte_atomic(
        circuit, volt::ErrorCode::UnknownEntity, missing_component_message, [&] {
            circuit.update(missing_component,
                           volt::SetComponentProperty{volt::PropertyKey{"value"},
                                                      volt::PropertyValue{"missing"}});
        });
    check_failure_is_byte_atomic(
        circuit, volt::ErrorCode::UnknownEntity, missing_component_message, [&] {
            circuit.update(missing_component,
                           volt::SetComponentElectricalAttribute{
                               component_attribute, volt::ElectricalAttributeValue{volt::Quantity{
                                                        volt::UnitDimension::Resistance, 1.0}}});
        });
    check_failure_is_byte_atomic(
        circuit, volt::ErrorCode::UnknownEntity, missing_component_message, [&] {
            circuit.update(missing_component,
                           volt::SelectPhysicalPart{
                               resistor_part(definition_pins.front(), definition_pins.front())});
        });
    check_failure_is_byte_atomic(
        circuit, volt::ErrorCode::UnknownEntity, missing_component_message, [&] {
            circuit.update(missing_component,
                           volt::SetSelectedPartElectricalAttribute{
                               component_attribute, volt::ElectricalAttributeValue{volt::Quantity{
                                                        volt::UnitDimension::Resistance, 1.0}}});
        });
    check_failure_is_byte_atomic(
        circuit, volt::ErrorCode::UnknownEntity, missing_component_message,
        [&] { circuit.update(missing_component, volt::SetAssemblyIntent{.dnp = true}); });

    check_failure_is_byte_atomic(
        circuit, volt::ErrorCode::InvalidArgument,
        "Electrical attribute value dimension does not match spec", [&] {
            circuit.update(component,
                           volt::SetComponentElectricalAttribute{
                               component_attribute, volt::ElectricalAttributeValue{volt::Quantity{
                                                        volt::UnitDimension::Voltage, 3.3}}});
        });
    check_failure_is_byte_atomic(circuit, volt::ErrorCode::InvalidArgument,
                                 "Assembly intent update must set DNP or selection override intent",
                                 [&] { circuit.update(component, volt::SetAssemblyIntent{}); });

    check_failure_is_byte_atomic(circuit, volt::ErrorCode::UnknownEntity, missing_net_message, [&] {
        circuit.update(missing_net,
                       volt::SetNetElectricalAttribute{
                           net_attribute, volt::ElectricalAttributeValue{
                                              volt::Quantity{volt::UnitDimension::Voltage, 3.3}}});
    });
    check_failure_is_byte_atomic(circuit, volt::ErrorCode::UnknownEntity, missing_net_message, [&] {
        circuit.update(missing_net, volt::AssignNetClass{volt::NetClassId{99}});
    });
    check_failure_is_byte_atomic(circuit, volt::ErrorCode::UnknownEntity, missing_net_message,
                                 [&] { circuit.update(missing_net, volt::MarkIntentionalStub{}); });

    check_failure_is_byte_atomic(
        circuit, volt::ErrorCode::InvalidArgument,
        "Electrical attribute value dimension does not match spec", [&] {
            circuit.update(net, volt::SetNetElectricalAttribute{
                                    net_attribute, volt::ElectricalAttributeValue{volt::Quantity{
                                                       volt::UnitDimension::Current, 1.0}}});
        });
    check_failure_is_byte_atomic(
        circuit, volt::ErrorCode::UnknownEntity, "Net class ID is out of range",
        [&] { circuit.update(net, volt::AssignNetClass{volt::NetClassId{99}}); });
    check_failure_is_byte_atomic(circuit, volt::ErrorCode::UnknownEntity,
                                 "Pin ID does not belong to this circuit",
                                 [&] { circuit.mark_no_connect(volt::PinId{99}); });
}
