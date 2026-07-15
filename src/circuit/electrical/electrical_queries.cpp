#include <volt/circuit/electrical/queries.hpp>

#include <map>
#include <utility>

#include <volt/core/errors.hpp>

namespace volt::queries {

namespace {

[[nodiscard]] bool matches(const ElectricalRecord &record,
                           const ElectricalRecordSelector &selector) noexcept {
    return record.subject() == selector.subject && record.observable() == selector.observable &&
           record.meaning() == selector.meaning;
}

} // namespace

std::vector<std::reference_wrapper<const ElectricalRecord>>
electrical_records(const ElectricalRecordSet &records, const ElectricalRecordSelector &selector) {
    auto result = std::vector<std::reference_wrapper<const ElectricalRecord>>{};
    for (const auto &record : records.records()) {
        if (matches(record, selector)) {
            result.push_back(std::cref(record));
        }
    }
    return result;
}

std::vector<ElectricalRecordGroup> electrical_record_groups(const ElectricalRecordSet &records) {
    auto grouped = std::map<ElectricalSemanticKey, std::vector<ElectricalRecord>>{};
    for (const auto &record : records.records()) {
        grouped[record.semantic_key()].push_back(record);
    }
    auto result = std::vector<ElectricalRecordGroup>{};
    result.reserve(grouped.size());
    for (auto &[key, source_records] : grouped) {
        static_cast<void>(key);
        result.push_back(ElectricalRecordGroup::from(std::move(source_records)));
    }
    return result;
}

std::optional<ElectricalRecordGroup>
electrical_record_group(const ElectricalRecordSet &records,
                        const ElectricalRecordSelector &selector) {
    auto match = std::optional<ElectricalRecordGroup>{};
    for (auto group : electrical_record_groups(records)) {
        if (group.selector() != selector) {
            continue;
        }
        if (match.has_value()) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Electrical record selector matches multiple condition keys"};
        }
        match = std::move(group);
    }
    return match;
}

} // namespace volt::queries
