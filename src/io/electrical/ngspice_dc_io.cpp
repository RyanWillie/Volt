#include <volt/io/electrical/ngspice_dc_io.hpp>

#include "../../circuit/electrical/ngspice_dc_detail.hpp"
#include "../detail/entity_ref_format.hpp"

#include <string>
#include <type_traits>
#include <variant>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/pcb/pcb_writer.hpp>

namespace volt::io {
namespace {

using Json = nlohmann::ordered_json;

[[nodiscard]] const char *projection_name(volt::detail::NgspiceCurrentProjection projection) {
    switch (projection) {
    case volt::detail::NgspiceCurrentProjection::Returned:
        return "returned";
    case volt::detail::NgspiceCurrentProjection::Resistance:
        return "derived_resistance";
    case volt::detail::NgspiceCurrentProjection::CapacitorOpen:
        return "exact_capacitor_open";
    case volt::detail::NgspiceCurrentProjection::CurrentSourceNominal:
        return "independent_source_nominal";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Invalid ngspice DC current projection"};
}

[[nodiscard]] Json origin_json(const ElectricalBranchOrigin &origin) {
    return std::visit(
        [](const auto &value) {
            using Origin = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<Origin, ElectricalElementOrigin>) {
                return Json{{"kind", "model_element"},
                            {"occurrence", "component:" + std::to_string(value.occurrence.index())},
                            {"part_digest", value.part.part_digest().value()},
                            {"element", value.element.value()}};
            } else {
                static_assert(std::same_as<Origin, DcSourceKey>);
                return Json{{"kind", "request_source"}, {"key", value.value()}};
            }
        },
        origin);
}

[[nodiscard]] Json diagnostics_json(const std::vector<Diagnostic> &diagnostics) {
    auto result = Json::array();
    for (const auto &diagnostic : diagnostics) {
        auto entities = Json::array();
        for (const auto &entity : diagnostic.entities()) {
            entities.push_back(detail::entity_ref_serialized_id(entity));
        }
        result.push_back(Json{{"severity", detail::severity_name(diagnostic.severity())},
                              {"category", diagnostic.category().value()},
                              {"code", diagnostic.code().value()},
                              {"message", diagnostic.message()},
                              {"entities", std::move(entities)}});
    }
    return result;
}

} // namespace

std::string write_ngspice_dc_analysis(const NgspiceDcAnalysis &analysis) {
    const auto mapping = volt::detail::ngspice_dc_mapping(analysis);
    auto nodes = Json::array();
    for (const auto &node : analysis.model().nodes()) {
        nodes.push_back(
            Json{{"node", "node:" + std::to_string(node.id.index())},
                 {"spice_node", volt::detail::ngspice_node(analysis.model(), node.id)},
                 {"reference", node.id == analysis.model().reference()},
                 {"output_vector", node.id == analysis.model().reference()
                                       ? Json(nullptr)
                                       : Json("v(n" + std::to_string(node.id.index()) + ")")}});
    }
    auto branches = Json::array();
    for (const auto &entry : mapping.branches) {
        const auto &branch = analysis.model().branches().at(entry.branch.index());
        branches.push_back(
            Json{{"branch", "branch:" + std::to_string(entry.branch.index())},
                 {"device", entry.device},
                 {"law", entry.law},
                 {"from", volt::detail::ngspice_node(analysis.model(), branch.from)},
                 {"to", volt::detail::ngspice_node(analysis.model(), branch.to)},
                 {"origin", origin_json(branch.origin)},
                 {"current_projection", projection_name(entry.current_projection)},
                 {"output_vector",
                  entry.output_vector.empty() ? Json(nullptr) : Json(entry.output_vector)}});
    }
    auto headers = Json::array();
    for (const auto &header : mapping.headers) {
        headers.push_back(header);
    }
    return Json{
               {"format", "volt.ngspice-dc-analysis"},
               {"version", 1},
               {"complete", analysis.complete()},
               {"backend", Json{{"name", NgspiceDcAnalysis::backend()},
                                {"version", NgspiceDcAnalysis::backend_version()},
                                {"adapter_contract_version", NgspiceDcAnalysis::contract_version()},
                                {"settings", NgspiceDcAnalysis::settings()}}},
               {"model_identity", analysis.model().identity().value()},
               {"request_identity", analysis.model().request_identity().value()},
               {"deck_identity", analysis.deck_identity().value()},
               {"projection", Json{{"kind", "exact_dc"}, {"losses", Json::array()}}},
               {"mapping",
                Json{{"identity", analysis.mapping_identity().value()},
                     {"marker", mapping.marker},
                     {"nodes", std::move(nodes)},
                     {"branches", std::move(branches)},
                     {"output", Json{{"filename", NgspiceDcAnalysis::output_filename()},
                                     {"maximum_bytes", NgspiceDcAnalysis::maximum_output_bytes()},
                                     {"headers", std::move(headers)},
                                     {"operating_points", 1}}}}},
               {"diagnostics", diagnostics_json(analysis.diagnostics())}}
               .dump(2) +
           "\n";
}

} // namespace volt::io
