#pragma once

#include <volt/electrical/ac_request.hpp>

namespace volt::detail {

/** Private topology projection; never retained as the AC model's public request. */
inline DcRequest ac_topology_request(const AcRequest &request) {
    std::vector<DcSource> sources;
    for (const auto &source : request.sources()) {
        std::visit(
            [&](const auto &value) {
                using Source = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<Source, AcVoltageSource>)
                    sources.emplace_back(DcVoltageSource{value.key(), value.nets(),
                                                         Quantity{UnitDimension::Voltage, 0.0}});
                else
                    sources.emplace_back(DcCurrentSource{value.key(), value.nets(),
                                                         Quantity{UnitDimension::Current, 0.0}});
            },
            source);
    }
    return DcRequest{request.key(),      request.input(),  request.reference(),
                     std::move(sources), request.probes(), request.exclusions()};
}
} // namespace volt::detail
