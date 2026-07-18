#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/parts/selected_part.hpp>
#include <volt/circuit/updates.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/pcb/pcb_fabrication_writer.hpp>
#include <volt/library/part_library.hpp>
#include <volt/pcb/assembly/cpl.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace {

constexpr auto asset_bytes = std::string_view{"p5-native-part-asset"};

[[nodiscard]] volt::ContentHash asset_digest() { return volt::sha256_content_hash(asset_bytes); }

[[nodiscard]] volt::Quantity volts(double value) {
    return volt::Quantity{volt::UnitDimension::Voltage, value};
}

[[nodiscard]] volt::Quantity amps(double value) {
    return volt::Quantity{volt::UnitDimension::Current, value};
}

[[nodiscard]] volt::ElectricalSubject supply() {
    return volt::ElectricalSubject::supply_domain({volt::ElectricalPinIndex{0}},
                                                  {volt::ElectricalPinIndex{1}});
}

[[nodiscard]] volt::ElectricalSubject relation() {
    return volt::ElectricalSubject::directed_relation(volt::ElectricalPinIndex{0},
                                                      volt::ElectricalPinIndex{1});
}

[[nodiscard]] volt::ComponentSpec supply_spec(std::string name, std::string key, bool source) {
    const auto schema =
        source ? volt::supply_source_feature_schema() : volt::supply_consumer_feature_schema();
    return volt::ComponentSpec{
        .name = std::move(name),
        .pins = {volt::PinSpec{.name = "P", .number = "1"},
                 volt::PinSpec{.name = "N", .number = "2"}},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{std::move(key)},
                .pin_keys = {volt::PinKey{"P"}, volt::PinKey{"N"}},
                .supply_domains = {volt::ContractSupplyDomain{
                    volt::SupplyDomainKey{"supply"}, {volt::PinKey{"P"}}, {volt::PinKey{"N"}}}},
                .feature_schemas = {schema},
                .feature_bindings = {volt::FeatureBinding{
                    volt::FeatureKey{"supply"},
                    schema.key(),
                    volt::ComponentSubjectRef::supply_domain(volt::SupplyDomainKey{"supply"}),
                    {volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"},
                                              {volt::PinKey{"P"}}},
                     volt::FeatureRoleBinding{volt::FeatureRoleKey{"return"},
                                              {volt::PinKey{"N"}}}}}},
            },
    };
}

[[nodiscard]] volt::ComponentSpec relation_spec(std::string name, std::string key, bool diode) {
    auto contract = volt::ComponentContractSpec{
        .key = volt::ComponentKey{std::move(key)},
        .pin_keys = {volt::PinKey{"P"}, volt::PinKey{"N"}},
        .relations = {volt::ContractDirectedRelation{volt::RelationKey{"junction"},
                                                     volt::PinKey{"P"}, volt::PinKey{"N"}}},
    };
    if (diode) {
        const auto schema = volt::diode_junction_feature_schema();
        contract.feature_schemas = {schema};
        contract.feature_bindings = {volt::FeatureBinding{
            volt::FeatureKey{"junction"},
            schema.key(),
            volt::ComponentSubjectRef::directed_relation(volt::RelationKey{"junction"}),
            {volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"}, {volt::PinKey{"P"}}},
             volt::FeatureRoleBinding{volt::FeatureRoleKey{"negative"}, {volt::PinKey{"N"}}}}}};
    }
    return volt::ComponentSpec{
        .name = std::move(name),
        .pins = {volt::PinSpec{.name = "P", .number = "1"},
                 volt::PinSpec{.name = "N", .number = "2"}},
        .contract = std::move(contract),
    };
}

