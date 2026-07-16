#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/parts/part_definition.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/io/parts/part_definition_reader.hpp>
#include <volt/io/parts/part_definition_writer.hpp>

namespace {

std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

volt::ContentHash hash(char fill) { return volt::ContentHash{"sha256:" + std::string(64U, fill)}; }

volt::ElectricalAttributeMap voltage_range_attributes(double minimum, double maximum) {
    auto attributes = volt::ElectricalAttributeMap{};
    attributes.set(volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"voltage_range"},
                                                 volt::ElectricalAttributeOwner::PinSpec,
                                                 volt::ElectricalAttributeKind::Constraint,
                                                 volt::UnitDimension::Voltage},
                   volt::ElectricalAttributeValue{volt::QuantityRange::bounded(
                       volt::Quantity{volt::UnitDimension::Voltage, minimum},
                       volt::Quantity{volt::UnitDimension::Voltage, maximum})});
    return attributes;
}

std::vector<volt::PinDefinition> regulator_pins() {
    return {
        volt::PinDefinition{"GND", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Ground,
                            volt::ElectricalDirection::Passive},
        volt::PinDefinition{"VO", "2", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output,
                            volt::ElectricalSignalDomain::Unspecified,
                            volt::ElectricalDriveKind::Unspecified, volt::ElectricalPolarity::None,
                            voltage_range_attributes(1.5, 1.5)},
        volt::PinDefinition{"VI", "3", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input,
                            volt::ElectricalSignalDomain::Unspecified,
                            volt::ElectricalDriveKind::Unspecified, volt::ElectricalPolarity::None,
                            voltage_range_attributes(2.5, 18.0)},
    };
}

volt::ComponentDefinition
regulator_component(std::string contract_key = "volt.component/ap1117@1") {
    const auto pins = regulator_pins();
    return volt::ComponentDefinition::make(
        "Three-pin regulator", pins, {volt::PinDefId{0}, volt::PinDefId{1}, volt::PinDefId{2}}, {},
        volt::DefinitionSource{"volt.components", "regulator-3pin", "1.0.0"},
        {volt::SchematicSymbolReference{"volt.power:regulator_3pin"}},
        volt::ComponentContractSpec{
            .key = volt::ComponentKey{std::move(contract_key)},
            .pin_keys = {volt::PinKey{"GND"}, volt::PinKey{"VO"}, volt::PinKey{"VI"}},
        });
}

volt::ComponentDefinition mismatched_regulator_component() {
    auto pins = regulator_pins();
    pins[0] = volt::PinDefinition{"GND", "9", volt::ConnectionRequirement::Required,
                                  volt::ElectricalTerminalKind::Ground,
                                  volt::ElectricalDirection::Passive};
    return volt::ComponentDefinition::make(
        "Three-pin regulator", pins, {volt::PinDefId{0}, volt::PinDefId{1}, volt::PinDefId{2}}, {},
        volt::DefinitionSource{"volt.components", "regulator-3pin", "1.0.0"},
        {volt::SchematicSymbolReference{"volt.power:regulator_3pin"}},
        volt::ComponentContractSpec{
            .key = volt::ComponentKey{"volt.component/ap1117@1"},
            .pin_keys = {volt::PinKey{"GND"}, volt::PinKey{"VO"}, volt::PinKey{"VI"}},
        });
}

volt::ElectricalRecordSet regulator_records() {
    const auto input = volt::ElectricalSubject::framed_pin(volt::ElectricalPinIndex{2},
                                                           volt::ElectricalPinIndex{0});
    const auto output = volt::ElectricalSubject::supply_domain({volt::ElectricalPinIndex{1}},
                                                               {volt::ElectricalPinIndex{0}});
    return volt::ElectricalRecordSet{
        3,
        {volt::voltage_record(input, volt::ElectricalMeaning::AcceptedRange,
                              volt::ElectricalValue{volt::QuantityRange::bounded(
                                  volt::Quantity{volt::UnitDimension::Voltage, 2.5},
                                  volt::Quantity{volt::UnitDimension::Voltage, 18.0})}),
         volt::current_record(output, volt::ElectricalMeaning::Capability,
                              volt::ElectricalValue{volt::ContinuousCurrent{
                                  volt::Quantity{volt::UnitDimension::Current, 1.0}}},
                              {}, {hash('d')})}};
}

volt::PartDefinition converted_v4_part(const volt::ComponentDefinition &component) {
    const auto legacy =
        volt::io::PartDefinitionV4::read_text(read_fixture("v4/ap1117.part.volt.json"));
    return legacy.convert(component, regulator_records());
}

void check_current_part_is_rejected(nlohmann::json document,
                                    const volt::ComponentDefinition &component) {
    CHECK_THROWS_AS(volt::io::read_part_definition_text(document.dump(), component),
                    std::logic_error);
}

