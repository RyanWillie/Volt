#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <volt/circuit/electrical/queries.hpp>
#include <volt/circuit/electrical/records.hpp>
#include <volt/core/content_hash.hpp>
#include <volt/core/errors.hpp>

namespace {

using volt::ElectricalMeaning;
using volt::ElectricalObservable;
using volt::ElectricalPinIndex;
using volt::ElectricalRecordSelector;
using volt::ElectricalSubject;
using volt::ElectricalValue;
using volt::Quantity;
using volt::QuantityRange;
using volt::UnitDimension;

[[nodiscard]] Quantity volts(double value) { return Quantity{UnitDimension::Voltage, value}; }

[[nodiscard]] Quantity amps(double value) { return Quantity{UnitDimension::Current, value}; }

[[nodiscard]] ElectricalSubject supply() {
    return ElectricalSubject::supply_domain({ElectricalPinIndex{0}}, {ElectricalPinIndex{1}});
}

[[nodiscard]] ElectricalSubject junction() {
    return ElectricalSubject::directed_relation(ElectricalPinIndex{0}, ElectricalPinIndex{1});
}

[[nodiscard]] volt::ElectricalRecordSpec accepted_supply(double minimum, double maximum) {
    return volt::voltage_record(
        supply(), ElectricalMeaning::AcceptedRange,
        ElectricalValue{QuantityRange::bounded(volts(minimum), volts(maximum))});
}

[[nodiscard]] volt::ElectricalRecordSpec
characteristic(ElectricalSubject subject, ElectricalObservable observable, double value,
               std::vector<volt::ElectricalCondition> conditions = {}) {
    const auto quantity = observable == ElectricalObservable::Voltage ? volts(value) : amps(value);
    return volt::ElectricalRecordSpec{std::move(subject),
                                      observable,
                                      ElectricalMeaning::Characteristic,
                                      ElectricalValue{quantity},
                                      std::move(conditions),
                                      {}};
}

} // namespace

TEST_CASE("Electrical subjects enforce frame direction and domain cardinality") {
    const auto first = ElectricalPinIndex{0};
    const auto second = ElectricalPinIndex{1};

    CHECK(ElectricalSubject::framed_pin(first, second).kind() ==
          volt::ElectricalSubjectKind::FramedPin);
    CHECK(ElectricalSubject::directed_relation(first, second).kind() ==
          volt::ElectricalSubjectKind::DirectedRelation);
    CHECK(ElectricalSubject::supply_domain({first}, {second}).kind() ==
          volt::ElectricalSubjectKind::SupplyDomain);

    CHECK_THROWS_AS(ElectricalSubject::framed_pin(first, first), std::invalid_argument);
    CHECK_THROWS_AS(ElectricalSubject::directed_relation(first, first), std::invalid_argument);
    CHECK_THROWS_AS(ElectricalSubject::supply_domain({}, {second}), std::invalid_argument);
    CHECK_THROWS_AS(ElectricalSubject::supply_domain({first}, {}), std::invalid_argument);
    CHECK_THROWS_AS(ElectricalSubject::supply_domain({first, first}, {second}),
                    std::invalid_argument);
    CHECK_THROWS_AS(ElectricalSubject::supply_domain({first}, {first}), std::invalid_argument);
}

