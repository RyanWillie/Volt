#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/component_contract.hpp>
#include <volt/core/errors.hpp>
#include <volt/core/properties.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

namespace {

[[nodiscard]] std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] volt::FeatureSchema custom_diode_schema() {
    return volt::FeatureSchema{
        volt::FeatureSchemaKey{"volt.feature/diode-junction@1"},
        volt::ElectricalSubjectKind::DirectedRelation,
        {volt::FeatureRole{volt::FeatureRoleKey{"positive"},
                           volt::FeatureRoleCardinality::ExactlyOne},
         volt::FeatureRole{volt::FeatureRoleKey{"negative"},
                           volt::FeatureRoleCardinality::ExactlyOne}},
        {{volt::ElectricalObservable::Current, volt::ElectricalMeaning::AbsoluteLimit},
         {volt::ElectricalObservable::Voltage, volt::ElectricalMeaning::AbsoluteLimit},
         {volt::ElectricalObservable::Voltage, volt::ElectricalMeaning::Characteristic}}};
}

[[nodiscard]] volt::ComponentSpec led_spec(volt::FeatureSchema schema) {
    auto contract = volt::ComponentContractSpec{
        .key = volt::ComponentKey{"volt/led@1"},
        .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"K"}},
        .relations = {volt::ContractDirectedRelation{volt::RelationKey{"junction"},
                                                     volt::PinKey{"A"}, volt::PinKey{"K"}}},
        .feature_schemas = {std::move(schema)},
        .feature_bindings = {volt::FeatureBinding{
            volt::FeatureKey{"diode"},
            volt::FeatureSchemaKey{"volt.feature/diode-junction@1"},
            volt::ComponentSubjectRef::directed_relation(volt::RelationKey{"junction"}),
            {volt::FeatureRoleBinding{volt::FeatureRoleKey{"negative"}, {volt::PinKey{"K"}}},
             volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"}, {volt::PinKey{"A"}}}}}}};
    return volt::ComponentSpec{.name = "LED",
                               .pins = {volt::PinSpec{.name = "A", .number = "2"},
                                        volt::PinSpec{.name = "K", .number = "1"}},
                               .contract = std::move(contract)};
}

[[nodiscard]] volt::ComponentSpec supply_spec(std::vector<volt::PinKey> positives,
                                              std::vector<volt::FeatureRoleBinding> roles) {
    auto contract = volt::ComponentContractSpec{
        .key = volt::ComponentKey{"test/supply-consumer@1"},
        .pin_keys = {volt::PinKey{"VDD1"}, volt::PinKey{"VDD2"}, volt::PinKey{"GND"}},
        .supply_domains = {volt::ContractSupplyDomain{
            volt::SupplyDomainKey{"main"}, std::move(positives), {volt::PinKey{"GND"}}}},
        .feature_schemas = {volt::supply_consumer_feature_schema()},
        .feature_bindings = {volt::FeatureBinding{
            volt::FeatureKey{"main_supply"},
            volt::FeatureSchemaKey{"volt.feature/supply-consumer@1"},
            volt::ComponentSubjectRef::supply_domain(volt::SupplyDomainKey{"main"}),
            std::move(roles)}}};
    return volt::ComponentSpec{.name = "Supply consumer",
                               .pins = {volt::PinSpec{.name = "VDD1", .number = "1"},
                                        volt::PinSpec{.name = "VDD2", .number = "2"},
                                        volt::PinSpec{.name = "GND", .number = "3"}},
                               .contract = std::move(contract)};
}

} // namespace

TEST_CASE("Component contract rejects invalid stable identity before Circuit mutation") {
    auto circuit = volt::Circuit{};
    auto missing = led_spec(custom_diode_schema());
    missing.contract->pin_keys.clear();

    CHECK_THROWS_AS(circuit.define_component(std::move(missing)), volt::KernelArgumentError);
    CHECK(circuit.all<volt::PinDefId>().size() == 0U);
    CHECK(circuit.all<volt::ComponentDefId>().size() == 0U);

    auto duplicate = led_spec(custom_diode_schema());
    duplicate.contract->pin_keys[1] = volt::PinKey{"A"};
    CHECK_THROWS_AS(circuit.define_component(std::move(duplicate)), volt::KernelArgumentError);
    CHECK(circuit.all<volt::PinDefId>().size() == 0U);

    auto foreign = led_spec(custom_diode_schema());
    foreign.contract->relations = {volt::ContractDirectedRelation{
        volt::RelationKey{"junction"}, volt::PinKey{"A"}, volt::PinKey{"FOREIGN"}}};
    CHECK_THROWS_AS(circuit.define_component(std::move(foreign)), volt::KernelLogicError);
    CHECK(circuit.all<volt::PinDefId>().size() == 0U);
}

TEST_CASE("Feature bindings reject missing foreign and cardinality-invalid roles") {
    auto missing_role = led_spec(custom_diode_schema());
    missing_role.contract->feature_bindings = {volt::FeatureBinding{
        volt::FeatureKey{"diode"},
        volt::FeatureSchemaKey{"volt.feature/diode-junction@1"},
        volt::ComponentSubjectRef::directed_relation(volt::RelationKey{"junction"}),
        {volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"}, {volt::PinKey{"A"}}}}}};
    CHECK_THROWS_AS(volt::Circuit{}.define_component(std::move(missing_role)),
                    volt::KernelArgumentError);

    auto foreign_schema = led_spec(custom_diode_schema());
    foreign_schema.contract->feature_bindings = {volt::FeatureBinding{
        volt::FeatureKey{"diode"},
        volt::FeatureSchemaKey{"foreign/schema@1"},
        volt::ComponentSubjectRef::directed_relation(volt::RelationKey{"junction"}),
        {volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"}, {volt::PinKey{"A"}}},
         volt::FeatureRoleBinding{volt::FeatureRoleKey{"negative"}, {volt::PinKey{"K"}}}}}};
    CHECK_THROWS_AS(volt::Circuit{}.define_component(std::move(foreign_schema)),
                    volt::KernelLogicError);

    auto foreign_subject = led_spec(custom_diode_schema());
    foreign_subject.contract->feature_bindings = {volt::FeatureBinding{
        volt::FeatureKey{"diode"},
        volt::FeatureSchemaKey{"volt.feature/diode-junction@1"},
        volt::ComponentSubjectRef::directed_relation(volt::RelationKey{"foreign"}),
        {volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"}, {volt::PinKey{"A"}}},
         volt::FeatureRoleBinding{volt::FeatureRoleKey{"negative"}, {volt::PinKey{"K"}}}}}};
    CHECK_THROWS_AS(volt::Circuit{}.define_component(std::move(foreign_subject)),
                    volt::KernelLogicError);

    auto duplicate_binding = led_spec(custom_diode_schema());
    duplicate_binding.contract->feature_bindings.push_back(
        duplicate_binding.contract->feature_bindings.front());
    CHECK_THROWS_AS(volt::Circuit{}.define_component(std::move(duplicate_binding)),
                    volt::KernelArgumentError);

    auto invalid_cardinality = led_spec(custom_diode_schema());
    invalid_cardinality.contract->feature_bindings = {volt::FeatureBinding{
        volt::FeatureKey{"diode"},
        volt::FeatureSchemaKey{"volt.feature/diode-junction@1"},
        volt::ComponentSubjectRef::directed_relation(volt::RelationKey{"junction"}),
        {volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"},
                                  {volt::PinKey{"A"}, volt::PinKey{"K"}}},
         volt::FeatureRoleBinding{volt::FeatureRoleKey{"negative"}, {volt::PinKey{"K"}}}}}};
    CHECK_THROWS_AS(volt::Circuit{}.define_component(std::move(invalid_cardinality)),
                    volt::KernelArgumentError);
}

TEST_CASE("Standard and custom feature schemas lower to identical component content") {
    auto standard_circuit = volt::Circuit{};
    const auto standard =
        standard_circuit.define_component(led_spec(volt::diode_junction_feature_schema()));
    auto custom_circuit = volt::Circuit{};
    const auto custom = custom_circuit.define_component(led_spec(custom_diode_schema()));

    CHECK(standard_circuit.get(standard).content_identity() ==
          custom_circuit.get(custom).content_identity());
    CHECK(standard_circuit.get(standard).contract().required_records().size() == 3U);
}

TEST_CASE("Unordered contract fields normalize without changing component identity") {
    auto first = volt::Circuit{};
    const auto first_id = first.define_component(supply_spec(
        {volt::PinKey{"VDD2"}, volt::PinKey{"VDD1"}},
        {volt::FeatureRoleBinding{volt::FeatureRoleKey{"return"}, {volt::PinKey{"GND"}}},
         volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"},
                                  {volt::PinKey{"VDD2"}, volt::PinKey{"VDD1"}}}}));

    auto second = volt::Circuit{};
    const auto second_id = second.define_component(supply_spec(
        {volt::PinKey{"VDD1"}, volt::PinKey{"VDD2"}},
        {volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"},
                                  {volt::PinKey{"VDD1"}, volt::PinKey{"VDD2"}}},
         volt::FeatureRoleBinding{volt::FeatureRoleKey{"return"}, {volt::PinKey{"GND"}}}}));

    CHECK(first.get(first_id).content_identity() == second.get(second_id).content_identity());
}

TEST_CASE("Component identity changes when accepted semantic content changes") {
    auto baseline_circuit = volt::Circuit{};
    const auto baseline = baseline_circuit.define_component(led_spec(custom_diode_schema()));

    auto reversed_spec = led_spec(custom_diode_schema());
    reversed_spec.contract->relations = {volt::ContractDirectedRelation{
        volt::RelationKey{"junction"}, volt::PinKey{"K"}, volt::PinKey{"A"}}};
    reversed_spec.contract->feature_bindings = {volt::FeatureBinding{
        volt::FeatureKey{"diode"},
        volt::FeatureSchemaKey{"volt.feature/diode-junction@1"},
        volt::ComponentSubjectRef::directed_relation(volt::RelationKey{"junction"}),
        {volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"}, {volt::PinKey{"K"}}},
         volt::FeatureRoleBinding{volt::FeatureRoleKey{"negative"}, {volt::PinKey{"A"}}}}}};
    auto reversed_circuit = volt::Circuit{};
    const auto reversed = reversed_circuit.define_component(std::move(reversed_spec));

    CHECK(baseline_circuit.get(baseline).content_identity() !=
          reversed_circuit.get(reversed).content_identity());
}

TEST_CASE("Build-local PinDefIds do not enter component content identity") {
    const auto pins = std::vector{volt::PinDefinition{"A", "2"}, volt::PinDefinition{"K", "1"}};
    const auto contract = *led_spec(custom_diode_schema()).contract;
    const auto first = volt::ComponentDefinition::make(
        "LED", pins, {volt::PinDefId{0}, volt::PinDefId{1}}, {}, std::nullopt, {}, contract);
    const auto second = volt::ComponentDefinition::make(
        "LED", pins, {volt::PinDefId{41}, volt::PinDefId{82}}, {}, std::nullopt, {}, contract);

    CHECK(first.content_identity() == second.content_identity());
}

TEST_CASE("Display labels and arbitrary metadata do not enter component content identity") {
    const auto pins = std::vector{volt::PinDefinition{"A", "2"}, volt::PinDefinition{"K", "1"}};
    const auto contract = *led_spec(custom_diode_schema()).contract;
    const auto first = volt::ComponentDefinition::make(
        "LED", pins, {volt::PinDefId{0}, volt::PinDefId{1}}, {}, std::nullopt, {}, contract);
    const auto second = volt::ComponentDefinition::make(
        "Red indicator", pins, {volt::PinDefId{0}, volt::PinDefId{1}},
        {{volt::PropertyKey{"datasheet_note"}, volt::PropertyValue{"display metadata"}}},
        std::nullopt, {}, contract);

    CHECK(first.content_identity() == second.content_identity());
}

TEST_CASE("Standard lowering gives repeated display pin names distinct stable PinKeys") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "Repeated supplies",
        .pins = {volt::PinSpec{.name = "VDD", .number = "1"},
                 volt::PinSpec{.name = "VDD", .number = "2"}},
    });
    const auto &contract = circuit.get(definition).contract();

    REQUIRE(contract.pin_keys().size() == 2U);
    CHECK(contract.pin_keys()[0] == volt::PinKey{"pin/0"});
    CHECK(contract.pin_keys()[1] == volt::PinKey{"pin/1"});

    const auto reopened =
        volt::io::read_logical_circuit_text(volt::io::write_logical_circuit(circuit));
    CHECK(reopened.get(definition).content_identity() ==
          circuit.get(definition).content_identity());
}

