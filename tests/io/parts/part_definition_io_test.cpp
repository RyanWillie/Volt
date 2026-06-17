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

volt::ElectricalAttributeMap part_ratings() {
    auto attributes = volt::ElectricalAttributeMap{};
    attributes.set(volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"input_voltage"},
                                                 volt::ElectricalAttributeOwner::PartDefinition,
                                                 volt::ElectricalAttributeKind::DesignInput,
                                                 volt::UnitDimension::Voltage},
                   volt::ElectricalAttributeValue{volt::QuantityRange::bounded(
                       volt::Quantity{volt::UnitDimension::Voltage, 2.5},
                       volt::Quantity{volt::UnitDimension::Voltage, 18.0})});
    attributes.set(
        volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"output_current"},
                                      volt::ElectricalAttributeOwner::PartDefinition,
                                      volt::ElectricalAttributeKind::DesignInput,
                                      volt::UnitDimension::Current},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Current, 1.0}});
    return attributes;
}

volt::PartDefinition build_ap1117_part() {
    return volt::PartDefinition{
        volt::PartIdentity{"volt.power", "AP1117-15", "1.0.0"},
        std::vector{
            volt::PartPin{volt::PinDefinition{"GND", "1", volt::ConnectionRequirement::Required,
                                              volt::ElectricalTerminalKind::Ground,
                                              volt::ElectricalDirection::Passive}},
            volt::PartPin{volt::PinDefinition{"VO", "2", volt::ConnectionRequirement::Required,
                                              volt::ElectricalTerminalKind::Power,
                                              volt::ElectricalDirection::Output},
                          voltage_range_attributes(1.5, 1.5)},
            volt::PartPin{volt::PinDefinition{"VI", "3", volt::ConnectionRequirement::Required,
                                              volt::ElectricalTerminalKind::Power,
                                              volt::ElectricalDirection::Input},
                          voltage_range_attributes(2.5, 18.0)},
        },
        part_ratings(),
        volt::PartProvenance{"AP1117 rev 24", "volt.tests", "manual"},
        std::vector{volt::HashedSchematicSymbolReference{
            "volt.power:regulator_3pin", "default",
            volt::ContentHash{
                "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
            std::vector{
                volt::PartSymbolPin{"GND", "1"},
                volt::PartSymbolPin{"VO", "2"},
                volt::PartSymbolPin{"VI", "3"},
            }}},
        volt::OrderablePart{
            volt::ManufacturerPart{"Diodes Incorporated", "AP1117E15G-13"},
            volt::PackageRef{"SOT-223-3"},
            volt::HashedFootprintReference{
                volt::FootprintRef{"Package_TO_SOT_SMD", "SOT-223-3_TabPin2"},
                volt::ContentHash{
                    "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}},
            std::vector{
                volt::PartFootprintPad{"1", -1.0, 0.0, 0.6, 0.6},
                volt::PartFootprintPad{"2", 0.0, 0.0, 0.6, 0.6},
                volt::PartFootprintPad{"3", 1.0, 0.0, 0.6, 0.6},
                volt::PartFootprintPad{"4", 0.0, 2.0, 1.8, 1.8,
                                       volt::PartFootprintPadRole::Thermal},
            },
            std::vector{
                volt::OrderablePinPadMapping{"1", "1"},
                volt::OrderablePinPadMapping{"2", "2"},
                volt::OrderablePinPadMapping{"2", "4"},
                volt::OrderablePinPadMapping{"3", "3"},
            },
            std::vector<std::string>{"AP1117E15G-7"},
            volt::PartModel3DReference{
                "step",
                "sot223.step",
                volt::ContentHash{
                    "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"},
                {0.0, 0.0, 0.8},
                0.0},
            volt::PartFootprintPolygon{std::vector{
                volt::PartFootprintPoint{-2.4, -1.2},
                volt::PartFootprintPoint{2.4, -1.2},
                volt::PartFootprintPoint{2.4, 3.2},
                volt::PartFootprintPoint{-2.4, 3.2},
            }},
            volt::PartFootprintPolygon{std::vector{
                volt::PartFootprintPoint{-1.9, -0.8},
                volt::PartFootprintPoint{1.9, -0.8},
                volt::PartFootprintPoint{1.9, 2.8},
                volt::PartFootprintPoint{-1.9, 2.8},
            }}},
    };
}

void check_malformed_part_is_rejected(nlohmann::json document) {
    CHECK_THROWS_AS(volt::io::read_part_definition_text(document.dump()), std::logic_error);
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

TEST_CASE("Part definition writer emits the golden kernel artifact and stable content hash") {
    const auto part = build_ap1117_part();
    const auto fixture = read_fixture("ap1117.part.volt.json");

    CHECK(volt::io::write_part_definition(part) == fixture);
    CHECK(volt::io::part_definition_content_hash(part) ==
          volt::ContentHash{
              "sha256:e81ab37d851b54ecceabd09fa314b712f8db14aee9fc509ee01367cf560ac33c"});
}

TEST_CASE("Golden part definition fixture round-trips byte-identically") {
    const auto fixture = read_fixture("ap1117.part.volt.json");
    const auto first_read = volt::io::read_part_definition_text(fixture);
    const auto first_write = volt::io::write_part_definition(first_read);
    const auto second_read = volt::io::read_part_definition_text(first_write);
    const auto document = nlohmann::json::parse(fixture);

    CHECK(first_write == fixture);
    CHECK(volt::io::write_part_definition(second_read) == fixture);
    CHECK(document["pins"][0].find("role") == document["pins"][0].end());
    CHECK(document["version"] == 3);
    CHECK(document["orderable_part"]["footprint"]["courtyard"][2]["x_mm"] == 2.4);
    CHECK(document["orderable_part"]["footprint"]["body"][0]["y_mm"] == -0.8);
}

TEST_CASE("Part definition reader rejects structurally malformed artifacts") {
    const auto fixture = nlohmann::json::parse(read_fixture("ap1117.part.volt.json"));

    auto wrong_format = fixture;
    wrong_format["format"] = "volt.logical_circuit";
    check_malformed_part_is_rejected(wrong_format);

    auto legacy_v1 = fixture;
    legacy_v1["version"] = 2;
    check_malformed_part_is_rejected(legacy_v1);

    auto empty_courtyard = fixture;
    empty_courtyard["orderable_part"]["footprint"]["courtyard"] = nlohmann::json::array();
    check_malformed_part_is_rejected(empty_courtyard);

    auto closed_courtyard = fixture;
    closed_courtyard["orderable_part"]["footprint"]["courtyard"].push_back(
        closed_courtyard["orderable_part"]["footprint"]["courtyard"][0]);
    check_malformed_part_is_rejected(closed_courtyard);

    auto persisted_role = fixture;
    persisted_role["pins"][0]["role"] = "ground";
    check_malformed_part_is_rejected(persisted_role);

    auto duplicate_pin_number = fixture;
    duplicate_pin_number["pins"][1]["number"] = "1";
    check_malformed_part_is_rejected(duplicate_pin_number);

    auto foreign_pin_mapping = fixture;
    foreign_pin_mapping["orderable_part"]["pin_pad_mappings"][0]["pin_number"] = "99";
    check_malformed_part_is_rejected(foreign_pin_mapping);

    auto duplicate_pad_mapping = fixture;
    duplicate_pad_mapping["orderable_part"]["pin_pad_mappings"][1]["pad"] = "1";
    check_malformed_part_is_rejected(duplicate_pad_mapping);
}

TEST_CASE("Part definition reader rejects symbol lineup contract violations") {
    const auto fixture = nlohmann::json::parse(read_fixture("ap1117.part.volt.json"));

    auto empty_symbols = fixture;
    empty_symbols["symbols"] = nlohmann::json::array();
    check_malformed_part_is_rejected(empty_symbols);

    auto foreign_symbol_pin = fixture;
    foreign_symbol_pin["symbols"][0]["pins"][2] = {{"name", "ENABLE"}, {"number", "9"}};
    check_malformed_part_is_rejected(foreign_symbol_pin);

    auto missing_symbol_pin = fixture;
    missing_symbol_pin["symbols"][0]["pins"].erase(2);
    check_malformed_part_is_rejected(missing_symbol_pin);

    auto duplicate_symbol_pin = fixture;
    duplicate_symbol_pin["symbols"][0]["pins"][2] = duplicate_symbol_pin["symbols"][0]["pins"][1];
    check_malformed_part_is_rejected(duplicate_symbol_pin);

    auto repeated_name_duplicate_number = fixture;
    repeated_name_duplicate_number["pins"][2]["name"] = "VO";
    repeated_name_duplicate_number["symbols"][0]["pins"][2] = {{"name", "VO"}, {"number", "2"}};
    check_malformed_part_is_rejected(repeated_name_duplicate_number);
}

TEST_CASE("Part definition reader rejects footprint lineup contract violations") {
    const auto fixture = nlohmann::json::parse(read_fixture("ap1117.part.volt.json"));

    auto foreign_pin_mapping = fixture;
    foreign_pin_mapping["orderable_part"]["pin_pad_mappings"][0]["pin_number"] = "99";
    check_malformed_part_is_rejected(foreign_pin_mapping);

    auto foreign_pad_mapping = fixture;
    foreign_pad_mapping["orderable_part"]["pin_pad_mappings"][0]["pad"] = "99";
    check_malformed_part_is_rejected(foreign_pad_mapping);

    auto collapsed_multi_pad_mapping = fixture;
    collapsed_multi_pad_mapping["orderable_part"]["pin_pad_mappings"][1]["pad"] = "2,4";
    collapsed_multi_pad_mapping["orderable_part"]["pin_pad_mappings"].erase(2);
    collapsed_multi_pad_mapping["orderable_part"]["footprint"]["pads"][1]["label"] = "2,4";
    check_malformed_part_is_rejected(collapsed_multi_pad_mapping);
}

TEST_CASE("Loaded part definitions produce lineup diagnostics without rejecting the artifact") {
    auto fixture = nlohmann::json::parse(read_fixture("ap1117.part.volt.json"));
    fixture["orderable_part"]["pin_pad_mappings"].erase(3);

    const auto part = volt::io::read_part_definition_text(fixture.dump());
    const auto report = volt::validate_part_lineup(part);

    REQUIRE(report.count() == 2U);
    CHECK(has_diagnostic(report, "PART_PIN_WITHOUT_PAD"));
    CHECK(has_diagnostic(report, "PART_PAD_WITHOUT_PIN"));
    CHECK(report.diagnostics()[0].message().find("volt.power:AP1117-15@1.0.0") !=
          std::string::npos);
}

TEST_CASE("Loaded part definitions report footprint geometry lineup diagnostics") {
    auto overlap_fixture = nlohmann::json::parse(read_fixture("ap1117.part.volt.json"));
    overlap_fixture["orderable_part"]["footprint"]["pads"][0]["x_mm"] = 0.0;
    overlap_fixture["orderable_part"]["footprint"]["pads"][1]["x_mm"] = 0.5;
    overlap_fixture["orderable_part"]["footprint"]["pads"][2]["x_mm"] = 1.0;

    const auto overlap_part = volt::io::read_part_definition_text(overlap_fixture.dump());
    const auto overlap_report = volt::validate_part_lineup(overlap_part);

    CHECK(has_diagnostic(overlap_report, "PART_PAD_OVERLAP"));

    auto pitch_fixture = nlohmann::json::parse(read_fixture("ap1117.part.volt.json"));
    pitch_fixture["orderable_part"]["footprint"]["pads"][0]["x_mm"] = 0.0;
    pitch_fixture["orderable_part"]["footprint"]["pads"][1]["x_mm"] = 1.0;
    pitch_fixture["orderable_part"]["footprint"]["pads"][2]["x_mm"] = 2.5;

    const auto pitch_part = volt::io::read_part_definition_text(pitch_fixture.dump());
    const auto pitch_report = volt::validate_part_lineup(pitch_part);

    CHECK(has_diagnostic(pitch_report, "PART_PAD_ROW_PITCH_INCONSISTENT"));
}
