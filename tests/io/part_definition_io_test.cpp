#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/part_definition.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/io/part_definition_reader.hpp>
#include <volt/io/part_definition_writer.hpp>

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
                "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}}},
        volt::OrderablePart{
            volt::ManufacturerPart{"Diodes Incorporated", "AP1117E15G-13"},
            volt::PackageRef{"SOT-223-3"},
            volt::HashedFootprintReference{
                volt::FootprintRef{"Package_TO_SOT_SMD", "SOT-223-3_TabPin2"},
                volt::ContentHash{
                    "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}},
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
                0.0}},
    };
}

void check_malformed_part_is_rejected(nlohmann::json document) {
    CHECK_THROWS_AS(volt::io::read_part_definition_text(document.dump()), std::logic_error);
}

} // namespace

TEST_CASE("Part definition writer emits the golden kernel artifact and stable content hash") {
    const auto part = build_ap1117_part();
    const auto fixture = read_fixture("ap1117.part.volt.json");

    CHECK(volt::io::write_part_definition(part) == fixture);
    CHECK(volt::io::part_definition_content_hash(part) ==
          volt::ContentHash{
              "sha256:2c42b309ec334f6cc7c9e79a9fd414dddc14b1bf15b089af59bdb44b003feb65"});
}

TEST_CASE("Golden part definition fixture round-trips byte-identically") {
    const auto fixture = read_fixture("ap1117.part.volt.json");
    const auto first_read = volt::io::read_part_definition_text(fixture);
    const auto first_write = volt::io::write_part_definition(first_read);
    const auto second_read = volt::io::read_part_definition_text(first_write);

    CHECK(first_write == fixture);
    CHECK(volt::io::write_part_definition(second_read) == fixture);
    CHECK(fixture.find("\"role\"") == std::string::npos);
}

TEST_CASE("Part definition reader rejects structurally malformed artifacts") {
    const auto fixture = nlohmann::json::parse(read_fixture("ap1117.part.volt.json"));

    auto wrong_format = fixture;
    wrong_format["format"] = "volt.logical_circuit";
    check_malformed_part_is_rejected(wrong_format);

    auto persisted_role = fixture;
    persisted_role["pins"][0]["role"] = "ground";
    check_malformed_part_is_rejected(persisted_role);

    auto duplicate_pin_number = fixture;
    duplicate_pin_number["pins"][1]["number"] = "1";
    check_malformed_part_is_rejected(duplicate_pin_number);

    auto missing_pin_mapping = fixture;
    missing_pin_mapping["orderable_part"]["pin_pad_mappings"].erase(3);
    check_malformed_part_is_rejected(missing_pin_mapping);

    auto foreign_pin_mapping = fixture;
    foreign_pin_mapping["orderable_part"]["pin_pad_mappings"][0]["pin_number"] = "99";
    check_malformed_part_is_rejected(foreign_pin_mapping);

    auto duplicate_pad_mapping = fixture;
    duplicate_pad_mapping["orderable_part"]["pin_pad_mappings"][1]["pad"] = "1";
    check_malformed_part_is_rejected(duplicate_pad_mapping);
}
