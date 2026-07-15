#include <volt/circuit/electrical/records.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include <volt/circuit/electrical/queries.hpp>
#include <volt/core/errors.hpp>

namespace volt {

namespace {

[[nodiscard]] UnitDimension dimension_for(ElectricalObservable observable) noexcept {
    return observable == ElectricalObservable::Voltage ? UnitDimension::Voltage
                                                       : UnitDimension::Current;
}

[[nodiscard]] std::string canonical_number(double value) {
    auto buffer = std::array<char, 64>{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                      std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) {
        throw KernelLogicError{ErrorCode::InvalidState,
                               "Failed to encode canonical electrical number"};
    }
    return std::string{buffer.data(), result.ptr};
}

[[nodiscard]] std::string observable_key(ElectricalObservable observable) {
    return observable == ElectricalObservable::Voltage ? "voltage" : "current";
}

[[nodiscard]] std::string meaning_key(ElectricalMeaning meaning) {
    switch (meaning) {
    case ElectricalMeaning::Characteristic:
        return "characteristic";
    case ElectricalMeaning::AcceptedRange:
        return "accepted_range";
    case ElectricalMeaning::ProvidedRange:
        return "provided_range";
    case ElectricalMeaning::AbsoluteLimit:
        return "absolute_limit";
    case ElectricalMeaning::Requirement:
        return "requirement";
    case ElectricalMeaning::Capability:
        return "capability";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled electrical meaning"};
}

[[nodiscard]] std::string subject_key(const ElectricalSubject &subject) {
    auto out = std::ostringstream{};
    switch (subject.kind()) {
    case ElectricalSubjectKind::FramedPin: {
        const auto &framed = subject.as_framed_pin();
        out << "framed:" << framed.pin.value() << ':' << framed.reference.value();
        break;
    }
    case ElectricalSubjectKind::DirectedRelation: {
        const auto &relation = subject.as_directed_relation();
        out << "relation:" << relation.from.value() << ':' << relation.to.value();
        break;
    }
    case ElectricalSubjectKind::SupplyDomain: {
        const auto &domain = subject.as_supply_domain();
        out << "domain:+";
        for (const auto pin : domain.positive_pins()) {
            out << pin.value() << ',';
        }
        out << "-";
        for (const auto pin : domain.return_pins()) {
            out << pin.value() << ',';
        }
        break;
    }
    }
    return out.str();
}

[[nodiscard]] std::string selector_key(const ElectricalRecordSelector &selector) {
    return subject_key(selector.subject) + '|' + observable_key(selector.observable) + '|' +
           meaning_key(selector.meaning);
}

[[nodiscard]] std::string expression_key(const ElectricalValueExpression &expression) {
    if (expression.is_literal()) {
        const auto &literal = expression.as_literal();
        return "literal:" +
               observable_key(literal.dimension() == UnitDimension::Voltage
                                  ? ElectricalObservable::Voltage
                                  : ElectricalObservable::Current) +
               ':' + canonical_number(literal.value());
    }
    const auto &reference = expression.as_scaled_reference();
    return "reference:" + selector_key(reference.selector()) + ':' +
           canonical_number(reference.scale());
}

[[nodiscard]] std::string condition_key(const ElectricalCondition &condition) {
    auto result =
        subject_key(condition.subject()) + '|' + observable_key(condition.observable()) + '|';
    if (condition.predicate_kind() == ElectricalConditionPredicateKind::Equal) {
        return result + "equal:" + expression_key(*condition.minimum());
    }
    result += "range:";
    result += condition.minimum().has_value() ? expression_key(*condition.minimum()) : "none";
    result += ':';
    result += condition.maximum().has_value() ? expression_key(*condition.maximum()) : "none";
    return result;
}

[[nodiscard]] std::string semantic_key_input(const ElectricalSubject &subject,
                                             ElectricalObservable observable,
                                             ElectricalMeaning meaning,
                                             const std::vector<ElectricalCondition> &conditions) {
    auto out = std::ostringstream{};
    out << "volt.electrical-semantic-key\n1\n"
        << subject_key(subject) << '\n'
        << observable_key(observable) << '\n'
        << meaning_key(meaning) << '\n';
    for (const auto &condition : conditions) {
        out << condition_key(condition) << '\n';
    }
    return out.str();
}

[[nodiscard]] bool tolerance_matches_nominal(const TolerancedQuantity &value) noexcept {
    if (value.tolerance().mode() == ToleranceMode::Percent) {
        return value.tolerance().minus().dimension() == UnitDimension::Ratio &&
               value.tolerance().plus().dimension() == UnitDimension::Ratio;
    }
    return value.tolerance().minus().dimension() == value.nominal().dimension() &&
           value.tolerance().plus().dimension() == value.nominal().dimension();
}

[[nodiscard]] std::pair<double, double> tolerance_deltas(const TolerancedQuantity &value) {
    if (value.tolerance().mode() == ToleranceMode::Absolute) {
        return {value.tolerance().minus().value(), value.tolerance().plus().value()};
    }
    const auto magnitude = std::abs(value.nominal().value());
    return {magnitude * value.tolerance().minus().value(),
            magnitude * value.tolerance().plus().value()};
}

[[nodiscard]] ElectricalValue normalize_value(ElectricalObservable observable,
                                              ElectricalMeaning meaning,
                                              const ElectricalSubject &subject,
                                              ElectricalValue value) {
    const auto expected_dimension = dimension_for(observable);
    if (value.kind() == ElectricalValueKind::TolerancedQuantity) {
        const auto &toleranced = value.as_toleranced_quantity();
        if (toleranced.nominal().dimension() != expected_dimension ||
            !tolerance_matches_nominal(toleranced)) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Electrical tolerance dimension does not match observable"};
        }
        if (meaning == ElectricalMeaning::Requirement || meaning == ElectricalMeaning::Capability) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Current requirements and capabilities do not accept tolerance"};
        }
        const auto [minus, plus] = tolerance_deltas(toleranced);
        const auto minimum = Quantity{expected_dimension, toleranced.nominal().value() - minus};
        const auto maximum = Quantity{expected_dimension, toleranced.nominal().value() + plus};
        if (meaning == ElectricalMeaning::Characteristic) {
            value = ElectricalValue{CharacteristicEnvelope{minimum, toleranced.nominal(), maximum}};
        } else {
            value = ElectricalValue{QuantityRange::bounded(minimum, maximum)};
        }
    }