[[nodiscard]] volt::PartDefinition exact_part(const volt::ComponentDefinition &component,
                                              std::string key, volt::ElectricalRecordSet records) {
    const auto footprint_name = key;
    return volt::PartDefinition{
        component,
        volt::PartIdentity{"test.parts", key, "1.0.0"},
        std::move(records),
        {volt::PinPackageTerminalMapping{volt::PinKey{"P"}, {volt::PackageTerminalKey{"1"}}},
         volt::PinPackageTerminalMapping{volt::PinKey{"N"}, {volt::PackageTerminalKey{"2"}}}},
        {},
        volt::PartProvenance{"P5 fixture datasheet", "volt.tests", "native P5 fixture"},
        {},
        volt::OrderablePart{
            volt::ManufacturerPart{"Test Manufacturer", "MPN-" + key},
            volt::PackageRef{"TEST-2"},
            volt::HashedFootprintReference{
                volt::FootprintRef{"TestFootprints", std::move(footprint_name)}, asset_digest()},
            {volt::PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5},
             volt::PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5}},
            {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                             {volt::FootprintPadKey{"1"}}},
             volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                             {volt::FootprintPadKey{"2"}}}},
        },
    };
}

[[nodiscard]] volt::ElectricalRecordSet source_records(double minimum_voltage,
                                                       double maximum_voltage,
                                                       std::optional<double> capability,
                                                       bool conditional_capability = false) {
    auto records = std::vector<volt::ElectricalRecordSpec>{
        volt::voltage_record(supply(), volt::ElectricalMeaning::ProvidedRange,
                             volt::ElectricalValue{volt::QuantityRange::bounded(
                                 volts(minimum_voltage), volts(maximum_voltage))})};
    auto conditions = std::vector<volt::ElectricalCondition>{};
    if (conditional_capability) {
        conditions.push_back(
            volt::ElectricalCondition::equal(supply(), volt::ElectricalObservable::Voltage,
                                             volt::ElectricalValueExpression::literal(volts(3.3))));
    }
    records.push_back(volt::current_record(
        supply(), volt::ElectricalMeaning::Capability,
        capability.has_value() ? volt::ElectricalValue{volt::ContinuousCurrent{amps(*capability)}}
                               : volt::ElectricalValue{volt::UnknownElectricalValue{}},
        std::move(conditions)));
    return volt::ElectricalRecordSet{2, std::move(records)};
}

[[nodiscard]] volt::ElectricalRecordSet load_records(double current) {
    const auto voltage_condition =
        volt::ElectricalCondition::equal(supply(), volt::ElectricalObservable::Voltage,
                                         volt::ElectricalValueExpression::literal(volts(3.3)));
    return volt::ElectricalRecordSet{
        2,
        {volt::voltage_record(
             supply(), volt::ElectricalMeaning::AcceptedRange,
             volt::ElectricalValue{volt::QuantityRange::bounded(volts(3.0), volts(3.6))}),
         volt::voltage_record(
             supply(), volt::ElectricalMeaning::AbsoluteLimit,
             volt::ElectricalValue{volt::QuantityRange::bounded(volts(-0.3), volts(3.9))}),
         volt::current_record(supply(), volt::ElectricalMeaning::Requirement,
                              volt::ElectricalValue{volt::ContinuousCurrent{amps(current)}}),
         volt::current_record(supply(), volt::ElectricalMeaning::Requirement,
                              volt::ElectricalValue{volt::ContinuousCurrent{amps(current * 0.8)}},
                              {voltage_condition})},
    };
}

[[nodiscard]] volt::ElectricalRecordSet led_records() {
    const auto forward_current =
        volt::ElectricalCondition::equal(relation(), volt::ElectricalObservable::Current,
                                         volt::ElectricalValueExpression::literal(amps(0.02)));
    return volt::ElectricalRecordSet{
        2,
        {volt::voltage_record(relation(), volt::ElectricalMeaning::Characteristic,
                              volt::ElectricalValue{
                                  volt::CharacteristicEnvelope{volts(1.6), volts(2.0), volts(2.4)}},
                              {forward_current}),
         volt::current_record(relation(), volt::ElectricalMeaning::AbsoluteLimit,
                              volt::ElectricalValue{volt::QuantityRange::maximum(amps(0.025))}),
         volt::voltage_record(relation(), volt::ElectricalMeaning::AbsoluteLimit,
                              volt::ElectricalValue{volt::QuantityRange::minimum(volts(-5.0))})},
    };
}

