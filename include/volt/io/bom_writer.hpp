#pragma once

#include <ostream>
#include <string>
#include <string_view>

#include <volt/circuit/bom.hpp>

namespace volt::io {

/** Return the canonical v1 BOM format name. */
[[nodiscard]] inline constexpr std::string_view bom_format_name() noexcept { return "volt.bom"; }

/** Return the canonical BOM format version. */
[[nodiscard]] inline constexpr int bom_format_version() noexcept { return 1; }

/** Write deterministic BOM JSON to a stream. */
void write_bom_json(std::ostream &out, const Bom &bom);

/** Return deterministic BOM JSON. */
[[nodiscard]] std::string write_bom_json(const Bom &bom);

/** Write deterministic BOM CSV to a stream. */
void write_bom_csv(std::ostream &out, const Bom &bom);

/** Return deterministic BOM CSV. */
[[nodiscard]] std::string write_bom_csv(const Bom &bom);

} // namespace volt::io