bool has_diagnostic(const volt::DiagnosticReport &report, std::string_view code) {
    return std::any_of(report.diagnostics().begin(), report.diagnostics().end(),
                       [&](const auto &diagnostic) {
                           return diagnostic.code() == volt::DiagnosticCode{std::string{code}} &&
                                  diagnostic.severity() == volt::Severity::Warning &&
                                  diagnostic.category() == volt::DiagnosticCategory{"part.lineup"};
                       });
}

} // namespace

TEST_CASE("Part definition v5 writer emits one exact component and two physical mapping seams") {
    const auto component = regulator_component();
    const auto part = converted_v4_part(component);
    const auto bytes = volt::io::write_part_definition(part);
    const auto document = nlohmann::json::parse(bytes);

    CHECK(document["format"] == "volt.part");
    CHECK(document["version"] == 5);
    CHECK(document["implements"] == component.content_identity().value());
    CHECK(document["content_identity"] == part.content_identity().value());
    CHECK(document["electrical_records"]["records"].size() == 2U);
    CHECK(document["pin_terminal_mappings"][1]["pin_key"] == "VI");
    CHECK(document["pin_terminal_mappings"][2]["terminals"] == nlohmann::json::array({"2"}));
    CHECK(document["orderable_part"]["terminal_pad_mappings"][1]["pads"] ==
          nlohmann::json::array({"2", "4"}));
    CHECK(document["orderable_part"].find("pin_pad_mappings") == document["orderable_part"].end());
    CHECK(document["schematic_assets"][0]["hash"] == hash('a').value());
    CHECK(volt::io::part_definition_content_hash(part).value().starts_with("sha256:"));
}

TEST_CASE("Golden v5 part fixture round-trips byte-identically against the supplied component") {
    const auto component = regulator_component();
    const auto fixture = read_fixture("ap1117.part.volt.json");
    const auto first = volt::io::read_part_definition_text(fixture, component);
    const auto first_write = volt::io::write_part_definition(first);
    const auto second = volt::io::read_part_definition_text(first_write, component);

    CHECK(first_write == fixture);
    CHECK(volt::io::write_part_definition(second) == fixture);
}

TEST_CASE("Part definition v5 reader rejects component and content identity mismatches") {
    const auto component = regulator_component();
    const auto bytes = volt::io::write_part_definition(converted_v4_part(component));
    const auto document = nlohmann::json::parse(bytes);

    CHECK_THROWS_AS(volt::io::read_part_definition_text(bytes, regulator_component("other")),
                    std::logic_error);

    auto forged_implements = document;
    forged_implements["implements"] = hash('f').value();
    check_current_part_is_rejected(std::move(forged_implements), component);

    auto forged_content = document;
    forged_content["content_identity"] = hash('f').value();
    check_current_part_is_rejected(std::move(forged_content), component);
}

TEST_CASE("Part definition v5 reader rejects incomplete dangling and duplicate ownership") {
    const auto component = regulator_component();
    const auto document =
        nlohmann::json::parse(volt::io::write_part_definition(converted_v4_part(component)));

    auto missing_pin = document;
    missing_pin["pin_terminal_mappings"].erase(0);
    check_current_part_is_rejected(std::move(missing_pin), component);

    auto duplicate_terminal = document;
    duplicate_terminal["pin_terminal_mappings"][1]["terminals"] = {"1"};
    check_current_part_is_rejected(std::move(duplicate_terminal), component);

    auto foreign_terminal = document;
    foreign_terminal["orderable_part"]["terminal_pad_mappings"][0]["terminal"] = "99";
    check_current_part_is_rejected(std::move(foreign_terminal), component);

    auto foreign_pad = document;
    foreign_pad["orderable_part"]["terminal_pad_mappings"][0]["pads"] = {"99"};
    check_current_part_is_rejected(std::move(foreign_pad), component);

    auto duplicate_pad = document;
    duplicate_pad["orderable_part"]["terminal_pad_mappings"][0]["pads"] = {"2"};
    check_current_part_is_rejected(std::move(duplicate_pad), component);
}

