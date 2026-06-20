#pragma once

#include <ostream>
#include <string>
#include <string_view>

#include <volt/pcb/assembly/cpl.hpp>

namespace volt::io {

/** Return the canonical v1 CPL format name. */
[[nodiscard]] inline constexpr std::string_view cpl_format_name() noexcept { return "volt.cpl"; }

/** Return the canonical CPL format version. */
[[nodiscard]] inline constexpr int cpl_format_version() noexcept { return 1; }

/** Write deterministic canonical CPL JSON to a stream. */
void write_cpl_json(std::ostream &out, const Cpl &cpl);

/** Return deterministic canonical CPL JSON. */
[[nodiscard]] std::string write_cpl_json(const Cpl &cpl);

/** Write deterministic JLCPCB-shaped CPL CSV to a stream. */
void write_cpl_csv(std::ostream &out, const Cpl &cpl);

/** Return deterministic JLCPCB-shaped CPL CSV. */
[[nodiscard]] std::string write_cpl_csv(const Cpl &cpl);

} // namespace volt::io
