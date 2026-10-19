#include "assembly_ratings_fixture.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

int main(int argc, char **argv) {
    const auto mode = argc > 1 ? std::string{argv[1]} : "a";
    const auto strict = mode == "b";
    auto options = volt::test::manufacturing_options(strict);
    if (mode == "missing")
        options.placements = 0;
    if (mode == "short") {
        options.different_nets = true;
        options.netless_zone = true;
    }
    const auto exact = mode == "assembly" || mode.starts_with("rating-");
    const auto compiled = [&] {
        if (!exact)
            return volt::test::completion_fixture(options);
        auto inputs = volt::test::RatingFixtureOptions{};
        if (mode == "assembly")
            inputs.occurrences = {
                volt::test::RatingOccurrence{},
                volt::test::RatingOccurrence{.side = volt::BoardSide::Bottom, .rotation = -450},
                volt::test::RatingOccurrence{.dnp = true, .placed = false}};
        if (mode == "rating-conditional") {
            auto conditional = volt::test::voltage_rating();
            conditional.conditions = {volt::ElectricalCondition::equal(
                volt::test::rating_subject(), volt::ElectricalObservable::Voltage,
                volt::ElectricalValueExpression::scaled_reference(
                    volt::ElectricalRecordSelector{volt::test::rating_subject(),
                                                   volt::ElectricalObservable::Voltage,
                                                   volt::ElectricalMeaning::Characteristic},
                    1))};
            inputs.occurrences[0].records = {
                volt::voltage_record(volt::test::rating_subject(),
                                     volt::ElectricalMeaning::Characteristic,
                                     volt::ElectricalValue{volt::test::rating_volts(3.3)}),
                conditional};
        }
        if (mode == "rating-failure")
            inputs.voltage = 9;
        if (mode == "rating-unknown")
            inputs.occurrences[0].records = {volt::voltage_record(
                volt::test::rating_subject(), volt::ElectricalMeaning::AbsoluteLimit,
                volt::ElectricalValue{volt::UnknownElectricalValue{}})};
        if (mode == "rating-accepted" || mode == "rating-current" || mode == "rating-budget") {
            auto provider = std::vector<volt::ElectricalRecordSpec>{};
            auto consumer = std::vector<volt::ElectricalRecordSpec>{};
            if (mode == "rating-budget") {
                provider.push_back(volt::current_record(
                    volt::test::rating_subject(), volt::ElectricalMeaning::Capability,
                    volt::ElectricalValue{volt::ContinuousCurrent{volt::test::rating_amps(1)}}));
                consumer.push_back(volt::current_record(
                    volt::test::rating_subject(), volt::ElectricalMeaning::Requirement,
                    volt::ElectricalValue{volt::ContinuousCurrent{volt::test::rating_amps(0.5)}}));
            } else {
                const auto voltage = mode == "rating-accepted";
                const auto observable = voltage ? volt::ElectricalObservable::Voltage
                                                : volt::ElectricalObservable::Current;
                const auto dimension =
                    voltage ? volt::UnitDimension::Voltage : volt::UnitDimension::Current;
                provider.push_back(
                    {volt::test::rating_subject(), observable,
                     volt::ElectricalMeaning::ProvidedRange,
                     volt::ElectricalValue{volt::QuantityRange::bounded(
                         volt::Quantity{dimension, 0}, volt::Quantity{dimension, 1})}});
                consumer.push_back(
                    {volt::test::rating_subject(), observable,
                     voltage ? volt::ElectricalMeaning::AcceptedRange
                             : volt::ElectricalMeaning::AbsoluteLimit,
                     volt::ElectricalValue{volt::QuantityRange::bounded(
                         volt::Quantity{dimension, 0}, volt::Quantity{dimension, 2})}});
            }
            inputs.occurrences = {
                volt::test::RatingOccurrence{.records = std::move(provider)},
                volt::test::RatingOccurrence{.records = std::move(consumer), .selected_index = 1}};
        }
        return volt::test::assembly_rating_fixture(inputs);
    }();
    const auto target = exact ? volt::test::assembly_rating_target(
                                    compiled, true,
                                    volt::CplProjectionOptions{
                                        {volt::CplRotationOffset{{"test.rating", "subject"}, 90}}})
                              : volt::test::manufacturing_target(compiled, strict);
    const auto result = volt::evaluate_manufacturing_coverage(compiled, target);
    auto hex = std::string{};
    constexpr auto digits = std::string_view{"0123456789abcdef"};
    for (const char value : compiled.bytes()) {
        const auto byte = static_cast<unsigned char>(value);
        hex.push_back(digits[byte >> 4U]);
        hex.push_back(digits[byte & 15U]);
    }
    std::cout << nlohmann::json{{"artifact_hex", hex},
                                {"target", volt::io::write_manufacturing_target(target)},
                                {"report", volt::io::write_manufacturing_coverage(result)}}
                     .dump()
              << '\n';
}
