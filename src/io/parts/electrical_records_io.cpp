#include <volt/io/parts/electrical_records_io.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>

namespace volt::io {

namespace {

using Json = nlohmann::ordered_json;

[[nodiscard]] std::string observable_name(ElectricalObservable observable) {
    return observable == ElectricalObservable::Voltage ? "voltage" : "current";
}

[[nodiscard]] ElectricalObservable observable_from(const std::string &value) {
    if (value == "voltage") {
        return ElectricalObservable::Voltage;
    }
    if (value == "current") {
        return ElectricalObservable::Current;
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Electrical observable must be voltage or current"};
}

[[nodiscard]] std::string meaning_name(ElectricalMeaning meaning) {
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

[[nodiscard]] ElectricalMeaning meaning_from(const std::string &value) {
    if (value == "characteristic")
        return ElectricalMeaning::Characteristic;
    if (value == "accepted_range")
        return ElectricalMeaning::AcceptedRange;
    if (value == "provided_range")
        return ElectricalMeaning::ProvidedRange;
    if (value == "absolute_limit")
        return ElectricalMeaning::AbsoluteLimit;
    if (value == "requirement")
        return ElectricalMeaning::Requirement;
    if (value == "capability")
        return ElectricalMeaning::Capability;
    throw KernelArgumentError{ErrorCode::InvalidArgument, "Invalid electrical meaning"};
}

[[nodiscard]] std::string dimension_name(UnitDimension dimension) {
    if (dimension == UnitDimension::Voltage) {
        return "voltage";
    }
    if (dimension == UnitDimension::Current) {
        return "current";
    }
    throw KernelLogicError{ErrorCode::InvalidState,
                           "Canonical electrical records contain only Voltage or Current"};
}

[[nodiscard]] UnitDimension dimension_from(const std::string &value) {
    if (value == "voltage") {
        return UnitDimension::Voltage;
    }
    if (value == "current") {
        return UnitDimension::Current;
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Electrical value dimension must be voltage or current"};
}

[[nodiscard]] Json subject_json(const ElectricalSubject &subject) {
    auto result = Json::object();
    switch (subject.kind()) {
    case ElectricalSubjectKind::FramedPin:
        result["kind"] = "framed_pin";
        result["pin"] = subject.as_framed_pin().pin.value();
        result["reference"] = subject.as_framed_pin().reference.value();
        return result;
    case ElectricalSubjectKind::DirectedRelation:
        result["kind"] = "directed_relation";
        result["from"] = subject.as_directed_relation().from.value();
        result["to"] = subject.as_directed_relation().to.value();
        return result;
    case ElectricalSubjectKind::SupplyDomain:
        result["kind"] = "supply_domain";
        result["positive_pins"] = Json::array();
        for (const auto pin : subject.as_supply_domain().positive_pins()) {
            result["positive_pins"].push_back(pin.value());
        }
        result["return_pins"] = Json::array();
        for (const auto pin : subject.as_supply_domain().return_pins()) {
            result["return_pins"].push_back(pin.value());
        }
        return result;
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled electrical subject"};
}

[[nodiscard]] Json selector_json(const ElectricalRecordSelector &selector) {
    return Json{{"subject", subject_json(selector.subject)},
                {"observable", observable_name(selector.observable)},
                {"meaning", meaning_name(selector.meaning)}};
}

[[nodiscard]] Json expression_json(const ElectricalValueExpression &expression) {
    if (expression.is_literal()) {
        const auto &literal = expression.as_literal();
        return Json{{"kind", "literal"},
                    {"dimension", dimension_name(literal.dimension())},
                    {"value", literal.value()}};
    }
    const auto &reference = expression.as_scaled_reference();
    return Json{{"kind", "scaled_reference"},
                {"selector", selector_json(reference.selector())},
                {"scale", reference.scale()}};
}

[[nodiscard]] Json condition_json(const ElectricalCondition &condition) {
    auto predicate = Json::object();
    if (condition.predicate_kind() == ElectricalConditionPredicateKind::Equal) {
        predicate["kind"] = "equal";
        predicate["value"] = expression_json(*condition.minimum());
    } else {
        predicate["kind"] = "range";
        if (condition.minimum().has_value()) {
            predicate["minimum"] = expression_json(*condition.minimum());
        }
        if (condition.maximum().has_value()) {
            predicate["maximum"] = expression_json(*condition.maximum());
        }
    }
    return Json{{"subject", subject_json(condition.subject())},
                {"observable", observable_name(condition.observable())},
                {"predicate", std::move(predicate)}};
}

[[nodiscard]] Json value_json(const ElectricalValue &value) {
    auto result = Json::object();
    switch (value.kind()) {
    case ElectricalValueKind::Unknown:
        result["kind"] = "unknown";
        return result;
    case ElectricalValueKind::Quantity:
        result["kind"] = "quantity";
        result["dimension"] = dimension_name(value.as_quantity().dimension());
        result["value"] = value.as_quantity().value();
        return result;
    case ElectricalValueKind::CharacteristicEnvelope: {
        const auto &envelope = value.as_characteristic_envelope();
        result["kind"] = "characteristic_envelope";
        result["dimension"] = dimension_name(envelope.typical().dimension());
        result["minimum"] = envelope.minimum().value();
        result["typical"] = envelope.typical().value();
        result["maximum"] = envelope.maximum().value();
        return result;
    }
    case ElectricalValueKind::Range: {
        const auto &range = value.as_range();
        result["kind"] = "range";
        result["dimension"] = dimension_name(range.dimension());
        if (range.minimum().has_value()) {
            result["minimum"] = range.minimum()->value();
        }
        if (range.maximum().has_value()) {
            result["maximum"] = range.maximum()->value();
        }
        return result;
    }
    case ElectricalValueKind::ContinuousCurrent:
        result["kind"] = "continuous_current";
        result["dimension"] = "current";
        result["value"] = value.as_continuous_current().value().value();
        return result;
    case ElectricalValueKind::TolerancedQuantity:
        break;
    }
    throw KernelLogicError{ErrorCode::InvalidState,
                           "Toleranced electrical value escaped normalization"};
}

[[nodiscard]] Json record_json(const ElectricalRecord &record) {
    auto conditions = Json::array();
    for (const auto &condition : record.conditions()) {
        conditions.push_back(condition_json(condition));
    }
    auto evidence = Json::array();
    for (const auto &reference : record.evidence()) {
        evidence.push_back(reference.value());
    }
    return Json{{"semantic_key", record.semantic_key().hash().value()},
                {"subject", subject_json(record.subject())},
                {"observable", observable_name(record.observable())},
                {"meaning", meaning_name(record.meaning())},
                {"value", value_json(record.value())},
                {"conditions", std::move(conditions)},
                {"evidence", std::move(evidence)}};
}

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, message};
    }
}

[[nodiscard]] const nlohmann::json &field(const nlohmann::json &object, const char *name) {
    require(object.is_object(), "Expected object while reading electrical records");
    const auto it = object.find(name);
    require(it != object.end(), std::string{"Missing required electrical record field: "} + name);
    return *it;
}

[[nodiscard]] const nlohmann::json *optional_field(const nlohmann::json &object, const char *name) {
    require(object.is_object(), "Expected object while reading electrical records");
    const auto it = object.find(name);
    return it == object.end() ? nullptr : &*it;
}

[[nodiscard]] std::string string_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_string(), std::string{"Expected electrical record string field: "} + name);
    return value.get<std::string>();
}

[[nodiscard]] double number_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_number(), std::string{"Expected electrical record number field: "} + name);
    return value.get<double>();
}

