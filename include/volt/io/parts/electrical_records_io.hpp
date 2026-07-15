#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <string_view>

#include <volt/circuit/electrical/records.hpp>

namespace volt::io {

/** Return the standalone canonical electrical-record document format name. */
[[nodiscard]] inline constexpr std::string_view electrical_records_format_name() noexcept {
    return "volt.electrical_records";
}

/** Return the canonical electrical-record document format version. */
[[nodiscard]] inline constexpr int electrical_records_format_version() noexcept { return 1; }

/** Return the semantic-model version included in electrical-record hash inputs. */
[[nodiscard]] inline constexpr int electrical_semantic_model_version() noexcept { return 1; }

/** Write deterministic canonical electrical-record JSON. */
void write_electrical_records(std::ostream &out, const ElectricalRecordSet &records);

/** Return deterministic canonical electrical-record JSON bytes. */
[[nodiscard]] std::string write_electrical_records(const ElectricalRecordSet &records);

/** Read canonical electrical-record JSON, rejecting malformed state atomically. */
[[nodiscard]] ElectricalRecordSet read_electrical_records(std::istream &input);

/** Read canonical electrical-record JSON text, rejecting malformed state atomically. */
[[nodiscard]] ElectricalRecordSet read_electrical_records_text(std::string_view text);

/** Hash the deterministic semantic-model and record bytes. */
[[nodiscard]] ContentHash electrical_records_content_hash(const ElectricalRecordSet &records);

} // namespace volt::io
