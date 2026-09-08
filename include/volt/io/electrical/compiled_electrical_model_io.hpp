#pragma once

#include <string>

#include <volt/electrical/compiled_electrical_model.hpp>

namespace volt::io {

/** Write the complete native graph/law snapshot for deterministic inspection, not restoration. */
[[nodiscard]] std::string write_compiled_electrical_model(const CompiledElectricalModel &model);

/** Write coverage, diagnostics and the optional complete model; never a partial runnable graph. */
[[nodiscard]] std::string write_electrical_compile_report(const ElectricalCompileReport &report);

} // namespace volt::io