[[nodiscard]] volt::ElectricalRecordSet driver_records() {
    return volt::ElectricalRecordSet{
        2,
        {volt::voltage_record(
             relation(), volt::ElectricalMeaning::ProvidedRange,
             volt::ElectricalValue{volt::QuantityRange::bounded(volts(-6.0), volts(-5.5))}),
         volt::current_record(
             relation(), volt::ElectricalMeaning::ProvidedRange,
             volt::ElectricalValue{volt::QuantityRange::bounded(amps(0.030), amps(0.031))})},
    };
}

class AssetResolver final : public volt::PartAssetResolver {
  public:
    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &) const override {
        return std::string{asset_bytes};
    }
};

struct P5Fixture {
    volt::Circuit circuit;
    volt::ComponentDefId source_definition;
    volt::ComponentDefId load_definition;
    volt::ComponentDefId led_definition;
    volt::ComponentDefId driver_definition;
    volt::PartLibrary library;
};

[[nodiscard]] P5Fixture make_fixture() {
    auto circuit = volt::Circuit{};
    const auto source_definition =
        circuit.define_component(supply_spec("Source", "test.component/source@1", true));
    const auto load_definition =
        circuit.define_component(supply_spec("MCU", "test.component/mcu@1", false));
    const auto led_definition =
        circuit.define_component(relation_spec("LED", "test.component/led@1", true));
    const auto driver_definition =
        circuit.define_component(relation_spec("Driver", "test.component/driver@1", false));

    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.parts", "2026.1", volt::PartLibrarySchemaVersion::V1}};
    for (const auto definition :
         {source_definition, load_definition, led_definition, driver_definition}) {
        builder.add_component(circuit.get(definition));
    }
    builder.add_part(
        exact_part(circuit.get(source_definition), "source-good", source_records(3.2, 3.4, 0.6)));
    builder.add_part(
        exact_part(circuit.get(source_definition), "source-under", source_records(2.8, 2.9, 0.6)));
    builder.add_part(
        exact_part(circuit.get(source_definition), "source-over", source_records(3.7, 3.8, 0.6)));
    builder.add_part(exact_part(circuit.get(source_definition), "source-absolute",
                                source_records(4.0, 4.1, 0.6)));
    builder.add_part(exact_part(circuit.get(source_definition), "source-unknown",
                                source_records(3.2, 3.4, std::nullopt)));
    builder.add_part(exact_part(circuit.get(source_definition), "source-conditional",
                                source_records(3.2, 3.4, 0.6, true)));
    builder.add_part(exact_part(circuit.get(load_definition), "mcu-500ma", load_records(0.5)));
    builder.add_part(exact_part(circuit.get(load_definition), "mcu-100ma", load_records(0.1)));
    builder.add_part(exact_part(circuit.get(led_definition), "led", led_records()));
    builder.add_part(exact_part(circuit.get(driver_definition), "driver", driver_records()));

    const auto resolver = AssetResolver{};
    auto library = builder.build(resolver);
    return P5Fixture{std::move(circuit), source_definition, load_definition,
                     led_definition,     driver_definition, std::move(library)};
}

[[nodiscard]] volt::ComponentId instantiate(volt::Circuit &circuit, volt::ComponentDefId definition,
                                            std::string reference) {
    return circuit.instantiate_component(
        definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{std::move(reference)}});
}

