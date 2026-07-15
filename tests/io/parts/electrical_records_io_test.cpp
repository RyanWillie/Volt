#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include <volt/io/parts/electrical_records_io.hpp>

namespace {

[[nodiscard]] std::string read_fixture(const std::string &name) {
    const auto path = std::filesystem::path{VOLT_TEST_FIXTURE_DIR} / name;
    auto input = std::ifstream{path};
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return buffer.str();
}

[[nodiscard]] volt::Quantity volts(double value) {
    return volt::Quantity{volt::UnitDimension::Voltage, value};
}

[[nodiscard]] volt::Quantity amps(double value) {
    return volt::Quantity{volt::UnitDimension::Current, value};
}

[[nodiscard]] volt::ElectricalRecordSet regulator_fixture() {
    const auto input = volt::ElectricalSubject::framed_pin(volt::ElectricalPinIndex{0},
                                                           volt::ElectricalPinIndex{1});
    const auto output = volt::ElectricalSubject::supply_domain({volt::ElectricalPinIndex{2}},
                                                               {volt::ElectricalPinIndex{1}});
    const auto condition =
        volt::ElectricalCondition::range(input, volt::ElectricalObservable::Voltage,
                                         volt::ElectricalValueExpression::literal(volts(4.5)),
                                         volt::ElectricalValueExpression::literal(volts(12.0)));
    const auto evidence = volt::sha256_content_hash("regulator-datasheet-output-table");
    return volt::ElectricalRecordSet{
        3,
        {
            volt::voltage_record(
                output, volt::ElectricalMeaning::ProvidedRange,
                volt::ElectricalValue{volt::QuantityRange::bounded(volts(3.2), volts(3.4))},
                {condition}, {evidence}),
            volt::current_record(output, volt::ElectricalMeaning::Capability,
                                 volt::ElectricalValue{volt::ContinuousCurrent{amps(0.6)}}, {},
                                 {evidence}),
        },
    };
}

[[nodiscard]] volt::ElectricalRecordSet mcu_fixture() {
    const auto supply = volt::ElectricalSubject::supply_domain({volt::ElectricalPinIndex{0}},
                                                               {volt::ElectricalPinIndex{1}});
    const auto evidence = volt::sha256_content_hash("mcu-datasheet-operating-conditions");
    return volt::ElectricalRecordSet{
        2,
        {
            volt::voltage_record(
                supply, volt::ElectricalMeaning::AcceptedRange,
                volt::ElectricalValue{volt::QuantityRange::bounded(volts(3.0), volts(3.6))}, {},
                {evidence}),
            volt::voltage_record(
                supply, volt::ElectricalMeaning::AbsoluteLimit,
                volt::ElectricalValue{volt::QuantityRange::bounded(volts(-0.3), volts(3.6))}, {},
                {evidence}),
            volt::current_record(supply, volt::ElectricalMeaning::Requirement,
                                 volt::ElectricalValue{volt::ContinuousCurrent{amps(0.5)}}, {},
                                 {evidence}),
        },
    };
}

[[nodiscard]] volt::ElectricalRecordSet led_fixture() {
    const auto junction = volt::ElectricalSubject::directed_relation(volt::ElectricalPinIndex{0},
                                                                     volt::ElectricalPinIndex{1});
    const auto forward_current =
        volt::ElectricalCondition::equal(junction, volt::ElectricalObservable::Current,
                                         volt::ElectricalValueExpression::literal(amps(0.02)));
    const auto evidence = volt::sha256_content_hash("led-datasheet-electrical-table");
    return volt::ElectricalRecordSet{
        2,
        {
            volt::voltage_record(junction, volt::ElectricalMeaning::Characteristic,
                                 volt::ElectricalValue{volt::CharacteristicEnvelope{
                                     volts(1.6), volts(2.0), volts(2.4)}},
                                 {forward_current}, {evidence}),
            volt::current_record(junction, volt::ElectricalMeaning::AbsoluteLimit,
                                 volt::ElectricalValue{volt::QuantityRange::maximum(amps(0.025))},
                                 {}, {evidence}),
            volt::voltage_record(junction, volt::ElectricalMeaning::AbsoluteLimit,
                                 volt::ElectricalValue{volt::QuantityRange::minimum(volts(-5.0))},
                                 {}, {evidence}),
        },
    };
}

} // namespace

TEST_CASE("Regulator MCU and LED electrical fixtures round-trip deterministically") {
    const auto fixtures = std::array{
        std::pair{"regulator.electrical.volt.json", regulator_fixture()},
        std::pair{"mcu.electrical.volt.json", mcu_fixture()},
        std::pair{"led.electrical.volt.json", led_fixture()},
    };

    for (const auto &[fixture_name, expected] : fixtures) {
        const auto fixture = read_fixture(fixture_name);
        const auto records = volt::io::read_electrical_records_text(fixture);
        const auto first_write = volt::io::write_electrical_records(records);
        const auto second_read = volt::io::read_electrical_records_text(first_write);

        CHECK(first_write == fixture);
        CHECK(first_write == volt::io::write_electrical_records(expected));
        CHECK(volt::io::write_electrical_records(second_read) == fixture);
        CHECK(volt::io::electrical_records_content_hash(records) ==
              volt::sha256_content_hash(fixture));
    }
}

TEST_CASE("Electrical record reader rejects malformed units keys and references atomically") {
    const auto fixture = nlohmann::json::parse(read_fixture("led.electrical.volt.json"));

    auto wrong_version = fixture;
    wrong_version["version"] = 2;
    CHECK_THROWS_AS(volt::io::read_electrical_records_text(wrong_version.dump()),
                    std::invalid_argument);

    auto wrong_dimension = fixture;
    wrong_dimension["records"][0]["value"]["dimension"] = "current";
    CHECK_THROWS_AS(volt::io::read_electrical_records_text(wrong_dimension.dump()),
                    std::invalid_argument);

    auto forged_key = fixture;
    forged_key["records"][0]["semantic_key"] =
        "sha256:0000000000000000000000000000000000000000000000000000000000000000";
    CHECK_THROWS_AS(volt::io::read_electrical_records_text(forged_key.dump()),
                    std::invalid_argument);

    auto dangling_reference = fixture;
    dangling_reference["records"][0]["conditions"][0]["predicate"]["value"] = {
        {"kind", "scaled_reference"},
        {"scale", 1.0},
        {"selector",
         {{"subject", {{"kind", "directed_relation"}, {"from", 0}, {"to", 1}}},
          {"observable", "current"},
          {"meaning", "provided_range"}}},
    };
    CHECK_THROWS_AS(volt::io::read_electrical_records_text(dangling_reference.dump()),
                    std::invalid_argument);
}
