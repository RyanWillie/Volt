#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/parts/part_definition.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/errors.hpp>
#include <volt/electrical/passive_model.hpp>
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

volt::PartDefinition current_part(const volt::ComponentDefinition &component) {
    return volt::io::read_part_definition_text(read_fixture("ap1117.part.volt.json"), component);
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

TEST_CASE("Part definition v6 writer emits one exact component and two physical mapping seams") {
    const auto component = regulator_component();
    const auto part = current_part(component);
    const auto bytes = volt::io::write_part_definition(part);
    const auto document = nlohmann::json::parse(bytes);

    CHECK(document["format"] == "volt.part");
    CHECK(document["version"] == 6);
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
    CHECK_FALSE(part.electrical_model().has_value());
}

TEST_CASE("Part definition v6 preserves a three-terminal model alongside voltage records") {
    const auto component = regulator_component();
    const auto original = current_part(component);
    auto model_builder = volt::PartElectricalModelBuilder{component};
    const auto ground =
        model_builder.terminal(volt::ModelTerminalKey{"ground"}, volt::PinKey{"GND"});
    const auto output =
        model_builder.terminal(volt::ModelTerminalKey{"output"}, volt::PinKey{"VO"});
    const auto input = model_builder.terminal(volt::ModelTerminalKey{"input"}, volt::PinKey{"VI"});
    model_builder.add<volt::ResistanceElement>(
        volt::ModelElementKey{"output_load"}, output, ground,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, 1000.0}});
    model_builder.add<volt::ResistanceElement>(
        volt::ModelElementKey{"input_load"}, input, ground,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, 2000.0}});
    const auto part = volt::PartDefinition{component,
                                           original.identity(),
                                           original.electrical_records(),
                                           original.pin_terminal_mappings(),
                                           original.terminal_dispositions(),
                                           original.provenance(),
                                           original.schematic_assets(),
                                           original.orderable_part(),
                                           model_builder.build()};
    REQUIRE(part.electrical_model().has_value());

    const auto bytes = volt::io::write_part_definition(part);
    auto stream = std::istringstream{bytes};
    const auto reopened = volt::io::read_part_definition(stream, component);
    REQUIRE(reopened.electrical_model().has_value());
    CHECK(reopened.electrical_model()->terminals().size() == 3U);
    CHECK(reopened.electrical_model()->elements().size() == 2U);
    CHECK(reopened.content_identity() == part.content_identity());
    CHECK(volt::io::write_part_definition(reopened) == bytes);
    CHECK(nlohmann::json::parse(bytes)["electrical_records"] ==
          nlohmann::json::parse(volt::io::write_part_definition(original))["electrical_records"]);
}

TEST_CASE("Golden v6 part fixture round-trips byte-identically against the supplied component") {
    const auto component = regulator_component();
    const auto fixture = read_fixture("ap1117.part.volt.json");
    const auto first = volt::io::read_part_definition_text(fixture, component);
    const auto first_write = volt::io::write_part_definition(first);
    const auto second = volt::io::read_part_definition_text(first_write, component);

    CHECK(first_write == fixture);
    CHECK(volt::io::write_part_definition(second) == fixture);
    CHECK_FALSE(first.electrical_model().has_value());
    CHECK_FALSE(second.electrical_model().has_value());
    CHECK(nlohmann::json::parse(first_write)["electrical_model"].is_null());
}

TEST_CASE("Part definition v6 reader rejects component and content identity mismatches") {
    const auto component = regulator_component();
    const auto bytes = volt::io::write_part_definition(current_part(component));
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

TEST_CASE("Part definition v6 reader requires current provenance and schematic asset fields") {
    const auto component = regulator_component();
    const auto document =
        nlohmann::json::parse(volt::io::write_part_definition(current_part(component)));

    auto missing_provenance = document;
    missing_provenance.erase("provenance");
    check_current_part_is_rejected(std::move(missing_provenance), component);

    auto incomplete_provenance = document;
    incomplete_provenance["provenance"].erase("authored_by");
    check_current_part_is_rejected(std::move(incomplete_provenance), component);

    auto missing_asset_variant = document;
    missing_asset_variant["schematic_assets"][0].erase("variant");
    check_current_part_is_rejected(std::move(missing_asset_variant), component);

    auto unknown_field = document;
    unknown_field["orderable_part"]["footprint"]["viewer_cache"] = nlohmann::json::object();
    check_current_part_is_rejected(std::move(unknown_field), component);
}

TEST_CASE("Part definition v6 reader rejects duplicate object keys before schema validation") {
    const auto component = regulator_component();
    auto document = volt::io::write_part_definition(current_part(component));
    const auto current_version = document.find("\"version\": 6");
    REQUIRE(current_version != std::string::npos);
    document.insert(current_version, "\"version\": 4,\n  ");

    CHECK_THROWS_AS(volt::io::read_part_definition_text(document, component),
                    volt::KernelLogicError);
}

TEST_CASE("Part definition v6 reader rejects incomplete dangling and duplicate ownership") {
    const auto component = regulator_component();
    const auto document =
        nlohmann::json::parse(volt::io::write_part_definition(current_part(component)));

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

TEST_CASE("Part definition v6 requires explicit non-electrical terminal dispositions") {
    const auto component = regulator_component();
    const auto part = current_part(component);
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

TEST_CASE("Loaded exact parts retain geometry lineup diagnostics") {
    const auto component = regulator_component();
    auto fixture = nlohmann::json::parse(volt::io::write_part_definition(current_part(component)));
    fixture["orderable_part"]["footprint"]["pads"][0]["x_mm"] = 0.0;
    fixture["orderable_part"]["footprint"]["pads"][1]["x_mm"] = 0.5;
    const auto changed = fixture;

    auto rebuilt = current_part(component);
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