[[nodiscard]] volt::LibraryPartRef select(volt::Circuit &circuit, volt::ComponentId component,
                                          const volt::PartLibrary &library,
                                          const std::string &key) {
    const auto reference = library.require(volt::PartKey{key});
    circuit.update(component, volt::SelectLibraryPart{library, reference});
    return reference;
}

void connect_domain(volt::Circuit &circuit, volt::ComponentId component, volt::NetId positive,
                    volt::NetId negative) {
    const auto pins = volt::queries::pins_for(circuit, component);
    REQUIRE(pins.size() == 2U);
    CHECK(circuit.connect(positive, pins[0]));
    CHECK(circuit.connect(negative, pins[1]));
}

[[nodiscard]] const volt::Diagnostic *diagnostic(const volt::DiagnosticReport &report,
                                                 std::string_view code) {
    const auto match = std::ranges::find(report.diagnostics(), code, [](const auto &candidate) {
        return std::string_view{candidate.code().value()};
    });
    return match == report.diagnostics().end() ? nullptr : &*match;
}

[[nodiscard]] bool has_diagnostic(const volt::DiagnosticReport &report, std::string_view code) {
    return diagnostic(report, code) != nullptr;
}

[[nodiscard]] auto diagnostic_snapshot(const volt::DiagnosticReport &report) {
    auto result = std::vector<std::tuple<std::string, std::string, std::vector<volt::EntityRef>,
                                         std::optional<std::string>>>{};
    for (const auto &item : report.diagnostics()) {
        result.emplace_back(item.code().value(), item.message(), item.entities(), item.rule());
    }
    return result;
}

struct SupplyCircuit {
    volt::ComponentId source;
    std::vector<volt::ComponentId> loads;
    volt::NetId positive;
    volt::NetId negative;
};

[[nodiscard]] SupplyCircuit build_supply_circuit(P5Fixture &fixture, const std::string &source_key,
                                                 const std::vector<std::string> &load_keys) {
    const auto positive = fixture.circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"VDD"}, .kind = volt::NetKind::Power});
    const auto negative = fixture.circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"GND"}, .kind = volt::NetKind::Ground});
    const auto source = instantiate(fixture.circuit, fixture.source_definition, "U1");
    static_cast<void>(select(fixture.circuit, source, fixture.library, source_key));
    connect_domain(fixture.circuit, source, positive, negative);

    auto loads = std::vector<volt::ComponentId>{};
    for (std::size_t index = 0; index < load_keys.size(); ++index) {
        const auto load =
            instantiate(fixture.circuit, fixture.load_definition, "U" + std::to_string(index + 2U));
        static_cast<void>(select(fixture.circuit, load, fixture.library, load_keys[index]));
        connect_domain(fixture.circuit, load, positive, negative);
        loads.push_back(load);
    }
    return SupplyCircuit{source, std::move(loads), positive, negative};
}

} // namespace

