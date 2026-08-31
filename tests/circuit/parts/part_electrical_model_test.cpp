#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/parts/part_definition.hpp>
#include <volt/library/part_library.hpp>

namespace {

constexpr auto asset_bytes = "passive-part-test-footprint";

volt::ComponentSpec passive_spec(std::string key = "test.component/passive@1") {
    return volt::ComponentSpec{
        .name = "Passive",
        .pins = {volt::PinSpec{.name = "A", .number = "1"},
                 volt::PinSpec{.name = "B", .number = "2"}},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{std::move(key)},
                .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"B"}},
            },
    };
}

volt::PartDefinition exact_part(const volt::ComponentDefinition &component,
                                std::optional<volt::PartElectricalModel> model = std::nullopt) {
    return volt::PartDefinition{
        component,
        volt::PartIdentity{"test.passives", "demonstration", "1"},
        volt::ElectricalRecordSet{2},
        {volt::PinPackageTerminalMapping{volt::PinKey{"A"}, {volt::PackageTerminalKey{"1"}}},
         volt::PinPackageTerminalMapping{volt::PinKey{"B"}, {volt::PackageTerminalKey{"2"}}}},
        {},
        volt::PartProvenance{},
        {},
        volt::OrderablePart{
            volt::ManufacturerPart{"Test", "demonstration"},
            volt::PackageRef{"0603"},
            volt::HashedFootprintReference{volt::FootprintRef{"Test", "0603"},
                                           volt::sha256_content_hash(asset_bytes)},
            {volt::PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5},
             volt::PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5}},
            {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                             {volt::FootprintPadKey{"1"}}},
             volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                             {volt::FootprintPadKey{"2"}}}},
        },
        std::move(model),
    };
}

volt::PartElectricalModel composite_model(const volt::ComponentDefinition &component) {
    auto builder = volt::PartElectricalModelBuilder{component};
    const auto a = builder.terminal(volt::ModelTerminalKey{"a"}, volt::PinKey{"A"});
    const auto b = builder.terminal(volt::ModelTerminalKey{"b"}, volt::PinKey{"B"});
    const auto x = builder.internal_node(volt::ModelInternalNodeKey{"x"});
    builder.add<volt::ResistanceElement>(
        volt::ModelElementKey{"esr"}, a, x,
        volt::ModelParameter{
            volt::Quantity{volt::UnitDimension::Resistance, 10.0},
            volt::Tolerance::percent(0.1),
            {volt::sha256_content_hash("evidence-a"), volt::sha256_content_hash("evidence-b")}});
    builder.add<volt::CapacitanceElement>(
        volt::ModelElementKey{"storage"}, x, b,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Capacitance, 2.0}});
    return builder.build();
}

class AssetResolver final : public volt::PartAssetResolver {
  public:
    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &) const override {
        return asset_bytes;
    }
};

} // namespace

TEST_CASE("Exact Part owns an independent optional model and checks component identity") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(passive_spec());
    const auto foreign = circuit.define_component(passive_spec("test.component/other@1"));
    const auto &component = circuit.get(definition);
    const auto digest = component.content_identity();
    const auto absent = exact_part(component);
    auto model = composite_model(component);
    const auto present = exact_part(component, model);
    const auto copy = present;

    CHECK_FALSE(absent.electrical_model().has_value());
    REQUIRE(present.electrical_model().has_value());
    CHECK(present.content_identity() != absent.content_identity());
    CHECK(copy.content_identity() == present.content_identity());
    CHECK(present.implemented_component() == digest);
    CHECK(component.content_identity() == digest);
    CHECK_THROWS_AS(exact_part(circuit.get(foreign), model), std::invalid_argument);

    model = composite_model(circuit.get(foreign));
    CHECK(present.electrical_model()->implemented_component() == digest);
    CHECK(copy.electrical_model()->elements().size() == 2U);
}