    if (meaning == ElectricalMeaning::Requirement || meaning == ElectricalMeaning::Capability) {
        if (observable != ElectricalObservable::Current ||
            subject.kind() != ElectricalSubjectKind::SupplyDomain) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Requirement and Capability require Current on a supply domain"};
        }
        if (value.kind() == ElectricalValueKind::Unknown) {
            return value;
        }
        if (value.kind() != ElectricalValueKind::ContinuousCurrent) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Requirement and Capability require a continuous Current value"};
        }
    } else if (value.kind() == ElectricalValueKind::Unknown) {
        return value;
    } else if (meaning == ElectricalMeaning::Characteristic) {
        const auto valid = value.kind() == ElectricalValueKind::Quantity ||
                           value.kind() == ElectricalValueKind::CharacteristicEnvelope;
        if (!valid) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Characteristic requires a scalar or min/typical/max envelope"};
        }
    } else if (meaning == ElectricalMeaning::AcceptedRange ||
               meaning == ElectricalMeaning::ProvidedRange ||
               meaning == ElectricalMeaning::AbsoluteLimit) {
        if (value.kind() != ElectricalValueKind::Range) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Range and limit meanings require a quantity range"};
        }
    }

    const auto actual_dimension = [&value]() {
        switch (value.kind()) {
        case ElectricalValueKind::Quantity:
            return value.as_quantity().dimension();
        case ElectricalValueKind::CharacteristicEnvelope:
            return value.as_characteristic_envelope().typical().dimension();
        case ElectricalValueKind::Range:
            return value.as_range().dimension();
        case ElectricalValueKind::ContinuousCurrent:
            return value.as_continuous_current().value().dimension();
        case ElectricalValueKind::Unknown:
        case ElectricalValueKind::TolerancedQuantity:
            break;
        }
        throw KernelLogicError{ErrorCode::InvalidState, "Unhandled normalized electrical value"};
    }();
    if (actual_dimension != expected_dimension) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical value dimension does not match observable"};
    }
    return value;
}