TEST_CASE("Feature names cannot substitute for canonical P1 record requirements") {
    auto extension_only =
        volt::FeatureSchema{volt::FeatureSchemaKey{"volt.feature/supply-consumer@1"},
                            volt::ElectricalSubjectKind::SupplyDomain,
                            {volt::FeatureRole{volt::FeatureRoleKey{"positive"},
                                               volt::FeatureRoleCardinality::OneOrMore},
                             volt::FeatureRole{volt::FeatureRoleKey{"return"},
                                               volt::FeatureRoleCardinality::OneOrMore}},
                            {}};
    auto spec = supply_spec(
        {volt::PinKey{"VDD1"}, volt::PinKey{"VDD2"}},
        {volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"},
                                  {volt::PinKey{"VDD1"}, volt::PinKey{"VDD2"}}},
         volt::FeatureRoleBinding{volt::FeatureRoleKey{"return"}, {volt::PinKey{"GND"}}}});
    spec.contract->feature_schemas = {std::move(extension_only)};
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(std::move(spec));

    CHECK(circuit.get(definition).contract().required_records().empty());
}

TEST_CASE("Component contracts persist deterministically and verify content identity") {
    auto standard = volt::Circuit{};
    const auto definition =
        standard.define_component(led_spec(volt::diode_junction_feature_schema()));
    const auto first_write = volt::io::write_logical_circuit(standard);
    const auto reopened = volt::io::read_logical_circuit_text(first_write);

    CHECK(first_write == read_fixture("led.component-contract.volt.json"));
    CHECK(reopened.get(volt::ComponentDefId{0}).content_identity() ==
          standard.get(definition).content_identity());
    CHECK(volt::io::write_logical_circuit(reopened) == first_write);

    auto custom = volt::Circuit{};
    static_cast<void>(custom.define_component(led_spec(custom_diode_schema())));
    CHECK(volt::io::write_logical_circuit(custom) == first_write);

    auto forged = first_write;
    const auto digest = standard.get(definition).content_identity().value();
    const auto position = forged.find(digest);
    REQUIRE(position != std::string::npos);
    forged.replace(position, digest.size(),
                   "sha256:0000000000000000000000000000000000000000000000000000000000000000");
    CHECK_THROWS_AS(volt::io::read_logical_circuit_text(forged), volt::KernelLogicError);
}