TEST_CASE("Exact Part model identity canonicalizes declaration order tolerance and evidence") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(passive_spec());
    const auto &component = circuit.get(definition);
    const auto model = composite_model(component);
    const auto original = exact_part(component, model);
    auto terminals = model.terminals();
    auto nodes = model.internal_nodes();
    auto elements = model.elements();
    const auto &resistance = std::get<volt::ResistanceElement>(elements[0]);
    auto evidence = resistance.parameter().evidence();
    std::ranges::reverse(evidence);
    evidence.push_back(evidence.front());
    elements[0] = volt::ResistanceElement{
        resistance.key(), resistance.from(), resistance.to(),
        volt::ModelParameter{
            volt::Quantity{volt::UnitDimension::Resistance, 10.0},
            volt::Tolerance::absolute(volt::Quantity{volt::UnitDimension::Resistance, 1.0},
                                      volt::Quantity{volt::UnitDimension::Resistance, 1.0}),
            evidence}};
    std::ranges::reverse(terminals);
    std::ranges::reverse(elements);

    const auto equivalent =
        exact_part(component, volt::PartElectricalModel{component, terminals, nodes, elements});
    CHECK(equivalent.content_identity() == original.content_identity());
    CHECK(exact_part(component, composite_model(component)).content_identity() ==
          original.content_identity());
}

TEST_CASE("Every canonical passive model field contributes to exact Part identity") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(passive_spec());
    const auto &component = circuit.get(definition);
    const auto model = composite_model(component);
    auto terminals = model.terminals();
    auto nodes = model.internal_nodes();
    auto elements = model.elements();
    const auto resistance = std::get<volt::ResistanceElement>(elements[0]);
    const auto capacitance = std::get<volt::CapacitanceElement>(elements[1]);
    const auto replace_parameter = [&](volt::ModelParameter parameter) {
        elements[0] = volt::ResistanceElement{resistance.key(), resistance.from(), resistance.to(),
                                              std::move(parameter)};
    };

    SECTION("nominal") {
        replace_parameter(volt::ModelParameter{
            volt::Quantity{volt::UnitDimension::Resistance, 11.0},
            resistance.parameter().tolerance(), resistance.parameter().evidence()});
    }
    SECTION("tolerance presence") {
        replace_parameter(volt::ModelParameter{resistance.parameter().nominal(), std::nullopt,
                                               resistance.parameter().evidence()});
    }
    SECTION("tolerance value") {
        replace_parameter(volt::ModelParameter{resistance.parameter().nominal(),
                                               volt::Tolerance::percent(0.05),
                                               resistance.parameter().evidence()});
    }
    SECTION("evidence") {
        auto evidence = resistance.parameter().evidence();
        evidence.push_back(volt::sha256_content_hash("evidence-c"));
        replace_parameter(volt::ModelParameter{resistance.parameter().nominal(),
                                               resistance.parameter().tolerance(), evidence});
    }
    SECTION("terminal binding") {
        terminals = {volt::ModelTerminal{volt::ModelTerminalKey{"a"}, volt::PinKey{"B"}},
                     volt::ModelTerminal{volt::ModelTerminalKey{"b"}, volt::PinKey{"A"}}};
    }
    SECTION("terminal key") {
        terminals[0] = volt::ModelTerminal{volt::ModelTerminalKey{"renamed"}, volt::PinKey{"A"}};
        elements[0] = volt::ResistanceElement{resistance.key(), volt::ModelTerminalKey{"renamed"},
                                              resistance.to(), resistance.parameter()};
    }
    SECTION("internal node key") {
        nodes[0] = volt::ModelInternalNode{volt::ModelInternalNodeKey{"renamed"}};
        elements[0] =
            volt::ResistanceElement{resistance.key(), resistance.from(),
                                    volt::ModelInternalNodeKey{"renamed"}, resistance.parameter()};
        elements[1] =
            volt::CapacitanceElement{capacitance.key(), volt::ModelInternalNodeKey{"renamed"},
                                     capacitance.to(), capacitance.parameter()};
    }
    SECTION("element key") {
        elements[0] = volt::ResistanceElement{volt::ModelElementKey{"renamed"}, resistance.from(),
                                              resistance.to(), resistance.parameter()};
    }
    SECTION("orientation") {
        elements[0] = volt::ResistanceElement{resistance.key(), resistance.to(), resistance.from(),
                                              resistance.parameter()};
    }
    SECTION("element kind") {
        elements[1] = volt::InductanceElement{
            capacitance.key(), capacitance.from(), capacitance.to(),
            volt::ModelParameter{volt::Quantity{volt::UnitDimension::Inductance, 2.0}}};
    }

    const auto changed =
        exact_part(component, volt::PartElectricalModel{component, terminals, nodes, elements});
    CHECK(changed.content_identity() != exact_part(component, model).content_identity());
}