void validate_expression_dimension(const ElectricalValueExpression &expression,
                                   ElectricalObservable observable) {
    if (expression.is_literal()) {
        if (expression.as_literal().dimension() != dimension_for(observable)) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Condition literal dimension does not match observable"};
        }
        return;
    }
    if (expression.as_scaled_reference().selector().observable != observable) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Condition reference dimension does not match observable"};
    }
}

[[nodiscard]] std::vector<ElectricalCondition>
normalize_conditions(std::vector<ElectricalCondition> conditions) {
    for (const auto &condition : conditions) {
        if (condition.minimum().has_value()) {
            validate_expression_dimension(*condition.minimum(), condition.observable());
        }
        if (condition.maximum().has_value()) {
            validate_expression_dimension(*condition.maximum(), condition.observable());
        }
    }
    std::ranges::sort(conditions, {}, condition_key);
    for (std::size_t index = 1; index < conditions.size(); ++index) {
        if (condition_key(conditions[index - 1U]) == condition_key(conditions[index])) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Electrical condition set contains a duplicate predicate"};
        }
    }
    return conditions;
}

[[nodiscard]] std::vector<ContentHash> normalize_evidence(std::vector<ContentHash> evidence) {
    std::ranges::sort(evidence, {}, [](const ContentHash &hash) { return hash.value(); });
    evidence.erase(std::unique(evidence.begin(), evidence.end()), evidence.end());
    return evidence;
}

void validate_subject_pins(const ElectricalSubject &subject, std::size_t pin_count) {
    const auto validate = [pin_count](ElectricalPinIndex pin) {
        if (pin.value() >= pin_count) {
            throw KernelRangeError{ErrorCode::UnknownEntity,
                                   "Electrical subject pin index is outside its record set"};
        }
    };
    switch (subject.kind()) {
    case ElectricalSubjectKind::FramedPin:
        validate(subject.as_framed_pin().pin);
        validate(subject.as_framed_pin().reference);
        return;
    case ElectricalSubjectKind::DirectedRelation:
        validate(subject.as_directed_relation().from);
        validate(subject.as_directed_relation().to);
        return;
    case ElectricalSubjectKind::SupplyDomain:
        for (const auto pin : subject.as_supply_domain().positive_pins()) {
            validate(pin);
        }
        for (const auto pin : subject.as_supply_domain().return_pins()) {
            validate(pin);
        }
        return;
    }
}