[[nodiscard]] std::size_t index_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_number_unsigned(),
            std::string{"Expected electrical pin index field: "} + name);
    return value.get<std::size_t>();
}

[[nodiscard]] std::vector<ElectricalPinIndex> pin_indices(const nlohmann::json &value) {
    require(value.is_array(), "Electrical subject pin set must be an array");
    auto result = std::vector<ElectricalPinIndex>{};
    for (const auto &item : value) {
        require(item.is_number_unsigned(), "Electrical subject pin index must be unsigned");
        result.emplace_back(item.get<std::size_t>());
    }
    return result;
}

[[nodiscard]] ElectricalSubject subject_from(const nlohmann::json &value) {
    const auto kind = string_field(value, "kind");
    if (kind == "framed_pin") {
        return ElectricalSubject::framed_pin(ElectricalPinIndex{index_field(value, "pin")},
                                             ElectricalPinIndex{index_field(value, "reference")});
    }
    if (kind == "directed_relation") {
        return ElectricalSubject::directed_relation(ElectricalPinIndex{index_field(value, "from")},
                                                    ElectricalPinIndex{index_field(value, "to")});
    }
    if (kind == "supply_domain") {
        return ElectricalSubject::supply_domain(pin_indices(field(value, "positive_pins")),
                                                pin_indices(field(value, "return_pins")));
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument, "Invalid electrical subject kind"};
}