TEST_CASE("Circuit selects one exact P4 reference atomically without copying intrinsic truth") {
    auto fixture = make_fixture();
    const auto led = instantiate(fixture.circuit, fixture.led_definition, "D1");

    CHECK_FALSE(volt::queries::selected_library_part_ref(fixture.circuit, led).has_value());
    CHECK_FALSE(volt::queries::selected_physical_part(fixture.circuit, led).has_value());
    CHECK_THROWS_AS(volt::PartKey{"placeholder"}, volt::KernelArgumentError);
    CHECK_THROWS_AS(volt::PartKey{"unspecified"}, volt::KernelArgumentError);
    CHECK_THROWS_AS(volt::PartKey{"synthetic"}, volt::KernelArgumentError);

    const auto selected = select(fixture.circuit, led, fixture.library, "led");
    REQUIRE(volt::queries::selected_library_part_ref(fixture.circuit, led).has_value());
    CHECK(*volt::queries::selected_library_part_ref(fixture.circuit, led) == selected);
    CHECK_FALSE(volt::queries::selected_physical_part(fixture.circuit, led).has_value());
    const auto legacy_power = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"power_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Power,
    };
    CHECK_THROWS_AS(
        fixture.circuit.update(led,
                               volt::SetSelectedPartElectricalAttribute{
                                   legacy_power, volt::ElectricalAttributeValue{volt::Quantity{
                                                     volt::UnitDimension::Power, 0.25}}}),
        volt::KernelLogicError);
    CHECK(*volt::queries::selected_library_part_ref(fixture.circuit, led) == selected);

    const auto forged_digest = volt::sha256_content_hash("forged");
    const auto forged = volt::LibraryPartRef{"test.parts", "2026.1", volt::PartKey{"led"},
                                             forged_digest, selected.part_digest()};
    CHECK_THROWS_AS((volt::SelectLibraryPart{fixture.library, forged}), volt::KernelLogicError);
    CHECK(*volt::queries::selected_library_part_ref(fixture.circuit, led) == selected);

    const auto source = instantiate(fixture.circuit, fixture.source_definition, "U1");
    CHECK_THROWS_AS(
        fixture.circuit.update(source, volt::SelectLibraryPart{fixture.library, selected}),
        volt::KernelLogicError);
    CHECK_FALSE(volt::queries::selected_library_part_ref(fixture.circuit, source).has_value());
}

TEST_CASE("Exact selected-part reference persists and reopens deterministically") {
    auto fixture = make_fixture();
    const auto led = instantiate(fixture.circuit, fixture.led_definition, "D1");
    const auto selected = select(fixture.circuit, led, fixture.library, "led");

    const auto first_write = volt::io::write_logical_circuit(fixture.circuit);
    const auto payload = nlohmann::json::parse(first_write);
    const auto &selection = payload["components"][0]["selected_library_part"];
    CHECK(selection == nlohmann::json{{"library_namespace", "test.parts"},
                                      {"library_version", "2026.1"},
                                      {"part_key", "led"},
                                      {"library_digest", selected.library_digest().value()},
                                      {"part_digest", selected.part_digest().value()}});
    CHECK_FALSE(payload["components"][0].contains("manufacturer"));
    CHECK_FALSE(payload["components"][0].contains("package"));
    CHECK_FALSE(payload["components"][0].contains("ratings"));
    CHECK_FALSE(payload["components"][0].contains("mappings"));
    CHECK_FALSE(payload["components"][0].contains("assets"));

    const auto reopened = volt::io::read_logical_circuit_text(first_write);
    REQUIRE(volt::queries::selected_library_part_ref(reopened, led).has_value());
    CHECK(*volt::queries::selected_library_part_ref(reopened, led) == selected);
    CHECK_FALSE(volt::queries::selected_physical_part(reopened, led).has_value());
    CHECK(volt::io::write_logical_circuit(reopened) == first_write);
}

TEST_CASE("Legacy selected-part Power remains typed and survives logical reopen") {
    auto fixture = make_fixture();
    const auto load = instantiate(fixture.circuit, fixture.load_definition, "U1");
    const auto pins = fixture.circuit.get(fixture.load_definition).pins();
    fixture.circuit.update(
        load, volt::SelectPhysicalPart{volt::PhysicalPart{
                  volt::ManufacturerPart{"Legacy", "POWER-PART"},
                  volt::PackageRef{"TEST-2"},
                  volt::FootprintRef{"TestFootprints", "legacy"},
                  {volt::PinPadMapping{pins[0], "1"}, volt::PinPadMapping{pins[1], "2"}}}});
    const auto power = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"power_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Power,
    };
    fixture.circuit.update(load, volt::SetSelectedPartElectricalAttribute{
                                     power, volt::ElectricalAttributeValue{
                                                volt::Quantity{volt::UnitDimension::Power, 0.25}}});

    const auto reopened =
        volt::io::read_logical_circuit_text(volt::io::write_logical_circuit(fixture.circuit));
    const auto &selected = volt::queries::selected_physical_part(reopened, load);
    REQUIRE(selected.has_value());
    CHECK(selected->electrical_attributes()
              .get(volt::ElectricalAttributeName{"power_rating"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Power, 0.25});
}