[[nodiscard]] std::string value_key(const ElectricalValue &value) {
    auto out = std::ostringstream{};
    switch (value.kind()) {
    case ElectricalValueKind::Unknown:
        return "unknown";
    case ElectricalValueKind::Quantity:
        return "quantity:" + canonical_number(value.as_quantity().value());
    case ElectricalValueKind::CharacteristicEnvelope:
        out << "envelope:" << canonical_number(value.as_characteristic_envelope().minimum().value())
            << ':' << canonical_number(value.as_characteristic_envelope().typical().value()) << ':'
            << canonical_number(value.as_characteristic_envelope().maximum().value());
        return out.str();
    case ElectricalValueKind::Range:
        out << "range:";
        if (value.as_range().minimum().has_value()) {
            out << canonical_number(value.as_range().minimum()->value());
        }
        out << ':';
        if (value.as_range().maximum().has_value()) {
            out << canonical_number(value.as_range().maximum()->value());
        }
        return out.str();
    case ElectricalValueKind::ContinuousCurrent:
        return "continuous:" + canonical_number(value.as_continuous_current().value().value());
    case ElectricalValueKind::TolerancedQuantity:
        break;
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Toleranced value escaped normalization"};
}

[[nodiscard]] std::optional<QuantityRange> intersect_ranges(const QuantityRange &lhs,
                                                            const QuantityRange &rhs) {
    auto minimum = lhs.minimum();
    if (!minimum.has_value() ||
        (rhs.minimum().has_value() && rhs.minimum()->value() > minimum->value())) {
        minimum = rhs.minimum();
    }
    auto maximum = lhs.maximum();
    if (!maximum.has_value() ||
        (rhs.maximum().has_value() && rhs.maximum()->value() < maximum->value())) {
        maximum = rhs.maximum();
    }
    if (minimum.has_value() && maximum.has_value() && minimum->value() > maximum->value()) {
        return std::nullopt;
    }
    if (minimum.has_value() && maximum.has_value()) {
        return QuantityRange::bounded(*minimum, *maximum);
    }
    if (minimum.has_value()) {
        return QuantityRange::minimum(*minimum);
    }
    return QuantityRange::maximum(*maximum);
}

[[nodiscard]] bool selector_matches(const ElectricalRecordSelector &selector,
                                    const ElectricalRecord &record) noexcept {
    return record.subject() == selector.subject && record.observable() == selector.observable &&
           record.meaning() == selector.meaning;
}

[[nodiscard]] std::vector<ElectricalSemanticKey>
matching_semantic_keys(const std::vector<ElectricalRecord> &records,
                       const ElectricalRecordSelector &selector) {
    auto keys = std::vector<ElectricalSemanticKey>{};
    for (const auto &record : records) {
        if (selector_matches(selector, record)) {
            keys.push_back(record.semantic_key());
        }
    }
    std::ranges::sort(keys);
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

[[nodiscard]] std::vector<ElectricalRecordSelector>
condition_references(const ElectricalRecord &record) {
    auto result = std::vector<ElectricalRecordSelector>{};
    const auto append = [&result](const std::optional<ElectricalValueExpression> &expression) {
        if (expression.has_value() && !expression->is_literal()) {
            result.push_back(expression->as_scaled_reference().selector());
        }
    };
    for (const auto &condition : record.conditions()) {
        append(condition.minimum());
        append(condition.maximum());
    }
    return result;
}

[[nodiscard]] bool is_referenceable_scalar(const std::vector<ElectricalRecord> &records,
                                           const ElectricalSemanticKey &key) {
    const ElectricalValue *scalar = nullptr;
    for (const auto &record : records) {
        if (record.semantic_key() != key) {
            continue;
        }
        if (record.value().kind() != ElectricalValueKind::Quantity &&
            record.value().kind() != ElectricalValueKind::ContinuousCurrent) {
            return false;
        }
        if (scalar != nullptr && *scalar != record.value()) {
            return false;
        }
        scalar = &record.value();
    }
    return scalar != nullptr;
}

void validate_references(const std::vector<ElectricalRecord> &records) {
    auto edges = std::map<ElectricalSemanticKey, std::set<ElectricalSemanticKey>>{};
    for (const auto &record : records) {
        for (const auto &selector : condition_references(record)) {
            const auto matches = matching_semantic_keys(records, selector);
            if (matches.empty()) {
                throw KernelArgumentError{ErrorCode::InvalidArgument,
                                          "Electrical condition reference is dangling"};
            }
            if (matches.size() != 1U || !is_referenceable_scalar(records, matches.front())) {
                throw KernelArgumentError{
                    ErrorCode::InvalidArgument,
                    "Electrical condition reference must resolve to one known scalar value"};
            }
            edges[record.semantic_key()].insert(matches.front());
        }
    }

    auto visiting = std::set<ElectricalSemanticKey>{};
    auto visited = std::set<ElectricalSemanticKey>{};
    const auto visit = [&](const auto &self, const ElectricalSemanticKey &node) -> void {
        if (visiting.contains(node)) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Electrical condition references must be acyclic"};
        }
        if (visited.contains(node)) {
            return;
        }
        visiting.insert(node);
        if (const auto it = edges.find(node); it != edges.end()) {
            for (const auto &target : it->second) {
                self(self, target);
            }
        }
        visiting.erase(node);
        visited.insert(node);
    };
    for (const auto &[node, unused] : edges) {
        static_cast<void>(unused);
        visit(visit, node);
    }
    for (const auto &[source, targets] : edges) {
        static_cast<void>(source);
        for (const auto &target : targets) {
            if (edges.contains(target) && !edges.at(target).empty()) {
                throw KernelArgumentError{
                    ErrorCode::InvalidArgument,
                    "Multi-step electrical condition reference graphs are not supported"};
            }
        }
    }
}

} // namespace