[[nodiscard]] ElectricalRecordSelector selector_from(const nlohmann::json &value) {
    return ElectricalRecordSelector{subject_from(field(value, "subject")),
                                    observable_from(string_field(value, "observable")),
                                    meaning_from(string_field(value, "meaning"))};
}

[[nodiscard]] ElectricalValueExpression expression_from(const nlohmann::json &value) {
    const auto kind = string_field(value, "kind");
    if (kind == "literal") {
        return ElectricalValueExpression::literal(Quantity{
            dimension_from(string_field(value, "dimension")), number_field(value, "value")});
    }
    if (kind == "scaled_reference") {
        return ElectricalValueExpression::scaled_reference(selector_from(field(value, "selector")),
                                                           number_field(value, "scale"));
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Invalid electrical condition expression kind"};
}

[[nodiscard]] ElectricalCondition condition_from(const nlohmann::json &value) {
    auto subject = subject_from(field(value, "subject"));
    const auto observable = observable_from(string_field(value, "observable"));
    const auto &predicate = field(value, "predicate");
    const auto kind = string_field(predicate, "kind");
    if (kind == "equal") {
        return ElectricalCondition::equal(std::move(subject), observable,
                                          expression_from(field(predicate, "value")));
    }
    if (kind == "range") {
        const auto *minimum = optional_field(predicate, "minimum");
        const auto *maximum = optional_field(predicate, "maximum");
        return ElectricalCondition::range(
            std::move(subject), observable,
            minimum == nullptr ? std::nullopt : std::optional{expression_from(*minimum)},
            maximum == nullptr ? std::nullopt : std::optional{expression_from(*maximum)});
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Invalid electrical condition predicate kind"};
}

[[nodiscard]] ElectricalValue value_from(const nlohmann::json &value) {
    const auto kind = string_field(value, "kind");
    if (kind == "unknown") {
        return ElectricalValue{UnknownElectricalValue{}};
    }
    const auto dimension = dimension_from(string_field(value, "dimension"));
    if (kind == "quantity") {
        return ElectricalValue{Quantity{dimension, number_field(value, "value")}};
    }
    if (kind == "characteristic_envelope") {
        return ElectricalValue{
            CharacteristicEnvelope{Quantity{dimension, number_field(value, "minimum")},
                                   Quantity{dimension, number_field(value, "typical")},
                                   Quantity{dimension, number_field(value, "maximum")}}};
    }
    if (kind == "range") {
        const auto *minimum = optional_field(value, "minimum");
        const auto *maximum = optional_field(value, "maximum");
        require(minimum != nullptr || maximum != nullptr,
                "Electrical quantity range must contain at least one bound");
        if (minimum != nullptr && maximum != nullptr) {
            require(minimum->is_number() && maximum->is_number(),
                    "Electrical range bounds must be numbers");
            return ElectricalValue{
                QuantityRange::bounded(Quantity{dimension, minimum->get<double>()},
                                       Quantity{dimension, maximum->get<double>()})};
        }
        if (minimum != nullptr) {
            require(minimum->is_number(), "Electrical range minimum must be a number");
            return ElectricalValue{
                QuantityRange::minimum(Quantity{dimension, minimum->get<double>()})};
        }
        require(maximum->is_number(), "Electrical range maximum must be a number");
        return ElectricalValue{QuantityRange::maximum(Quantity{dimension, maximum->get<double>()})};
    }
    if (kind == "continuous_current") {
        require(dimension == UnitDimension::Current,
                "Continuous Current value must use the current dimension");
        return ElectricalValue{
            ContinuousCurrent{Quantity{dimension, number_field(value, "value")}}};
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument, "Invalid electrical value kind"};
}

struct ParsedRecord {
    ElectricalSemanticKey expected_key;
    ElectricalRecordSpec spec;
};

[[nodiscard]] ParsedRecord record_from(const nlohmann::json &value) {
    auto conditions = std::vector<ElectricalCondition>{};
    const auto &condition_values = field(value, "conditions");
    require(condition_values.is_array(), "Electrical record conditions must be an array");
    for (const auto &condition : condition_values) {
        conditions.push_back(condition_from(condition));
    }
    auto evidence = std::vector<ContentHash>{};
    const auto &evidence_values = field(value, "evidence");
    require(evidence_values.is_array(), "Electrical record evidence must be an array");
    for (const auto &reference : evidence_values) {
        require(reference.is_string(), "Electrical evidence reference must be a content hash");
        evidence.emplace_back(reference.get<std::string>());
    }
    return ParsedRecord{ElectricalSemanticKey{ContentHash{string_field(value, "semantic_key")}},
                        ElectricalRecordSpec{subject_from(field(value, "subject")),
                                             observable_from(string_field(value, "observable")),
                                             meaning_from(string_field(value, "meaning")),
                                             value_from(field(value, "value")),
                                             std::move(conditions), std::move(evidence)}};
}

[[nodiscard]] ElectricalRecordSet document_from(const nlohmann::json &document) {
    require(document.is_object(), "Electrical record document must be an object");
    require(string_field(document, "format") == electrical_records_format_name(),
            "Unsupported electrical record document format");
    const auto &version = field(document, "version");
    require(version.is_number_integer() &&
                version.get<std::int64_t>() == electrical_records_format_version(),
            "Unsupported electrical record document version");
    const auto &semantic_model_version = field(document, "semantic_model_version");
    require(semantic_model_version.is_number_integer() &&
                semantic_model_version.get<std::int64_t>() == electrical_semantic_model_version(),
            "Unsupported electrical semantic model version");
    const auto &pin_count = field(document, "pin_count");
    require(pin_count.is_number_unsigned(), "Electrical record pin_count must be unsigned");
    const auto &record_values = field(document, "records");
    require(record_values.is_array(), "Electrical records must be an array");

    auto specs = std::vector<ElectricalRecordSpec>{};
    specs.reserve(record_values.size());
    for (const auto &value : record_values) {
        auto parsed = record_from(value);
        if (electrical_semantic_key(parsed.spec) != parsed.expected_key) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Electrical record semantic key does not match its meaning"};
        }
        specs.push_back(std::move(parsed.spec));
    }
    return ElectricalRecordSet{pin_count.get<std::size_t>(), std::move(specs)};
}

} // namespace

void write_electrical_records(std::ostream &out, const ElectricalRecordSet &records) {
    auto record_values = Json::array();
    for (const auto &record : records.records()) {
        record_values.push_back(record_json(record));
    }
    const auto document = Json{{"format", electrical_records_format_name()},
                               {"version", electrical_records_format_version()},
                               {"semantic_model_version", electrical_semantic_model_version()},
                               {"pin_count", records.pin_count()},
                               {"records", std::move(record_values)}};
    out << document.dump(2) << '\n';
}

std::string write_electrical_records(const ElectricalRecordSet &records) {
    auto out = std::ostringstream{};
    write_electrical_records(out, records);
    return out.str();
}

ElectricalRecordSet read_electrical_records(std::istream &input) {
    return document_from(nlohmann::json::parse(input));
}

ElectricalRecordSet read_electrical_records_text(std::string_view text) {
    return document_from(nlohmann::json::parse(text.begin(), text.end()));
}

ContentHash electrical_records_content_hash(const ElectricalRecordSet &records) {
    return sha256_content_hash(write_electrical_records(records));
}

} // namespace volt::io