TEST_CASE("Native selected-part ERC diagnoses accepted and absolute Voltage violations") {
    auto under = make_fixture();
    const auto under_circuit = build_supply_circuit(under, "source-under", {"mcu-500ma"});
    const auto under_report = volt::validate_selected_part_erc(under.circuit, under.library);
    const auto *under_diagnostic =
        diagnostic(under_report, "SELECTED_PART_VOLTAGE_BELOW_ACCEPTED_RANGE");
    REQUIRE(under_diagnostic != nullptr);
    CHECK(under_diagnostic->entities() ==
          std::vector{volt::EntityRef::component(under_circuit.loads[0]),
                      volt::EntityRef::component(under_circuit.source),
                      volt::EntityRef::net(under_circuit.positive),
                      volt::EntityRef::net(under_circuit.negative)});
    CHECK(under_diagnostic->rule() ==
          std::optional<std::string>{"volt.part_erc.voltage.accepted_range@1"});
    CHECK(under_diagnostic->message().find(
              under.library.require(volt::PartKey{"source-under"}).part_digest().value()) !=
          std::string::npos);

    auto over = make_fixture();
    static_cast<void>(build_supply_circuit(over, "source-over", {"mcu-500ma"}));
    const auto over_report = volt::validate_selected_part_erc(over.circuit, over.library);
    CHECK(has_diagnostic(over_report, "SELECTED_PART_VOLTAGE_ABOVE_ACCEPTED_RANGE"));
    CHECK_FALSE(has_diagnostic(over_report, "SELECTED_PART_VOLTAGE_ABSOLUTE_LIMIT_VIOLATION"));

    auto absolute = make_fixture();
    static_cast<void>(build_supply_circuit(absolute, "source-absolute", {"mcu-500ma"}));
    const auto absolute_report =
        volt::validate_selected_part_erc(absolute.circuit, absolute.library);
    CHECK(has_diagnostic(absolute_report, "SELECTED_PART_VOLTAGE_ABOVE_ACCEPTED_RANGE"));
    const auto *absolute_diagnostic =
        diagnostic(absolute_report, "SELECTED_PART_VOLTAGE_ABSOLUTE_LIMIT_VIOLATION");
    REQUIRE(absolute_diagnostic != nullptr);
    CHECK(absolute_diagnostic->rule() ==
          std::optional<std::string>{"volt.part_erc.voltage.absolute_limit@1"});
}

TEST_CASE("Native selected-part ERC is identical after logical round-trip") {
    auto fixture = make_fixture();
    static_cast<void>(build_supply_circuit(fixture, "source-over", {"mcu-500ma"}));
    const auto before = volt::validate_selected_part_erc(fixture.circuit, fixture.library);

    const auto reopened =
        volt::io::read_logical_circuit_text(volt::io::write_logical_circuit(fixture.circuit));
    const auto after = volt::validate_selected_part_erc(reopened, fixture.library);

    CHECK(diagnostic_snapshot(after) == diagnostic_snapshot(before));
}