ElectricalRecord ElectricalRecord::from(ElectricalRecordSpec spec) {
    spec.conditions = normalize_conditions(std::move(spec.conditions));
    spec.evidence = normalize_evidence(std::move(spec.evidence));
    spec.value =
        normalize_value(spec.observable, spec.meaning, spec.subject, std::move(spec.value));
    return ElectricalRecord{ElectricalSemanticKey{sha256_content_hash(semantic_key_input(
                                spec.subject, spec.observable, spec.meaning, spec.conditions))},
                            std::move(spec.subject),
                            spec.observable,
                            spec.meaning,
                            std::move(spec.value),
                            std::move(spec.conditions),
                            std::move(spec.evidence)};
}

SupplyDomainSubject::SupplyDomainSubject(std::vector<ElectricalPinIndex> positive_pins,
                                         std::vector<ElectricalPinIndex> return_pins)
    : positive_pins_{std::move(positive_pins)}, return_pins_{std::move(return_pins)} {
    if (positive_pins_.empty() || return_pins_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Supply domain sides must each contain at least one pin"};
    }
    const auto normalize = [](auto &pins) {
        std::ranges::sort(pins);
        if (std::adjacent_find(pins.begin(), pins.end()) != pins.end()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Supply domain sides must not repeat pins"};
        }
    };
    normalize(positive_pins_);
    normalize(return_pins_);
    for (const auto pin : positive_pins_) {
        if (std::ranges::binary_search(return_pins_, pin)) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Supply domain positive and return pins must be disjoint"};
        }
    }
}

ElectricalSubject ElectricalSubject::framed_pin(ElectricalPinIndex pin,
                                                ElectricalPinIndex reference) {
    if (pin == reference) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Framed pin and reference must be distinct"};
    }
    return ElectricalSubject{FramedPinSubject{pin, reference}};
}

ElectricalSubject ElectricalSubject::directed_relation(ElectricalPinIndex from,
                                                       ElectricalPinIndex to) {
    if (from == to) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Directed relation pins must be distinct"};
    }
    return ElectricalSubject{DirectedRelationSubject{from, to}};
}

ElectricalSubject ElectricalSubject::supply_domain(std::vector<ElectricalPinIndex> positive_pins,
                                                   std::vector<ElectricalPinIndex> return_pins) {
    return ElectricalSubject{SupplyDomainSubject{std::move(positive_pins), std::move(return_pins)}};
}

ElectricalSubjectKind ElectricalSubject::kind() const noexcept {
    return static_cast<ElectricalSubjectKind>(value_.index());
}

const FramedPinSubject &ElectricalSubject::as_framed_pin() const {
    return std::get<FramedPinSubject>(value_);
}

const DirectedRelationSubject &ElectricalSubject::as_directed_relation() const {
    return std::get<DirectedRelationSubject>(value_);
}

const SupplyDomainSubject &ElectricalSubject::as_supply_domain() const {
    return std::get<SupplyDomainSubject>(value_);
}

CharacteristicEnvelope::CharacteristicEnvelope(Quantity minimum, Quantity typical, Quantity maximum)
    : minimum_{minimum}, typical_{typical}, maximum_{maximum} {
    if (minimum_.dimension() != typical_.dimension() ||
        typical_.dimension() != maximum_.dimension()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Characteristic envelope dimensions must match"};
    }
    if (minimum_.value() > typical_.value() || typical_.value() > maximum_.value()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Characteristic envelope must be ordered min/typical/max"};
    }
}

TolerancedQuantity::TolerancedQuantity(Quantity nominal, Tolerance tolerance)
    : nominal_{nominal}, tolerance_{std::move(tolerance)} {
    if (!tolerance_matches_nominal(*this)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Tolerance dimension does not match nominal quantity"};
    }
}

ContinuousCurrent::ContinuousCurrent(Quantity value) : value_{value} {
    if (value_.dimension() != UnitDimension::Current || value_.value() < 0.0) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Continuous Current must be a non-negative Current quantity"};
    }
}

ElectricalValueKind ElectricalValue::kind() const noexcept {
    return static_cast<ElectricalValueKind>(value_.index());
}

const Quantity &ElectricalValue::as_quantity() const { return std::get<Quantity>(value_); }

const CharacteristicEnvelope &ElectricalValue::as_characteristic_envelope() const {
    return std::get<CharacteristicEnvelope>(value_);
}

const QuantityRange &ElectricalValue::as_range() const { return std::get<QuantityRange>(value_); }

