#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <volt/circuit/electrical/records.hpp>

namespace volt::queries {

/** Return source records matching one typed subject/observable/meaning selector. */
[[nodiscard]] std::vector<std::reference_wrapper<const ElectricalRecord>>
electrical_records(const ElectricalRecordSet &records, const ElectricalRecordSelector &selector);

/** Return every semantic-key group with its deterministic effective merge state. */
[[nodiscard]] std::vector<ElectricalRecordGroup>
electrical_record_groups(const ElectricalRecordSet &records);

/** Return the sole semantic-key group for a selector, or no value when absent. */
[[nodiscard]] std::optional<ElectricalRecordGroup>
electrical_record_group(const ElectricalRecordSet &records,
                        const ElectricalRecordSelector &selector);

} // namespace volt::queries
