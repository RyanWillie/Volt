#pragma once

#include <string>

#include <volt/electrical/ngspice_dc.hpp>

namespace volt::io {

/** Write the exact native capability, identity and coordinate-mapping report. */
[[nodiscard]] std::string write_ngspice_dc_analysis(const NgspiceDcAnalysis &analysis);

} // namespace volt::io