const ContinuousCurrent &ElectricalValue::as_continuous_current() const {
    return std::get<ContinuousCurrent>(value_);
}

const TolerancedQuantity &ElectricalValue::as_toleranced_quantity() const {
    return std::get<TolerancedQuantity>(value_);
}

bool operator==(const ElectricalValue &lhs, const ElectricalValue &rhs) noexcept {
    if (lhs.kind() != rhs.kind()) {
        return false;
    }
    switch (lhs.kind()) {
    case ElectricalValueKind::Unknown:
        return true;
    case ElectricalValueKind::Quantity:
        return lhs.as_quantity() == rhs.as_quantity();
    case ElectricalValueKind::CharacteristicEnvelope:
        return lhs.as_characteristic_envelope() == rhs.as_characteristic_envelope();
    case ElectricalValueKind::Range:
        return lhs.as_range().dimension() == rhs.as_range().dimension() &&
               lhs.as_range().minimum() == rhs.as_range().minimum() &&
               lhs.as_range().maximum() == rhs.as_range().maximum();
    case ElectricalValueKind::ContinuousCurrent:
        return lhs.as_continuous_current() == rhs.as_continuous_current();
    case ElectricalValueKind::TolerancedQuantity:
        return lhs.as_toleranced_quantity().nominal() == rhs.as_toleranced_quantity().nominal() &&
               lhs.as_toleranced_quantity().tolerance().mode() ==
                   rhs.as_toleranced_quantity().tolerance().mode() &&
               lhs.as_toleranced_quantity().tolerance().minus() ==
                   rhs.as_toleranced_quantity().tolerance().minus() &&
               lhs.as_toleranced_quantity().tolerance().plus() ==
                   rhs.as_toleranced_quantity().tolerance().plus();
    }
    return false;
}

ScaledElectricalValueReference::ScaledElectricalValueReference(ElectricalRecordSelector selector,
                                                               double scale)
    : selector_{std::move(selector)}, scale_{scale} {
    if (!std::isfinite(scale_)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical reference scale must be finite"};
    }
}

ElectricalValueExpression ElectricalValueExpression::literal(Quantity value) {
    if (value.dimension() != UnitDimension::Voltage &&
        value.dimension() != UnitDimension::Current) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical condition literals must be Voltage or Current"};
    }
    return ElectricalValueExpression{value};
}

ElectricalValueExpression
ElectricalValueExpression::scaled_reference(ElectricalRecordSelector selector, double scale) {
    return ElectricalValueExpression{ScaledElectricalValueReference{std::move(selector), scale}};
}

bool ElectricalValueExpression::is_literal() const noexcept {
    return std::holds_alternative<Quantity>(value_);
}

const Quantity &ElectricalValueExpression::as_literal() const { return std::get<Quantity>(value_); }

const ScaledElectricalValueReference &ElectricalValueExpression::as_scaled_reference() const {
    return std::get<ScaledElectricalValueReference>(value_);
}

ElectricalCondition ElectricalCondition::equal(ElectricalSubject subject,
                                               ElectricalObservable observable,
                                               ElectricalValueExpression value) {
    return ElectricalCondition{std::move(subject), observable,
                               ElectricalConditionPredicateKind::Equal, std::move(value),
                               std::nullopt};
}

ElectricalCondition ElectricalCondition::range(ElectricalSubject subject,
                                               ElectricalObservable observable,
                                               std::optional<ElectricalValueExpression> minimum,
                                               std::optional<ElectricalValueExpression> maximum) {
    return ElectricalCondition{std::move(subject), observable,
                               ElectricalConditionPredicateKind::Range, std::move(minimum),
                               std::move(maximum)};
}

ElectricalCondition::ElectricalCondition(ElectricalSubject subject, ElectricalObservable observable,
                                         ElectricalConditionPredicateKind predicate_kind,
                                         std::optional<ElectricalValueExpression> minimum,
                                         std::optional<ElectricalValueExpression> maximum)
    : subject_{std::move(subject)}, observable_{observable}, predicate_kind_{predicate_kind},
      minimum_{std::move(minimum)}, maximum_{std::move(maximum)} {
    if (!minimum_.has_value() && !maximum_.has_value()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical condition must contain a value or bound"};
    }
    if (predicate_kind_ == ElectricalConditionPredicateKind::Equal && maximum_.has_value()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical equality condition accepts exactly one value"};
    }
    if (minimum_.has_value()) {
        validate_expression_dimension(*minimum_, observable_);
    }
    if (maximum_.has_value()) {
        validate_expression_dimension(*maximum_, observable_);
    }
    if (predicate_kind_ == ElectricalConditionPredicateKind::Range && minimum_.has_value() &&
        maximum_.has_value() && minimum_->is_literal() && maximum_->is_literal() &&
        minimum_->as_literal().value() > maximum_->as_literal().value()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical condition minimum must not exceed maximum"};
    }
}

