#include <volt/io/electrical/compiled_electrical_model_io.hpp>

#include "../detail/entity_ref_format.hpp"

#include <type_traits>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/detail/typed_id.hpp>
#include <volt/io/electrical/ac_request_io.hpp>
#include <volt/io/pcb/pcb_writer.hpp>

namespace volt::io {
namespace {
using Json = nlohmann::ordered_json;

std::string coordinate(ElectricalNodeId id) { return "node:" + std::to_string(id.index()); }

std::string coordinate(ElectricalBranchId id) { return "branch:" + std::to_string(id.index()); }

Json part_json(const LibraryPartRef &part) {
    return Json{{"library_namespace", part.library_namespace()},
                {"library_version", part.library_version()},
                {"part_key", part.part_key().value()},
                {"library_digest", part.library_digest().value()},
                {"part_digest", part.part_digest().value()}};
}

Json endpoint_json(const ModelEndpoint &endpoint) {
    return std::visit(
        [&](const auto &key) {
            return Json{{"kind", std::holds_alternative<ModelTerminalKey>(endpoint)
                                     ? "terminal"
                                     : "internal_node"},
                        {"key", key.value()}};
        },
        endpoint);
}

Json parameter_json(const ModelParameter &parameter, const char *dimension) {
    const auto quantity = [dimension](const Quantity &value) {
        return Json{{"dimension", dimension}, {"si", value.value()}};
    };
    auto evidence = Json::array();
    for (const auto &key : parameter.evidence()) {
        evidence.push_back(key.value());
    }
    return Json{{"nominal", quantity(parameter.nominal())},
                {"tolerance", parameter.tolerance()
                                  ? Json{{"minus", quantity(parameter.tolerance()->minus())},
                                         {"plus", quantity(parameter.tolerance()->plus())}}
                                  : Json(nullptr)},
                {"evidence", std::move(evidence)}};
}

Json law_json(const ElectricalLaw &law) {
    return std::visit(
        [](const auto &value) {
            using Law = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<Law, DcVoltageSource> ||
                          std::same_as<Law, DcCurrentSource>) {
                constexpr bool voltage = std::same_as<Law, DcVoltageSource>;
                return Json{
                    {"kind", voltage ? "voltage_source" : "current_source"},
                    {"key", value.key().value()},
                    {"nets", Json{{"from", detail::encode_local_id(value.nets().from().id())},
                                  {"to", detail::encode_local_id(value.nets().to().id())}}},
                    {"value", Json{{"dimension", voltage ? "voltage" : "current"},
                                   {"si", value.value().value()}}}};
            } else if constexpr (std::same_as<Law, AcVoltageSource> ||
                                 std::same_as<Law, AcCurrentSource>) {
                constexpr bool voltage = std::same_as<Law, AcVoltageSource>;
                const auto phasor = value.phasor();
                return Json{
                    {"kind", voltage ? "ac_voltage_source" : "ac_current_source"},
                    {"key", value.key().value()},
                    {"nets", Json{{"from", detail::encode_local_id(value.nets().from().id())},
                                  {"to", detail::encode_local_id(value.nets().to().id())}}},
                    {"amplitude", Json{{"dimension", voltage ? "voltage" : "current"},
                                       {"si", value.amplitude().value()}}},
                    {"phase_radians", value.phase()},
                    {"phasor_si", Json{{"real", phasor.real()}, {"imaginary", phasor.imag()}}}};
            } else {
                constexpr const char *kind = [] {
                    if constexpr (std::same_as<Law, ResistanceElement>) {
                        return "resistance";
                    } else if constexpr (std::same_as<Law, CapacitanceElement>) {
                        return "capacitance";
                    } else {
                        static_assert(std::same_as<Law, InductanceElement>);
                        return "inductance";
                    }
                }();
                return Json{{"kind", kind},
                            {"key", value.key().value()},
                            {"from", endpoint_json(value.from())},
                            {"to", endpoint_json(value.to())},
                            {"parameter", parameter_json(value.parameter(), kind)}};
            }
        },
        law);
}

Json node_origin_json(const ElectricalNodeOrigin &origin, const Circuit &circuit) {
    return std::visit(
        [&](const auto &value) {
            using Origin = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<Origin, ElectricalNetOrigin>) {
                auto nets = Json::array();
                for (const auto net : value.nets) {
                    nets.push_back(detail::encode_local_id(net));
                }
                auto bindings = Json::array();
                for (const auto id : value.bindings) {
                    const auto &binding = circuit.get(id);
                    bindings.push_back(
                        Json{{"id", "port_binding:" + std::to_string(id.index())},
                             {"instance", detail::encode_local_id(binding.instance())},
                             {"port", detail::encode_local_id(binding.port())},
                             {"internal_net", detail::encode_local_id(binding.internal_net())},
                             {"parent_net", detail::encode_local_id(binding.parent_net())}});
                }
                return Json{
                    {"kind", "nets"}, {"nets", std::move(nets)}, {"bindings", std::move(bindings)}};
            } else if constexpr (std::same_as<Origin, ElectricalOpenPinOrigin>) {
                return Json{{"kind", "open_pin"},
                            {"occurrence", detail::encode_local_id(value.occurrence)},
                            {"pin", detail::encode_local_id(value.pin)},
                            {"pin_key", value.pin_key.value()},
                            {"terminal", value.terminal.value()}};
            } else {
                static_assert(std::same_as<Origin, ElectricalInternalNodeOrigin>);
                return Json{{"kind", "internal_node"},
                            {"occurrence", detail::encode_local_id(value.occurrence)},
                            {"part_digest", value.part_digest.value()},
                            {"node", value.node.value()}};
            }
        },
        origin);
}

