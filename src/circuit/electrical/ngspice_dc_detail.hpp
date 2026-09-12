#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <volt/electrical/ngspice_dc.hpp>

namespace volt::detail {

enum class NgspiceCurrentProjection {
    Returned,
    Resistance,
    CapacitorOpen,
    CurrentSourceNominal,
};

struct NgspiceBranchMapping {
    ElectricalBranchId branch;
    std::string device;
    std::string law;
    NgspiceCurrentProjection current_projection;
    std::string output_vector;
};

struct NgspiceDcMapping {
    std::string marker;
    std::vector<std::string> headers;
    std::vector<NgspiceBranchMapping> branches;
};

[[nodiscard]] std::string ngspice_node(const CompiledElectricalModel &model, ElectricalNodeId node);
[[nodiscard]] NgspiceDcMapping ngspice_dc_mapping(const NgspiceDcAnalysis &analysis);

/** Parse one bounded wrdata row and reconstruct all model-order DC coordinates. */
[[nodiscard]] std::vector<double> read_ngspice_dc_coordinates(const NgspiceDcAnalysis &analysis,
                                                              std::string_view output);

} // namespace volt::detail