TEST_CASE("Zero tolerance and unspecified uncertainty have different Part identities") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(passive_spec());
    const auto &component = circuit.get(definition);
    auto builder = volt::PartElectricalModelBuilder{component};
    const auto a = builder.terminal(volt::ModelTerminalKey{"a"}, volt::PinKey{"A"});
    const auto b = builder.terminal(volt::ModelTerminalKey{"b"}, volt::PinKey{"B"});
    builder.add<volt::ResistanceElement>(
        volt::ModelElementKey{"ideal-short"}, a, b,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, -0.0}});
    const auto unspecified = builder.build();
    const auto &element = std::get<volt::ResistanceElement>(unspecified.elements()[0]);
    const auto positive_zero = volt::PartElectricalModel{
        component,
        unspecified.terminals(),
        {},
        {volt::ResistanceElement{
            element.key(), element.from(), element.to(),
            volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, 0.0}}}}};
    CHECK(exact_part(component, unspecified).content_identity() ==
          exact_part(component, positive_zero).content_identity());
    const auto explicit_zero = volt::PartElectricalModel{
        component,
        unspecified.terminals(),
        {},
        {volt::ResistanceElement{
            element.key(), element.from(), element.to(),
            volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, 0.0},
                                 volt::Tolerance::percent(-0.0)}}}};
    CHECK(exact_part(component, unspecified).content_identity() !=
          exact_part(component, explicit_zero).content_identity());
}

TEST_CASE("Occurrence attributes remain independent of selected intrinsic passive truth") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(passive_spec());
    const auto &component = circuit.get(definition);
    auto builder = volt::PartElectricalModelBuilder{component};
    const auto a = builder.terminal(volt::ModelTerminalKey{"a"}, volt::PinKey{"A"});
    const auto b = builder.terminal(volt::ModelTerminalKey{"b"}, volt::PinKey{"B"});
    builder.add<volt::ResistanceElement>(
        volt::ModelElementKey{"body"}, a, b,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, 330.0}});
    const auto part = exact_part(component, builder.build());
    auto library_builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.passives", "1", volt::PartLibrarySchemaVersion::V1}};
    library_builder.add_component(component).add_part(part);
    const auto library = library_builder.build(AssetResolver{});
    const auto selected = library.require(volt::PartKey{"demonstration"});
    const auto instance = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    circuit.update(instance, volt::SetComponentElectricalAttribute{
                                 volt::ElectricalAttributeSpec{
                                     volt::ElectricalAttributeName{"resistance"},
                                     volt::ElectricalAttributeOwner::ComponentInstance,
                                     volt::ElectricalAttributeKind::DesignInput,
                                     volt::UnitDimension::Resistance},
                                 volt::ElectricalAttributeValue{
                                     volt::Quantity{volt::UnitDimension::Resistance, 470.0}}});
    circuit.update(instance,
                   volt::SetComponentElectricalAttribute{
                       volt::ElectricalAttributeSpec{
                           volt::ElectricalAttributeName{"tolerance"},
                           volt::ElectricalAttributeOwner::ComponentInstance,
                           volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Ratio},
                       volt::ElectricalAttributeValue{volt::Tolerance::percent(0.5)}});
    circuit.update(instance, volt::SelectLibraryPart{library, selected});
    const auto &resolved = library.resolve(*circuit.get(instance).selected_library_part_ref());
    REQUIRE(resolved.electrical_model().has_value());
    CHECK(std::get<volt::ResistanceElement>(resolved.electrical_model()->elements()[0])
              .parameter()
              .nominal()
              .value() == 330.0);
    CHECK(resolved.content_identity() == part.content_identity());
    CHECK_FALSE(std::get<volt::ResistanceElement>(resolved.electrical_model()->elements()[0])
                    .parameter()
                    .tolerance()
                    .has_value());
    CHECK(volt::queries::component_electrical_attributes(circuit, instance)
              .get(volt::ElectricalAttributeName{"resistance"})
              .as_quantity()
              .value() == 470.0);
    CHECK(volt::queries::component_electrical_attributes(circuit, instance)
              .get(volt::ElectricalAttributeName{"tolerance"})
              .as_tolerance()
              .plus()
              .value() == 0.5);

    auto absent_builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.passives", "1", volt::PartLibrarySchemaVersion::V1}};
    absent_builder.add_component(component).add_part(exact_part(component));
    const auto absent_library = absent_builder.build(AssetResolver{});
    circuit.update(instance,
                   volt::SelectLibraryPart{absent_library,
                                           absent_library.require(volt::PartKey{"demonstration"})});
    CHECK_FALSE(absent_library.resolve(*circuit.get(instance).selected_library_part_ref())
                    .electrical_model()
                    .has_value());
}