TEST_CASE("Continuous Current demand merges once per instance and aggregates by domain") {
    auto passing = make_fixture();
    static_cast<void>(build_supply_circuit(passing, "source-good", {"mcu-500ma", "mcu-100ma"}));
    const auto passing_report = volt::validate_selected_part_erc(passing.circuit, passing.library);
    CHECK_FALSE(has_diagnostic(passing_report, "SELECTED_PART_CURRENT_CAPABILITY_INSUFFICIENT"));
    CHECK_FALSE(has_diagnostic(passing_report, "SELECTED_PART_CURRENT_BUDGET_UNKNOWN"));

    auto failing = make_fixture();
    const auto failing_circuit =
        build_supply_circuit(failing, "source-good", {"mcu-500ma", "mcu-100ma", "mcu-100ma"});
    const auto failing_report = volt::validate_selected_part_erc(failing.circuit, failing.library);
    const auto *insufficient =
        diagnostic(failing_report, "SELECTED_PART_CURRENT_CAPABILITY_INSUFFICIENT");
    REQUIRE(insufficient != nullptr);
    CHECK(insufficient->entities() ==
          std::vector{volt::EntityRef::component(failing_circuit.loads[0]),
                      volt::EntityRef::component(failing_circuit.loads[1]),
                      volt::EntityRef::component(failing_circuit.loads[2]),
                      volt::EntityRef::component(failing_circuit.source),
                      volt::EntityRef::net(failing_circuit.positive),
                      volt::EntityRef::net(failing_circuit.negative)});
    CHECK(insufficient->rule() ==
          std::optional<std::string>{"volt.part_erc.current.continuous_budget@1"});
}

TEST_CASE("Continuous Current ERC reports unknown guarantees and unresolved domains") {
    for (const auto *source : {"source-unknown", "source-conditional"}) {
        auto fixture = make_fixture();
        static_cast<void>(build_supply_circuit(fixture, source, {"mcu-500ma"}));
        const auto report = volt::validate_selected_part_erc(fixture.circuit, fixture.library);
        const auto *unknown = diagnostic(report, "SELECTED_PART_CURRENT_BUDGET_UNKNOWN");
        REQUIRE(unknown != nullptr);
        CHECK(unknown->rule() ==
              std::optional<std::string>{"volt.part_erc.current.continuous_budget@1"});
    }

    auto unresolved = make_fixture();
    const auto positive = unresolved.circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"VDD"}, .kind = volt::NetKind::Power});
    const auto negative = unresolved.circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"GND"}, .kind = volt::NetKind::Ground});
    const auto source = instantiate(unresolved.circuit, unresolved.source_definition, "U1");
    const auto load = instantiate(unresolved.circuit, unresolved.load_definition, "U2");
    static_cast<void>(select(unresolved.circuit, source, unresolved.library, "source-good"));
    static_cast<void>(select(unresolved.circuit, load, unresolved.library, "mcu-500ma"));
    connect_domain(unresolved.circuit, source, positive, negative);
    const auto load_pins = volt::queries::pins_for(unresolved.circuit, load);
    CHECK(unresolved.circuit.connect(positive, load_pins[0]));

    const auto report = volt::validate_selected_part_erc(unresolved.circuit, unresolved.library);
    const auto *subject = diagnostic(report, "SELECTED_PART_ELECTRICAL_SUBJECT_UNRESOLVED");
    REQUIRE(subject != nullptr);
    CHECK(subject->entities() ==
          std::vector{volt::EntityRef::component(load),
                      volt::EntityRef::component_def(unresolved.load_definition)});
    CHECK(has_diagnostic(report, "SELECTED_PART_CURRENT_BUDGET_UNKNOWN"));
}

TEST_CASE("LED directed-relation ERC diagnoses Voltage and Current absolute limits") {
    auto fixture = make_fixture();
    const auto positive = fixture.circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"LED_A"}, .kind = volt::NetKind::Signal});
    const auto negative = fixture.circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"LED_K"}, .kind = volt::NetKind::Signal});
    const auto driver = instantiate(fixture.circuit, fixture.driver_definition, "U1");
    const auto led = instantiate(fixture.circuit, fixture.led_definition, "D1");
    static_cast<void>(select(fixture.circuit, driver, fixture.library, "driver"));
    static_cast<void>(select(fixture.circuit, led, fixture.library, "led"));
    connect_domain(fixture.circuit, driver, positive, negative);
    connect_domain(fixture.circuit, led, positive, negative);

    const auto report = volt::validate_selected_part_erc(fixture.circuit, fixture.library);
    const auto *voltage = diagnostic(report, "SELECTED_PART_VOLTAGE_ABSOLUTE_LIMIT_VIOLATION");
    const auto *current = diagnostic(report, "SELECTED_PART_CURRENT_ABSOLUTE_LIMIT_VIOLATION");
    REQUIRE(voltage != nullptr);
    REQUIRE(current != nullptr);
    CHECK(voltage->entities() ==
          std::vector{volt::EntityRef::component(led), volt::EntityRef::component(driver),
                      volt::EntityRef::net(positive), volt::EntityRef::net(negative)});
    CHECK(current->rule() == std::optional<std::string>{"volt.part_erc.current.absolute_limit@1"});
}

