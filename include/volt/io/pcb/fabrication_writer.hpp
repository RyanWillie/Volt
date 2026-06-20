#pragma once

#include <ostream>
#include <string>
#include <vector>

#include <volt/core/diagnostics.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace volt::io {

/** One deterministic fabrication artifact returned by the native PCB writer. */
struct PcbFabricationFile {
    /** Stable package-local filename, such as F_Cu.gbr or PTH.drl. */
    std::string name;
    /** Complete file bytes encoded as text. */
    std::string contents;

    /** Return whether two fabrication files carry identical names and contents. */
    [[nodiscard]] friend bool operator==(const PcbFabricationFile &lhs,
                                         const PcbFabricationFile &rhs) = default;
};

/** Native fabrication package plus diagnostics for omitted or unsupported constructs. */
struct PcbFabricationPackage {
    /** Deterministically ordered Gerber and Excellon files. */
    std::vector<PcbFabricationFile> files;
    /** Stable diagnostics for missing geometry, unsupported layers, or unsupported geometry. */
    DiagnosticReport diagnostics;
};

/** Return deterministic native Gerber RS-274X and Excellon fabrication files. */
[[nodiscard]] PcbFabricationPackage
write_pcb_fabrication_package(const Board &board, const FootprintLibrary &footprints);

} // namespace volt::io
