#pragma once
#include "entity_ref_format.hpp"
#include <nlohmann/json.hpp>
#include <volt/core/diagnostics.hpp>

namespace volt::io::detail {
using Json = nlohmann::json;

[[nodiscard]] inline Json report_entity_refs(std::span<const EntityRef> refs) {
    auto result = Json::array();
    for (const auto ref : refs) {
        const auto id = detail::entity_ref_serialized_id(ref);
        result.push_back({{"kind", id.substr(0, id.find(':'))}, {"index", ref.index()}});
    }
    return result;
}

[[nodiscard]] inline Json diagnostic_json(const Diagnostic &diagnostic) {
    auto overlays = Json::array();
    for (const auto &overlay : diagnostic.overlays()) {
        auto points = Json::array();
        for (const auto point : overlay.points())
            points.push_back({point.x_mm, point.y_mm});
        auto layers = Json::array();
        for (const auto layer : overlay.layers())
            layers.push_back(layer.index());
        constexpr auto names = std::array{"bounding_box", "point", "polygon", "segment"};
        overlays.push_back({{"kind", names.at(static_cast<std::size_t>(overlay.kind()))},
                            {"points", points},
                            {"entities", report_entity_refs(overlay.entities())},
                            {"layers", layers}});
    }
    auto measurement = Json{};
    if (diagnostic.measurement())
        measurement = {{"actual_mm", diagnostic.measurement()->actual_mm},
                       {"required_mm", diagnostic.measurement()->required_mm}};
    return {{"severity", diagnostic.severity() == Severity::Error     ? "error"
                         : diagnostic.severity() == Severity::Warning ? "warning"
                                                                      : "info"},
            {"category", diagnostic.category().value()},
            {"code", diagnostic.code().value()},
            {"message", diagnostic.message()},
            {"entities", report_entity_refs(diagnostic.entities())},
            {"entity_count", diagnostic.entities().size()},
            {"overlays", overlays},
            {"measurement", measurement},
            {"rule", diagnostic.rule() ? Json(*diagnostic.rule()) : Json{}}};
}

} // namespace volt::io::detail