TEST_CASE("Part definition v5 requires explicit non-electrical terminal dispositions") {
    const auto component = regulator_component();
    const auto part = converted_v4_part(component);
    auto document = nlohmann::json::parse(volt::io::write_part_definition(part));
    document["orderable_part"]["footprint"]["pads"].push_back(
        {{"label", "NC"}, {"x_mm", 3.0}, {"y_mm", 0.0}, {"width_mm", 0.6}, {"height_mm", 0.6}});
    document["orderable_part"]["terminal_pad_mappings"].push_back(
        {{"terminal", "NC"}, {"pads", nlohmann::json::array({"NC"})}});
    document["content_identity"] = hash('f').value();

    check_current_part_is_rejected(document, component);

    document["terminal_dispositions"].push_back(
        {{"terminal", "NC"}, {"disposition", "no_connect"}});
    const auto accepted = volt::PartDefinition{
        component,
        part.identity(),
        part.electrical_records(),
        part.pin_terminal_mappings(),
        {volt::DisposedPackageTerminal{volt::PackageTerminalKey{"NC"},
                                       volt::PackageTerminalDisposition::NoConnect}},
        part.provenance(),
        part.schematic_assets(),
        volt::OrderablePart{
            part.orderable_part().manufacturer_part(),
            part.orderable_part().package(),
            part.orderable_part().footprint(),
            {volt::PartFootprintPad{"1", -1.0, 0.0, 0.6, 0.6},
             volt::PartFootprintPad{"2", 0.0, 0.0, 0.6, 0.6},
             volt::PartFootprintPad{"3", 1.0, 0.0, 0.6, 0.6},
             volt::PartFootprintPad{"4", 0.0, 2.0, 1.8, 1.8, volt::PartFootprintPadRole::Thermal},
             volt::PartFootprintPad{"NC", 3.0, 0.0, 0.6, 0.6}},
            {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                             {volt::FootprintPadKey{"1"}}},
             volt::PackageTerminalPadMapping{
                 volt::PackageTerminalKey{"2"},
                 {volt::FootprintPadKey{"2"}, volt::FootprintPadKey{"4"}}},
             volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"3"},
                                             {volt::FootprintPadKey{"3"}}},
             volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"NC"},
                                             {volt::FootprintPadKey{"NC"}}}},
        }};
    CHECK(accepted.terminal_dispositions().size() == 1U);
}

TEST_CASE("Legacy v4 reader is migration-only and converter rejects mismatched or lossy input") {
    const auto component = regulator_component();
    const auto fixture = read_fixture("v4/ap1117.part.volt.json");
    const auto legacy = volt::io::PartDefinitionV4::read_text(fixture);
    const auto part = legacy.convert(component, regulator_records());

    CHECK(part.implemented_component() == component.content_identity());
    CHECK(part.orderable_part().manufacturer_part().manufacturer() == "Diodes Incorporated");
    CHECK(part.orderable_part().package().value() == "SOT-223-3");
    CHECK(part.electrical_records().records().size() == 2U);
    CHECK_THROWS_AS(volt::io::read_part_definition_text(fixture, component), std::logic_error);
    CHECK_THROWS_AS(legacy.convert(mismatched_regulator_component(), regulator_records()),
                    std::logic_error);
    CHECK_THROWS_AS(legacy.convert(component, volt::ElectricalRecordSet{3}), std::logic_error);

    auto missing_mapping = nlohmann::json::parse(fixture);
    missing_mapping["orderable_part"]["pin_pad_mappings"].erase(0);
    const auto incomplete = volt::io::PartDefinitionV4::read_text(missing_mapping.dump());
    CHECK_THROWS_AS(incomplete.convert(component, regulator_records()), std::logic_error);
}

TEST_CASE("Loaded exact parts retain geometry lineup diagnostics") {
    const auto component = regulator_component();
    auto fixture =
        nlohmann::json::parse(volt::io::write_part_definition(converted_v4_part(component)));
    fixture["orderable_part"]["footprint"]["pads"][0]["x_mm"] = 0.0;
    fixture["orderable_part"]["footprint"]["pads"][1]["x_mm"] = 0.5;
    const auto changed = fixture;

    auto rebuilt = converted_v4_part(component);
    auto pads = rebuilt.orderable_part().footprint_pads();
    pads[0] = volt::PartFootprintPad{"1", 0.0, 0.0, 0.6, 0.6};
    pads[1] = volt::PartFootprintPad{"2", 0.5, 0.0, 0.6, 0.6};
    const auto overlap = volt::PartDefinition{
        component,
        rebuilt.identity(),
        rebuilt.electrical_records(),
        rebuilt.pin_terminal_mappings(),
        rebuilt.terminal_dispositions(),
        rebuilt.provenance(),
        rebuilt.schematic_assets(),
        volt::OrderablePart{rebuilt.orderable_part().manufacturer_part(),
                            rebuilt.orderable_part().package(),
                            rebuilt.orderable_part().footprint(), std::move(pads),
                            rebuilt.orderable_part().terminal_pad_mappings()}};
    const auto report = volt::validate_part_lineup(overlap);
    CHECK(has_diagnostic(report, "PART_PAD_OVERLAP"));
    CHECK(changed["orderable_part"]["footprint"]["pads"][1]["x_mm"] == 0.5);
}