ElectricalRecordSpec voltage_record(ElectricalSubject subject, ElectricalMeaning meaning,
                                    ElectricalValue value,
                                    std::vector<ElectricalCondition> conditions,
                                    std::vector<ContentHash> evidence) {
    return ElectricalRecordSpec{
        std::move(subject), ElectricalObservable::Voltage, meaning,
        std::move(value),   std::move(conditions),         std::move(evidence)};
}

ElectricalRecordSpec current_record(ElectricalSubject subject, ElectricalMeaning meaning,
                                    ElectricalValue value,
                                    std::vector<ElectricalCondition> conditions,
                                    std::vector<ContentHash> evidence) {
    return ElectricalRecordSpec{
        std::move(subject), ElectricalObservable::Current, meaning,
        std::move(value),   std::move(conditions),         std::move(evidence)};
}

ElectricalSemanticKey electrical_semantic_key(ElectricalRecordSpec record) {
    return ElectricalRecord::from(std::move(record)).semantic_key();
}

ElectricalRecord::ElectricalRecord(ElectricalSemanticKey semantic_key, ElectricalSubject subject,
                                   ElectricalObservable observable, ElectricalMeaning meaning,
                                   ElectricalValue value,
                                   std::vector<ElectricalCondition> conditions,
                                   std::vector<ContentHash> evidence)
    : semantic_key_{std::move(semantic_key)}, subject_{std::move(subject)}, observable_{observable},
      meaning_{meaning}, value_{std::move(value)}, conditions_{std::move(conditions)},
      evidence_{std::move(evidence)} {}

ElectricalRecordSet::ElectricalRecordSet(std::size_t pin_count,
                                         std::vector<ElectricalRecordSpec> records)
    : pin_count_{pin_count} {
    if (pin_count_ == 0U) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical record set requires at least one contract pin"};
    }
    auto candidate = std::vector<ElectricalRecord>{};
    candidate.reserve(records.size());
    for (auto &spec : records) {
        auto record = ElectricalRecord::from(std::move(spec));
        validate_subject_pins(record.subject(), pin_count_);
        for (const auto &condition : record.conditions()) {
            validate_subject_pins(condition.subject(), pin_count_);
        }
        const auto duplicate =
            std::find_if(candidate.begin(), candidate.end(), [&](const auto &item) {
                return item.semantic_key() == record.semantic_key() &&
                       item.value() == record.value();
            });
        if (duplicate != candidate.end()) {
            auto evidence = duplicate->evidence();
            evidence.insert(evidence.end(), record.evidence().begin(), record.evidence().end());
            *duplicate = ElectricalRecord::from(ElectricalRecordSpec{
                duplicate->subject(), duplicate->observable(), duplicate->meaning(),
                duplicate->value(), duplicate->conditions(), std::move(evidence)});
        } else {
            candidate.push_back(std::move(record));
        }
    }
    std::ranges::sort(candidate, {}, [](const ElectricalRecord &record) {
        return std::pair{record.semantic_key().hash().value(), value_key(record.value())};
    });
    validate_references(candidate);
    records_ = std::move(candidate);
}

ElectricalRecordSet ElectricalRecordSet::with_record(ElectricalRecordSpec record) const {
    auto candidate_specs = std::vector<ElectricalRecordSpec>{};
    candidate_specs.reserve(records_.size() + 1U);
    for (const auto &existing : records_) {
        candidate_specs.push_back(ElectricalRecordSpec{existing.subject(), existing.observable(),
                                                       existing.meaning(), existing.value(),
                                                       existing.conditions(), existing.evidence()});
    }
    candidate_specs.push_back(std::move(record));
    return ElectricalRecordSet{pin_count_, std::move(candidate_specs)};
}