Json model_json(const CompiledElectricalModel &model) {
    auto nodes = Json::array();
    for (const auto &node : model.nodes()) {
        auto incidence = Json::array();
        for (const auto &term : node.incidence) {
            incidence.push_back(Json{{"branch", coordinate(term.branch)}, {"sign", term.sign}});
        }
        nodes.push_back(Json{{"id", coordinate(node.id)},
                             {"origin", node_origin_json(node.origin, model.input().circuit())},
                             {"incidence", std::move(incidence)}});
    }
    auto branches = Json::array();
    for (const auto &branch : model.branches()) {
        const auto origin = std::visit(
            [](const auto &value) {
                using Origin = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<Origin, ElectricalElementOrigin>) {
                    return Json{{"kind", "model_element"},
                                {"occurrence", detail::encode_local_id(value.occurrence)},
                                {"part", part_json(value.part)},
                                {"element", value.element.value()}};
                } else {
                    return Json{{"kind", "source"}, {"key", value.value()}};
                }
            },
            branch.origin);
        branches.push_back(Json{{"id", coordinate(branch.id)},
                                {"from", coordinate(branch.from)},
                                {"to", coordinate(branch.to)},
                                {"origin", origin},
                                {"law", law_json(branch.law)}});
    }
    auto storage = Json::array();
    for (const auto &entry : model.storage()) {
        storage.push_back(std::visit(
            [](const auto &value) {
                using Storage = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<Storage, ElectricalCapacitorStorage>) {
                    return Json{{"kind", "capacitor_charge"},
                                {"branch", coordinate(value.branch)},
                                {"capacitance_si", value.capacitance.value()}};
                } else {
                    return Json{{"kind", "inductor_flux_linkage"},
                                {"branch", coordinate(value.branch)},
                                {"inductance_si", value.inductance.value()}};
                }
            },
            entry));
    }
    auto probes = Json::array();
    for (const auto &probe : model.probes()) {
        auto target = std::visit(
            [](const auto &value) {
                using Target = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<Target, ElectricalVoltageObservation>) {
                    return Json{{"kind", "voltage"},
                                {"from", coordinate(value.from)},
                                {"to", coordinate(value.to)}};
                } else {
                    return Json{{"kind", "current"}, {"branch", coordinate(value.branch)}};
                }
            },
            probe.target);
        probes.push_back(Json{{"key", probe.key.value()}, {"target", std::move(target)}});
    }
    return Json{{"format", "volt.compiled-electrical-model"},
                {"version", 1},
                {"compiler_version", CompiledElectricalModel::compiler_version()},
                {"identity", model.identity().value()},
                {"request_identity", model.request_identity().value()},
                {"request", Json::parse(model.ac_request() ? write_ac_request(*model.ac_request())
                                                           : write_dc_request(model.request()))},
                {"nodes", std::move(nodes)},
                {"branches", std::move(branches)},
                {"reference", Json{{"node", coordinate(model.reference())}, {"potential_si", 0}}},
                {"storage", std::move(storage)},
                {"probes", std::move(probes)}};
}