TEST_CASE("Exact selection satisfies missing-selection readiness without becoming copied truth") {
    auto fixture = make_fixture();
    const auto led = instantiate(fixture.circuit, fixture.led_definition, "D1");
    fixture.circuit.update(led, volt::SetAssemblyIntent{.dnp = false});

    const auto pcb_missing = volt::validate_for_pcb(fixture.circuit);
    const auto bom_missing = volt::validate_bom_readiness(fixture.circuit);
    CHECK(has_diagnostic(pcb_missing, "PHYSICAL_PART_REQUIRED"));
    CHECK(has_diagnostic(bom_missing, "BOM_COMPONENT_MISSING_SELECTED_PART"));

    auto board = volt::Board{fixture.circuit};
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        led, volt::BoardPoint{1.0, 2.0}, volt::BoardRotation::degrees(0.0)}));
    const auto board_missing = volt::validate_board(board, volt::builtin_footprint_library());
    CHECK(has_diagnostic(board_missing, "PCB_COMPONENT_MISSING_SELECTED_PART"));
    const auto fabrication_missing =
        volt::io::write_pcb_fabrication_files(board, volt::builtin_footprint_library());
    const auto missing_selection_warning =
        std::ranges::find_if(fabrication_missing.loss_report.warnings(), [](const auto &warning) {
            return warning.construct == "component.part" &&
                   warning.message ==
                       "Component placement has no selected physical part for fabrication export";
        });
    CHECK(missing_selection_warning != fabrication_missing.loss_report.warnings().end());

    static_cast<void>(select(fixture.circuit, led, fixture.library, "led"));
    const auto pcb_selected = volt::validate_for_pcb(fixture.circuit);
    const auto bom_selected = volt::validate_bom_readiness(fixture.circuit);
    CHECK_FALSE(has_diagnostic(pcb_selected, "PHYSICAL_PART_REQUIRED"));
    CHECK_FALSE(has_diagnostic(bom_selected, "BOM_COMPONENT_MISSING_SELECTED_PART"));

    const auto cpl = volt::project_cpl(board, volt::builtin_footprint_library());
    CHECK_FALSE(has_diagnostic(cpl.diagnostics(), "ASSEMBLY_COMPONENT_MISSING_SELECTED_PART"));
    CHECK(has_diagnostic(cpl.diagnostics(), "ASSEMBLY_PART_IDENTITY_MISSING"));

    const auto board_report = volt::validate_board(board, volt::builtin_footprint_library());
    CHECK_FALSE(has_diagnostic(board_report, "PCB_COMPONENT_MISSING_SELECTED_PART"));
    CHECK(has_diagnostic(board_report, "PCB_FOOTPRINT_UNRESOLVED"));

    const auto fabrication =
        volt::io::write_pcb_fabrication_files(board, volt::builtin_footprint_library());
    const auto exact_resolution_warning =
        std::ranges::find_if(fabrication.loss_report.warnings(), [](const auto &warning) {
            return warning.construct == "component.part" &&
                   warning.message ==
                       "Exact selected part requires library resolution for fabrication export";
        });
    CHECK(exact_resolution_warning != fabrication.loss_report.warnings().end());
}