ElectricalRecordGroup ElectricalRecordGroup::from(std::vector<ElectricalRecord> source_records) {
    if (source_records.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical record group requires at least one source record"};
    }
    std::ranges::sort(source_records, {},
                      [](const ElectricalRecord &record) { return value_key(record.value()); });
    const auto &first = source_records.front();
    for (const auto &record : source_records) {
        if (record.semantic_key() != first.semantic_key()) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Electrical record group sources must share one semantic key"};
        }
    }

    auto effective = std::optional<ElectricalValue>{};
    auto status = ElectricalMergeStatus::Unknown;
    const auto has_unknown = std::ranges::any_of(source_records, [](const auto &record) {
        return record.value().kind() == ElectricalValueKind::Unknown;
    });
    auto evidence = std::vector<ContentHash>{};
    for (const auto &record : source_records) {
        evidence.insert(evidence.end(), record.evidence().begin(), record.evidence().end());
    }
    for (const auto &record : source_records) {
        if (record.value().kind() == ElectricalValueKind::Unknown) {
            continue;
        }
        if (!effective.has_value()) {
            effective = record.value();
            status = ElectricalMergeStatus::Effective;
            continue;
        }
        switch (record.meaning()) {
        case ElectricalMeaning::Characteristic:
            if (*effective != record.value()) {
                status = ElectricalMergeStatus::Conflict;
                effective.reset();
            }
            break;
        case ElectricalMeaning::AcceptedRange:
        case ElectricalMeaning::ProvidedRange:
        case ElectricalMeaning::AbsoluteLimit: {
            const auto intersection =
                intersect_ranges(effective->as_range(), record.value().as_range());
            if (!intersection.has_value()) {
                status = ElectricalMergeStatus::Conflict;
                effective.reset();
            } else {
                effective = ElectricalValue{*intersection};
            }
            break;
        }
        case ElectricalMeaning::Requirement:
            if (record.value().as_continuous_current().value().value() >
                effective->as_continuous_current().value().value()) {
                effective = record.value();
            }
            break;
        case ElectricalMeaning::Capability:
            if (record.value().as_continuous_current().value().value() <
                effective->as_continuous_current().value().value()) {
                effective = record.value();
            }
            break;
        }
        if (status == ElectricalMergeStatus::Conflict) {
            break;
        }
    }

    return ElectricalRecordGroup{
        first.semantic_key(),
        ElectricalRecordSelector{first.subject(), first.observable(), first.meaning()},
        first.conditions(),
        std::move(source_records),
        status,
        std::move(effective),
        has_unknown,
        normalize_evidence(std::move(evidence))};
}

ElectricalRecordGroup::ElectricalRecordGroup(ElectricalSemanticKey semantic_key,
                                             ElectricalRecordSelector selector,
                                             std::vector<ElectricalCondition> conditions,
                                             std::vector<ElectricalRecord> source_records,
                                             ElectricalMergeStatus status,
                                             std::optional<ElectricalValue> effective_value,
                                             bool has_unknown, std::vector<ContentHash> evidence)
    : semantic_key_{std::move(semantic_key)}, selector_{std::move(selector)},
      conditions_{std::move(conditions)}, source_records_{std::move(source_records)},
      status_{status}, effective_value_{std::move(effective_value)}, has_unknown_{has_unknown},
      evidence_{std::move(evidence)} {}

DiagnosticReport validate_electrical_records(const ElectricalRecordSet &records) {
    auto report = DiagnosticReport{};
    for (const auto &group : queries::electrical_record_groups(records)) {
        if (group.status() == ElectricalMergeStatus::Conflict) {
            report.add(Diagnostic{Severity::Warning, DiagnosticCode{"ELECTRICAL_RECORD_CONFLICT"},
                                  "Well-formed electrical claims conflict for semantic key " +
                                      group.semantic_key().hash().value()});
        }
        if (group.has_unknown()) {
            report.add(Diagnostic{Severity::Warning, DiagnosticCode{"ELECTRICAL_RECORD_UNKNOWN"},
                                  "Electrical record has an Unknown value for semantic key " +
                                      group.semantic_key().hash().value()});
        }
    }
    return report;
}

} // namespace volt