TEST_CASE("Electrical records normalize values and reject invalid meanings or units") {
    const auto characteristic_record = volt::ElectricalRecordSet{
        2,
        {volt::voltage_record(
            junction(), ElectricalMeaning::Characteristic,
            ElectricalValue{volt::TolerancedQuantity{volts(2.0), volt::Tolerance::percent(0.2)}})},
    };
    const auto &envelope =
        characteristic_record.records().front().value().as_characteristic_envelope();
    CHECK(envelope.minimum() == volts(1.6));
    CHECK(envelope.typical() == volts(2.0));
    CHECK(envelope.maximum() == volts(2.4));

    const auto provided_record = volt::ElectricalRecordSet{
        2,
        {volt::voltage_record(supply(), ElectricalMeaning::ProvidedRange,
                              ElectricalValue{volt::TolerancedQuantity{
                                  volts(3.3), volt::Tolerance::absolute(volts(0.1), volts(0.2))}})},
    };
    const auto &range = provided_record.records().front().value().as_range();
    REQUIRE(range.minimum().has_value());
    REQUIRE(range.maximum().has_value());
    CHECK(range.minimum()->value() == Catch::Approx(3.2));
    CHECK(range.maximum()->value() == Catch::Approx(3.5));

    CHECK_THROWS_AS(volt::ElectricalRecordSet(
                        2, {volt::voltage_record(
                               supply(), ElectricalMeaning::AcceptedRange,
                               ElectricalValue{QuantityRange::bounded(amps(1.0), amps(2.0))})}),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        volt::ElectricalRecordSet(
            2, {volt::voltage_record(supply(), ElectricalMeaning::Requirement,
                                     ElectricalValue{volt::ContinuousCurrent{amps(0.5)}})}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        volt::ElectricalRecordSet(
            2, {volt::current_record(junction(), ElectricalMeaning::Requirement,
                                     ElectricalValue{volt::ContinuousCurrent{amps(0.5)}})}),
        std::invalid_argument);
    CHECK_THROWS_AS(volt::ElectricalRecordSet(
                        2, {volt::voltage_record(supply(), ElectricalMeaning::Requirement,
                                                 ElectricalValue{volt::UnknownElectricalValue{}})}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::ElectricalRecordSet(
                        2, {volt::current_record(junction(), ElectricalMeaning::Capability,
                                                 ElectricalValue{volt::UnknownElectricalValue{}})}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::ContinuousCurrent{amps(-0.1)}, std::invalid_argument);
}

TEST_CASE("Electrical record update rejects dangling pins without partial state") {
    const auto records = volt::ElectricalRecordSet{2, {accepted_supply(3.0, 3.6)}};
    const auto original_key = records.records().front().semantic_key();

    const auto dangling =
        ElectricalSubject::framed_pin(ElectricalPinIndex{0}, ElectricalPinIndex{2});
    CHECK_THROWS_AS(records.with_record(
                        volt::voltage_record(dangling, ElectricalMeaning::AbsoluteLimit,
                                             ElectricalValue{QuantityRange::maximum(volts(5.5))})),
                    std::out_of_range);

    REQUIRE(records.records().size() == 1U);
    CHECK(records.records().front().semantic_key() == original_key);
}

TEST_CASE("Electrical conditions reject invalid bounds dimensions and references") {
    CHECK_THROWS_AS(volt::ElectricalCondition::range(junction(), ElectricalObservable::Voltage,
                                                     std::nullopt, std::nullopt),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        volt::ElectricalCondition::range(junction(), ElectricalObservable::Voltage,
                                         volt::ElectricalValueExpression::literal(volts(5.0)),
                                         volt::ElectricalValueExpression::literal(volts(3.0))),
        std::invalid_argument);
    CHECK_THROWS_AS(
        volt::ElectricalCondition::equal(junction(), ElectricalObservable::Voltage,
                                         volt::ElectricalValueExpression::literal(amps(0.02))),
        std::invalid_argument);
    CHECK_THROWS_AS(volt::ElectricalValueExpression::scaled_reference(
                        ElectricalRecordSelector{supply(), ElectricalObservable::Voltage,
                                                 ElectricalMeaning::Characteristic},
                        std::numeric_limits<double>::infinity()),
                    std::invalid_argument);

    const auto dangling =
        ElectricalSubject::framed_pin(ElectricalPinIndex{0}, ElectricalPinIndex{2});
    const auto condition =
        volt::ElectricalCondition::equal(dangling, ElectricalObservable::Voltage,
                                         volt::ElectricalValueExpression::literal(volts(3.3)));
    const auto records = volt::ElectricalRecordSet{2, {accepted_supply(3.0, 3.6)}};
    CHECK_THROWS_AS(records.with_record(characteristic(junction(), ElectricalObservable::Voltage,
                                                       2.0, {condition})),
                    std::out_of_range);
    CHECK(records.records().size() == 1U);
}

TEST_CASE("Semantic keys normalize unordered subjects conditions and evidence") {
    const auto domain_a =
        ElectricalSubject::supply_domain({ElectricalPinIndex{1}, ElectricalPinIndex{0}},
                                         {ElectricalPinIndex{3}, ElectricalPinIndex{2}});
    const auto domain_b =
        ElectricalSubject::supply_domain({ElectricalPinIndex{0}, ElectricalPinIndex{1}},
                                         {ElectricalPinIndex{2}, ElectricalPinIndex{3}});
    const auto voltage_condition =
        volt::ElectricalCondition::range(domain_a, ElectricalObservable::Voltage,
                                         volt::ElectricalValueExpression::literal(volts(3.0)),
                                         volt::ElectricalValueExpression::literal(volts(3.6)));
    const auto current_condition =
        volt::ElectricalCondition::equal(domain_a, ElectricalObservable::Current,
                                         volt::ElectricalValueExpression::literal(amps(0.02)));
    const auto first_evidence = volt::sha256_content_hash("datasheet-page-1");
    const auto second_evidence = volt::sha256_content_hash("datasheet-page-2");

    const auto first = volt::ElectricalRecordSet{
        4,
        {volt::ElectricalRecordSpec{domain_a,
                                    ElectricalObservable::Voltage,
                                    ElectricalMeaning::AbsoluteLimit,
                                    ElectricalValue{QuantityRange::maximum(volts(5.5))},
                                    {voltage_condition, current_condition},
                                    {second_evidence, first_evidence, first_evidence}}},
    };
    const auto second = volt::ElectricalRecordSet{
        4,
        {volt::ElectricalRecordSpec{domain_b,
                                    ElectricalObservable::Voltage,
                                    ElectricalMeaning::AbsoluteLimit,
                                    ElectricalValue{QuantityRange::maximum(volts(5.5))},
                                    {current_condition, voltage_condition},
                                    {first_evidence, second_evidence}}},
    };

    CHECK(first.records().front().semantic_key() == second.records().front().semantic_key());
    CHECK(first.records().front().evidence() == second.records().front().evidence());
    CHECK_THROWS_AS(
        volt::ElectricalRecordSet(
            4, {volt::ElectricalRecordSpec{domain_a,
                                           ElectricalObservable::Voltage,
                                           ElectricalMeaning::AbsoluteLimit,
                                           ElectricalValue{QuantityRange::maximum(volts(5.5))},
                                           {voltage_condition, voltage_condition},
                                           {}}}),
        std::invalid_argument);
}

TEST_CASE("Exact duplicate records canonicalize once and combine evidence") {
    const auto first_evidence = volt::sha256_content_hash("datasheet-page-1");
    const auto second_evidence = volt::sha256_content_hash("datasheet-page-2");
    const auto first = volt::voltage_record(
        supply(), ElectricalMeaning::AcceptedRange,
        ElectricalValue{QuantityRange::bounded(volts(3.0), volts(3.6))}, {}, {first_evidence});
    const auto second = volt::voltage_record(
        supply(), ElectricalMeaning::AcceptedRange,
        ElectricalValue{QuantityRange::bounded(volts(3.0), volts(3.6))}, {}, {second_evidence});

    const auto records = volt::ElectricalRecordSet{2, {first, second}};
    auto expected_evidence = std::vector<volt::ContentHash>{first_evidence, second_evidence};
    std::ranges::sort(expected_evidence, {},
                      [](const volt::ContentHash &hash) { return hash.value(); });

    REQUIRE(records.records().size() == 1U);
    CHECK(records.records().front().evidence() == expected_evidence);
}

TEST_CASE("Condition references reject dangling cyclic and multi-step graphs") {
    const auto first_subject =
        ElectricalSubject::framed_pin(ElectricalPinIndex{0}, ElectricalPinIndex{1});
    const auto second_subject =
        ElectricalSubject::framed_pin(ElectricalPinIndex{2}, ElectricalPinIndex{3});
    const auto third_subject =
        ElectricalSubject::framed_pin(ElectricalPinIndex{4}, ElectricalPinIndex{5});
    const auto first_selector = ElectricalRecordSelector{
        first_subject, ElectricalObservable::Voltage, ElectricalMeaning::Characteristic};
    const auto second_selector = ElectricalRecordSelector{
        second_subject, ElectricalObservable::Current, ElectricalMeaning::Characteristic};
    const auto third_selector = ElectricalRecordSelector{
        third_subject, ElectricalObservable::Voltage, ElectricalMeaning::Characteristic};

    const auto condition_on_second = volt::ElectricalCondition::equal(
        second_subject, ElectricalObservable::Current,
        volt::ElectricalValueExpression::scaled_reference(second_selector, 1.0));
    CHECK_THROWS_AS(
        volt::ElectricalRecordSet(6, {characteristic(first_subject, ElectricalObservable::Voltage,
                                                     3.3, {condition_on_second})}),
        std::invalid_argument);

    const auto condition_on_first = volt::ElectricalCondition::equal(
        first_subject, ElectricalObservable::Voltage,
        volt::ElectricalValueExpression::scaled_reference(first_selector, 1.0));
    CHECK_THROWS_AS(
        volt::ElectricalRecordSet(6, {characteristic(first_subject, ElectricalObservable::Voltage,
                                                     3.3, {condition_on_second}),
                                      characteristic(second_subject, ElectricalObservable::Current,
                                                     0.02, {condition_on_first})}),
        std::invalid_argument);

    const auto condition_on_third = volt::ElectricalCondition::equal(
        third_subject, ElectricalObservable::Voltage,
        volt::ElectricalValueExpression::scaled_reference(third_selector, 1.0));
    CHECK_THROWS_AS(volt::ElectricalRecordSet(
                        6, {characteristic(first_subject, ElectricalObservable::Voltage, 3.3,
                                           {condition_on_second}),
                            characteristic(second_subject, ElectricalObservable::Current, 0.02,
                                           {condition_on_third}),
                            characteristic(third_subject, ElectricalObservable::Voltage, 1.8)}),
                    std::invalid_argument);
}

TEST_CASE("Typed queries merge compatible values and expose well-formed conflicts") {
    const auto records = volt::ElectricalRecordSet{
        2,
        {
            accepted_supply(3.0, 3.6),
            accepted_supply(3.1, 3.5),
            volt::current_record(supply(), ElectricalMeaning::Requirement,
                                 ElectricalValue{volt::ContinuousCurrent{amps(0.4)}}),
            volt::current_record(supply(), ElectricalMeaning::Requirement,
                                 ElectricalValue{volt::ContinuousCurrent{amps(0.5)}}),
            volt::current_record(supply(), ElectricalMeaning::Capability,
                                 ElectricalValue{volt::ContinuousCurrent{amps(0.8)}}),
            volt::current_record(supply(), ElectricalMeaning::Capability,
                                 ElectricalValue{volt::ContinuousCurrent{amps(0.6)}}),
            characteristic(junction(), ElectricalObservable::Voltage, 1.8),
            characteristic(junction(), ElectricalObservable::Voltage, 2.0),
            volt::voltage_record(junction(), ElectricalMeaning::Characteristic,
                                 ElectricalValue{volt::UnknownElectricalValue{}}),
        },
    };

    const auto accepted = volt::queries::electrical_record_group(
        records, ElectricalRecordSelector{supply(), ElectricalObservable::Voltage,
                                          ElectricalMeaning::AcceptedRange});
    REQUIRE(accepted.has_value());
    REQUIRE(accepted->status() == volt::ElectricalMergeStatus::Effective);
    CHECK(accepted->effective_value()->as_range().minimum() == volts(3.1));
    CHECK(accepted->effective_value()->as_range().maximum() == volts(3.5));

    const auto requirement = volt::queries::electrical_record_group(
        records, ElectricalRecordSelector{supply(), ElectricalObservable::Current,
                                          ElectricalMeaning::Requirement});
    REQUIRE(requirement.has_value());
    REQUIRE(requirement->status() == volt::ElectricalMergeStatus::Effective);
    CHECK(requirement->effective_value()->as_continuous_current().value() == amps(0.5));

    const auto capability = volt::queries::electrical_record_group(
        records, ElectricalRecordSelector{supply(), ElectricalObservable::Current,
                                          ElectricalMeaning::Capability});
    REQUIRE(capability.has_value());
    REQUIRE(capability->status() == volt::ElectricalMergeStatus::Effective);
    CHECK(capability->effective_value()->as_continuous_current().value() == amps(0.6));

    const auto led = volt::queries::electrical_record_group(
        records, ElectricalRecordSelector{junction(), ElectricalObservable::Voltage,
                                          ElectricalMeaning::Characteristic});
    REQUIRE(led.has_value());
    CHECK(led->status() == volt::ElectricalMergeStatus::Conflict);
    CHECK(led->has_unknown());
    CHECK_FALSE(led->effective_value().has_value());

    const auto diagnostics = volt::validate_electrical_records(records);
    CHECK(std::ranges::any_of(diagnostics.diagnostics(), [](const auto &diagnostic) {
        return diagnostic.code() == volt::DiagnosticCode{"ELECTRICAL_RECORD_CONFLICT"};
    }));

    const auto accepted_record = volt::ElectricalRecord::from(accepted_supply(3.0, 3.6));
    const auto characteristic_record = volt::ElectricalRecord::from(
        characteristic(junction(), ElectricalObservable::Voltage, 2.0));
    CHECK_THROWS_AS(volt::ElectricalRecordGroup::from({accepted_record, characteristic_record}),
                    std::invalid_argument);
}

TEST_CASE("Unknown remains typed and diagnosable without erasing known evidence") {
    const auto records = volt::ElectricalRecordSet{
        2,
        {
            accepted_supply(3.0, 3.6),
            volt::voltage_record(supply(), ElectricalMeaning::AcceptedRange,
                                 ElectricalValue{volt::UnknownElectricalValue{}}),
        },
    };
    const auto group = volt::queries::electrical_record_group(
        records, ElectricalRecordSelector{supply(), ElectricalObservable::Voltage,
                                          ElectricalMeaning::AcceptedRange});

    REQUIRE(group.has_value());
    CHECK(group->status() == volt::ElectricalMergeStatus::Effective);
    CHECK(group->has_unknown());
    REQUIRE(group->effective_value().has_value());

    const auto diagnostics = volt::validate_electrical_records(records);
    CHECK(std::ranges::any_of(diagnostics.diagnostics(), [](const auto &diagnostic) {
        return diagnostic.code() == volt::DiagnosticCode{"ELECTRICAL_RECORD_UNKNOWN"};
    }));
}

TEST_CASE("Canonical observable helpers lower to the generic bounded record shape") {
    const auto value = ElectricalValue{QuantityRange::bounded(volts(3.0), volts(3.6))};
    const auto standard = volt::voltage_record(supply(), ElectricalMeaning::AcceptedRange, value);
    const auto custom = volt::ElectricalRecordSpec{
        supply(), ElectricalObservable::Voltage, ElectricalMeaning::AcceptedRange, value, {}, {}};
    const auto standard_records = volt::ElectricalRecordSet{2, {standard}};
    const auto custom_records = volt::ElectricalRecordSet{2, {custom}};

    CHECK(standard_records.records().front().semantic_key() ==
          custom_records.records().front().semantic_key());
    CHECK(standard_records.records().front().value() == custom_records.records().front().value());
}