const char *coverage_name(DcCoverageStatus status) {
    switch (status) {
    case DcCoverageStatus::Supported:
        return "supported";
    case DcCoverageStatus::Excluded:
        return "excluded";
    case DcCoverageStatus::Unselected:
        return "unselected";
    case DcCoverageStatus::Unresolved:
        return "unresolved";
    case DcCoverageStatus::ModelAbsent:
        return "model_absent";
    case DcCoverageStatus::Unsupported:
        return "unsupported";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unknown DC coverage status"};
}

Json exclusion_json(const DcOccurrenceExclusion &exclusion) {
    return std::visit(
        [](const auto &value) {
            using Reason = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<Reason, DcNonElectricalExclusion>) {
                return Json{{"kind", "non_electrical"}};
            } else if constexpr (std::same_as<Reason, DcOutsideAnalysisExclusion>) {
                return Json{{"kind", "outside_analysis"}};
            } else {
                auto sources = Json::array();
                for (const auto &source : value.sources()) {
                    sources.push_back(source.value());
                }
                return Json{{"kind", "replaced_by_stimulus"}, {"sources", std::move(sources)}};
            }
        },
        exclusion.reason());
}
} // namespace

std::string write_compiled_electrical_model(const CompiledElectricalModel &model) {
    return model_json(model).dump(2) + "\n";
}

std::string write_electrical_compile_report(const ElectricalCompileReport &report) {
    auto coverage = Json::array();
    for (const auto &entry : report.coverage()) {
        coverage.push_back(Json{
            {"occurrence", detail::encode_local_id(entry.occurrence().id())},
            {"status", coverage_name(entry.status())},
            {"selected_part",
             entry.selected_part() ? part_json(*entry.selected_part()) : Json(nullptr)},
            {"exclusion", entry.exclusion() ? exclusion_json(*entry.exclusion()) : Json(nullptr)}});
    }
    auto diagnostics = Json::array();
    for (const auto &diagnostic : report.diagnostics()) {
        auto entities = Json::array();
        for (const auto &entity : diagnostic.entities()) {
            entities.push_back(detail::entity_ref_serialized_id(entity));
        }
        diagnostics.push_back(Json{{"severity", detail::severity_name(diagnostic.severity())},
                                   {"category", diagnostic.category().value()},
                                   {"code", diagnostic.code().value()},
                                   {"message", diagnostic.message()},
                                   {"entities", std::move(entities)}});
    }
    return Json{{"format", "volt.electrical-compile-report"},
                {"version", 1},
                {"complete", report.complete()},
                {"input", Json{{"logical", report.input().logical().value()},
                               {"selected_parts", report.input().selected_parts().value()}}},
                {"coverage", std::move(coverage)},
                {"diagnostics", std::move(diagnostics)},
                {"model", report.model() ? model_json(*report.model()) : Json(nullptr)}}
               .dump(2) +
           "\n";
}
} // namespace volt::io
