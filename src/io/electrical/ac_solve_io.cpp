#include <volt/io/electrical/ac_solve_io.hpp>

#include "../detail/entity_ref_format.hpp"

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/electrical/compiled_electrical_model_io.hpp>
#include <volt/io/pcb/pcb_writer.hpp>

namespace volt::io {
namespace {
using Json = nlohmann::ordered_json;

Json quantity_json(const Quantity &quantity) {
    const auto dimension = [&]() -> const char * {
        switch (quantity.dimension()) {
        case UnitDimension::Voltage:
            return "voltage";
        case UnitDimension::Current:
            return "current";
        case UnitDimension::Power:
            return "power";
        default:
            throw KernelLogicError{ErrorCode::InvalidState, "Unexpected AC result dimension"};
        }
    }();
    return Json{{"dimension", dimension}, {"si", quantity.value()}};
}

template <typename T> Json optional_number(const std::optional<T> &value) {
    return value ? Json(*value) : Json(nullptr);
}

Json optional_quantity(const std::optional<Quantity> &value) {
    return value ? quantity_json(*value) : Json(nullptr);
}

Json options_json(const AcSolveOptions &options) {
    return Json{
        {"scaling", AcSolveOptions::scaling()},
        {"relative_rank_threshold", options.relative_rank_threshold()},
        {"minimum_reciprocal_condition", options.minimum_reciprocal_condition()},
        {"relative_residual_tolerance", options.relative_residual_tolerance()},
        {"absolute_voltage_tolerance", quantity_json(options.absolute_voltage_tolerance())},
        {"absolute_current_tolerance", quantity_json(options.absolute_current_tolerance())}};
}

Json provenance_json(const AcSolveProvenance &provenance) {
    return Json{
        {"backend", provenance.backend},
        {"backend_version", provenance.backend_version},
        {"adapter", provenance.adapter},
        {"adapter_contract_version", provenance.adapter_contract_version},
        {"effective_settings", provenance.effective_settings},
        {"acceptance_policy", provenance.acceptance_policy},
        {"validation_backend", provenance.validation_backend},
        {"metrics_origin", provenance.deck_identity ? "native_validation_of_external_observations"
                                                    : "native_evaluation"},
        {"deck_identity",
         provenance.deck_identity ? Json(provenance.deck_identity->value()) : Json(nullptr)},
        {"mapping_identity",
         provenance.mapping_identity ? Json(provenance.mapping_identity->value()) : Json(nullptr)}};
}

Json complex_json(const AcComplexQuantity &quantity) {
    auto dimension = quantity.dimension() == UnitDimension::Voltage      ? "voltage"
                     : quantity.dimension() == UnitDimension::Current    ? "current"
                     : quantity.dimension() == UnitDimension::Resistance ? "resistance"
                                                                         : "ratio";
    return Json{{"dimension", dimension},
                {"si", Json{{"real", quantity.real()}, {"imaginary", quantity.imaginary()}}},
                {"magnitude", quantity.magnitude()},
                {"phase", quantity.phase() ? Json(*quantity.phase()) : Json(nullptr)}};
}

Json observations_json(const AcSolution &solution) {
    auto points = Json::array();
    for (const auto &point : solution.points()) {
        auto nodes = Json::array();
        auto branches = Json::array();
        auto probes = Json::array();
        for (const auto &node : point.nodes)
            nodes.push_back(Json{{"node", "node:" + std::to_string(node.node.index())},
                                 {"potential", complex_json(node.potential)}});
        for (const auto &branch : point.branches)
            branches.push_back(Json{{"branch", "branch:" + std::to_string(branch.branch.index())},
                                    {"voltage", complex_json(branch.voltage)},
                                    {"current", complex_json(branch.current)}});
        for (const auto &probe : point.probes)
            probes.push_back(
                Json{{"key", probe.key.value()}, {"value", complex_json(probe.value)}});
        points.push_back(Json{{"frequency_hz", point.frequency.value()},
                              {"nodes", std::move(nodes)},
                              {"branches", std::move(branches)},
                              {"probes", std::move(probes)}});
    }
    return Json{{"analysis_identity", solution.analysis_identity().value()},
                {"points", std::move(points)}};
}

Json diagnostics_json(const std::vector<Diagnostic> &values) {
    auto diagnostics = Json::array();
    for (const auto &diagnostic : values) {
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
    return diagnostics;
}

const char *outcome_name(AcSolveOutcome outcome) {
    switch (outcome) {
    case AcSolveOutcome::Success:
        return "success";
    case AcSolveOutcome::RankDeficient:
        return "rank_deficient";
    case AcSolveOutcome::Inconsistent:
        return "inconsistent";
    case AcSolveOutcome::IllConditioned:
        return "ill_conditioned";
    case AcSolveOutcome::NumericalFailure:
        return "numerical_failure";
    case AcSolveOutcome::ResidualFailure:
        return "residual_failure";
    case AcSolveOutcome::UndefinedMeasurement:
        return "undefined_measurement";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unexpected AC solve outcome"};
}
} // namespace

std::string write_ac_solution(const AcSolution &solution) {
    return Json{{"format", "volt.ac-solution"},
                {"version", 1},
                {"contract_version", AcSolveReport::contract_version()},
                {"backend", solution.provenance().backend},
                {"provenance", provenance_json(solution.provenance())},
                {"options", options_json(solution.options())},
                {"model", Json::parse(write_compiled_electrical_model(solution.model()))},
                {"observations", observations_json(solution)}}
               .dump(2) +
           "\n";
}

std::string write_ac_solve_report(const AcSolveReport &report) {
    auto points = Json::array();
    for (const auto &point : report.points()) {
        const auto &metrics = point.metrics;
        points.push_back(
            Json{{"frequency_hz", point.frequency.value()},
                 {"outcome", outcome_name(point.outcome)},
                 {"diagnostics", diagnostics_json(point.diagnostics)},
                 {"metrics",
                  Json{{"coordinate_count", metrics.coordinate_count},
                       {"rank", optional_number(metrics.rank)},
                       {"augmented_rank", optional_number(metrics.augmented_rank)},
                       {"reciprocal_condition", optional_number(metrics.reciprocal_condition)},
                       {"scaled_residual", optional_number(metrics.scaled_residual)},
                       {"voltage_residual", optional_quantity(metrics.voltage_residual)},
                       {"current_residual", optional_quantity(metrics.current_residual)},
                       {"voltage_error_ratio", optional_number(metrics.voltage_error_ratio)},
                       {"current_error_ratio", optional_number(metrics.current_error_ratio)}}}});
    }
    auto diagnostics = diagnostics_json(report.diagnostics());
    return Json{{"format", "volt.ac-solve-report"},
                {"version", 1},
                {"analysis_identity", report.analysis_identity().value()},
                {"contract_version", AcSolveReport::contract_version()},
                {"backend", report.backend()},
                {"provenance", provenance_json(report.provenance())},
                {"options", options_json(report.options())},
                {"outcome", outcome_name(report.outcome())},
                {"points", std::move(points)},
                {"diagnostics", std::move(diagnostics)},
                {"model", Json::parse(write_compiled_electrical_model(report.model()))},
                {"solution",
                 report.solution() ? observations_json(*report.solution()) : Json(nullptr)}}
               .dump(2) +
           "\n";
}
} // namespace volt::io
