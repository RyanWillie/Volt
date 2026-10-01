#include <volt/electrical/dc_solve.hpp>
#include <volt/electrical/ngspice_dc.hpp>

#include "ngspice_dc_detail.hpp"
#include "nonlinear_dc_detail.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <Eigen/Dense>

#include <volt/core/errors.hpp>

namespace volt {
namespace {

using Matrix = Eigen::MatrixXd;
using Vector = Eigen::VectorXd;

static_assert(EIGEN_MAJOR_VERSION == 5 && EIGEN_MINOR_VERSION == 0 && EIGEN_PATCH_VERSION == 0,
              "DC solver backend identity requires Eigen 5.0.0 exactly");

constexpr auto backend_name =
    std::string_view{"eigen-5.0.0-full-piv-lu-double-dense-branch-tableau"};

[[nodiscard]] std::string number_text(double value) {
    auto buffer = std::array<char, 64>{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                      std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) {
        throw KernelLogicError{ErrorCode::InvalidState, "Failed to encode DC solver number"};
    }
    return std::string{buffer.data(), result.ptr};
}

class IdentityEncoder final {
  public:
    void text(std::string_view value) {
        bytes_ += std::to_string(value.size());
        bytes_ += ':';
        bytes_.append(value);
        bytes_ += '\n';
    }

    void number(double value) { text(number_text(value)); }

    [[nodiscard]] ContentHash digest() const { return sha256_content_hash(bytes_); }

  private:
    std::string bytes_;
};

[[nodiscard]] ContentHash
make_analysis_identity(const CompiledElectricalModel &model, const DcSolveOptions &options,
                       const DcSolveProvenance &provenance,
                       const NonlinearDcSolveOptions *nonlinear = nullptr) {
    if (model.ac_request() != nullptr || model.transient_request() != nullptr) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Non-DC compiled model cannot be solved as DC"};
    }
    auto encoder = IdentityEncoder{};
    encoder.text("volt.dc-analysis");
    encoder.text(nonlinear ? NonlinearDcSolveOptions::algorithm() : "linear");
    encoder.text(std::to_string(DcSolveReport::contract_version()));
    encoder.text(model.identity().value());
    encoder.text(provenance.backend);
    encoder.text(DcSolveOptions::scaling());
    encoder.number(options.relative_rank_threshold());
    encoder.number(options.minimum_reciprocal_condition());
    encoder.number(options.relative_residual_tolerance());
    encoder.number(options.absolute_voltage_tolerance().value());
    encoder.number(options.absolute_current_tolerance().value());
    encoder.text(provenance.backend_version);
    encoder.text(provenance.adapter);
    encoder.text(std::to_string(provenance.adapter_contract_version));
    encoder.text(provenance.effective_settings);
    encoder.text(provenance.acceptance_policy);
    encoder.text(provenance.validation_backend);
    encoder.text(provenance.deck_identity ? provenance.deck_identity->value() : "absent");
    encoder.text(provenance.mapping_identity ? provenance.mapping_identity->value() : "absent");
    if (nonlinear) {
        encoder.text(NonlinearDcSolveOptions::initial_guess());
        encoder.number(NonlinearDcSolveOptions::armijo_coefficient());
        encoder.number(NonlinearDcSolveOptions::minimum_step());
        encoder.text("base-original-terms-frozen-stable-l2");
        encoder.text("original-laws-and-undamped-coordinate-correction");
        encoder.text(std::to_string(nonlinear->max_iterations()));
        encoder.text(std::to_string(nonlinear->max_backtracks()));
        encoder.text(std::to_string(nonlinear->max_residual_evaluations()));
        encoder.text(std::to_string(nonlinear->max_jacobian_evaluations()));
    }
    return encoder.digest();
}

[[nodiscard]] DcSolveProvenance native_provenance() {
    return DcSolveProvenance{std::string{backend_name},
                             "5.0.0",
                             "volt.native-linear-dc",
                             1,
                             std::string{DcSolveOptions::scaling()},
                             "volt.linear-dc-unique-finite-residual:1",
                             std::string{backend_name},
                             std::nullopt,
                             std::nullopt};
}

[[nodiscard]] DcSolveProvenance nonlinear_provenance() {
    auto result = native_provenance();
    result.adapter = "volt.native-diode-newton-dc";
    result.adapter_contract_version = 2;
    result.effective_settings =
        "all-zero;max_abs_row_then_column;frozen-original-term-l2;armijo=0.0001;alpha-min=2^-24";
    result.acceptance_policy =
        "volt.nonlinear-dc-local-regular-finite-original-residual-undamped-correction:2";
    return result;
}

[[nodiscard]] DcSolveProvenance ngspice_provenance(const NgspiceDcAnalysis &analysis) {
    return DcSolveProvenance{"ngspice",
                             "46",
                             "volt.ngspice-dc",
                             NgspiceDcAnalysis::contract_version(),
                             std::string{NgspiceDcAnalysis::settings()},
                             "volt.linear-dc-unique-finite-residual:1",
                             std::string{backend_name},
                             analysis.deck_identity(),
                             analysis.mapping_identity()};
}

[[nodiscard]] Diagnostic solve_diagnostic(std::string_view code, std::string message,
                                          std::vector<EntityRef> entities = {}) {
    return Diagnostic{Severity::Error, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Analysis}, std::move(message),
                      std::move(entities)};
}

void append_entity(std::vector<EntityRef> &entities, EntityRef entity) {
    if (std::ranges::find(entities, entity) == entities.end()) {
        entities.push_back(entity);
    }
}

void append_node_entities(const CompiledElectricalModel &model, ElectricalNodeId node,
                          std::vector<EntityRef> &entities) {
    std::visit(
        [&](const auto &origin) {
            using Origin = std::decay_t<decltype(origin)>;
            if constexpr (std::same_as<Origin, ElectricalNetOrigin>) {
                for (const auto net : origin.nets) {
                    append_entity(entities, EntityRef::net(net));
                }
            } else if constexpr (std::same_as<Origin, ElectricalOpenPinOrigin>) {
                append_entity(entities, EntityRef::component(origin.occurrence));
                append_entity(entities, EntityRef::pin(origin.pin));
            } else {
                append_entity(entities, EntityRef::component(origin.occurrence));
            }
        },
        model.nodes().at(node.index()).origin);
}

[[nodiscard]] std::vector<EntityRef> branch_entities(const CompiledElectricalModel &model,
                                                     const ElectricalBranch &branch) {
    auto entities = std::vector<EntityRef>{};
    if (const auto *origin = std::get_if<ElectricalElementOrigin>(&branch.origin)) {
        append_entity(entities, EntityRef::component(origin->occurrence));
    }
    append_node_entities(model, branch.from, entities);
    append_node_entities(model, branch.to, entities);
    return entities;
}

[[nodiscard]] std::string branch_scope(const ElectricalBranch &branch) {
    return std::visit(
        [&](const auto &origin) {
            using Origin = std::decay_t<decltype(origin)>;
            if constexpr (std::same_as<Origin, ElectricalElementOrigin>) {
                return "branch:" + std::to_string(branch.id.index()) + " model element '" +
                       origin.element.value() +
                       "' on component:" + std::to_string(origin.occurrence.index());
            } else {
                return "branch:" + std::to_string(branch.id.index()) + " request source '" +
                       origin.value() + "'";
            }
        },
        branch.origin);
}

[[nodiscard]] bool finite(double value) { return std::isfinite(value); }

[[nodiscard]] std::optional<double> checked_difference(double lhs, double rhs) {
    const auto value = static_cast<long double>(lhs) - static_cast<long double>(rhs);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<double>::max()) {
        return std::nullopt;
    }
    return static_cast<double>(value);
}

[[nodiscard]] std::optional<double> checked_product(double lhs, double rhs) {
    const auto value = static_cast<long double>(lhs) * static_cast<long double>(rhs);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<double>::max()) {
        return std::nullopt;
    }
    return static_cast<double>(value);
}

[[nodiscard]] double max_abs(const Vector &value) {
    return value.size() == 0 ? 0.0 : value.cwiseAbs().maxCoeff();
}

[[nodiscard]] double matrix_infinity_norm(const Matrix &value) {
    if (value.rows() == 0 || value.cols() == 0) {
        return 0.0;
    }
    return value.cwiseAbs().rowwise().sum().maxCoeff();
}

struct Tableau {
    Matrix coefficients;
    Vector right_hand_side;
    std::vector<std::optional<std::size_t>> node_columns;
    std::size_t node_coordinate_count;
};

[[nodiscard]] std::optional<Tableau> assemble_tableau(const CompiledElectricalModel &model,
                                                      std::vector<Diagnostic> &diagnostics) {
    const auto node_count = model.nodes().size();
    const auto branch_count = model.branches().size();
    if (node_count == 0U || model.reference().index() >= node_count ||
        node_count - 1U > std::numeric_limits<std::size_t>::max() - branch_count) {
        diagnostics.push_back(
            solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                             "DC solver cannot construct coordinates for the compiled model"));
        return std::nullopt;
    }
    const auto node_coordinates = node_count - 1U;
    const auto coordinate_count = node_coordinates + branch_count;
    if (coordinate_count > static_cast<std::size_t>(std::numeric_limits<Eigen::Index>::max())) {
        diagnostics.push_back(
            solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                             "DC solver coordinate count exceeds the numerical backend limit"));
        return std::nullopt;
    }

    const auto dimension = static_cast<Eigen::Index>(coordinate_count);
    auto result = Tableau{Matrix::Zero(dimension, dimension), Vector::Zero(dimension),
                          std::vector<std::optional<std::size_t>>(node_count), node_coordinates};
    auto next_node_column = std::size_t{0};
    for (const auto &node : model.nodes()) {
        if (node.id != model.reference()) {
            result.node_columns.at(node.id.index()) = next_node_column++;
        }
    }

    const auto add_node_coefficient = [&](Eigen::Index row, ElectricalNodeId node, double value) {
        if (const auto column = result.node_columns.at(node.index())) {
            result.coefficients(row, static_cast<Eigen::Index>(*column)) += value;
        }
    };

    for (const auto &branch : model.branches()) {
        const auto current_column = static_cast<Eigen::Index>(node_coordinates + branch.id.index());
        if (const auto row = result.node_columns.at(branch.from.index())) {
            result.coefficients(static_cast<Eigen::Index>(*row), current_column) += 1.0;
        }
        if (const auto row = result.node_columns.at(branch.to.index())) {
            result.coefficients(static_cast<Eigen::Index>(*row), current_column) -= 1.0;
        }

        const auto law_row = static_cast<Eigen::Index>(node_coordinates + branch.id.index());
        std::visit(
            [&](const auto &law) {
                using Law = std::decay_t<decltype(law)>;
                if constexpr (std::same_as<Law, ResistanceElement>) {
                    add_node_coefficient(law_row, branch.from, 1.0);
                    add_node_coefficient(law_row, branch.to, -1.0);
                    result.coefficients(law_row, current_column) =
                        -law.parameter().nominal().value();
                } else if constexpr (std::same_as<Law, CapacitanceElement>) {
                    result.coefficients(law_row, current_column) = 1.0;
                } else if constexpr (std::same_as<Law, InductanceElement>) {
                    add_node_coefficient(law_row, branch.from, 1.0);
                    add_node_coefficient(law_row, branch.to, -1.0);
                } else if constexpr (std::same_as<Law, ShockleyDiodeElement>) {
                    result.coefficients(law_row, current_column) = 1.0;
                } else if constexpr (std::same_as<Law, DcVoltageSource>) {
                    add_node_coefficient(law_row, branch.from, 1.0);
                    add_node_coefficient(law_row, branch.to, -1.0);
                    result.right_hand_side(law_row) = law.value().value();
                } else if constexpr (std::same_as<Law, DcCurrentSource>) {
                    result.coefficients(law_row, current_column) = 1.0;
                    result.right_hand_side(law_row) = law.value().value();
                } else {
                    throw KernelArgumentError{ErrorCode::InvalidArgument,
                                              "AC source cannot enter the DC tableau"};
                }
            },
            branch.law);
    }

    if (!result.coefficients.allFinite() || !result.right_hand_side.allFinite()) {
        diagnostics.push_back(solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                                               "DC solver assembled a nonfinite branch tableau"));
        return std::nullopt;
    }
    return result;
}

struct EquilibratedSystem {
    Matrix coefficients;
    Vector right_hand_side;
    Vector column_maxima;
};

[[nodiscard]] std::optional<EquilibratedSystem> equilibrate(const Tableau &tableau,
                                                            std::vector<Diagnostic> &diagnostics) {
    auto coefficients = tableau.coefficients;
    auto right_hand_side = tableau.right_hand_side;
    for (Eigen::Index row = 0; row < coefficients.rows(); ++row) {
        const double maximum =
            coefficients.cols() == 0 ? 0.0 : coefficients.row(row).cwiseAbs().maxCoeff();
        if (maximum > 0.0) {
            coefficients.row(row) /= maximum;
            right_hand_side(row) /= maximum;
        }
    }
    Vector column_maxima = Vector::Zero(coefficients.cols());
    for (Eigen::Index column = 0; column < coefficients.cols(); ++column) {
        const double maximum =
            coefficients.rows() == 0 ? 0.0 : coefficients.col(column).cwiseAbs().maxCoeff();
        column_maxima.coeffRef(column) = maximum;
        if (maximum > 0.0) {
            coefficients.col(column) /= maximum;
        }
    }
    if (!coefficients.allFinite() || !right_hand_side.allFinite() || !column_maxima.allFinite()) {
        diagnostics.push_back(
            solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                             "DC solver equilibration produced a nonfinite value"));
        return std::nullopt;
    }
    return EquilibratedSystem{std::move(coefficients), std::move(right_hand_side),
                              std::move(column_maxima)};
}

struct ResidualTracker {
    const DcSolveOptions &options;
    DcSolveMetrics &metrics;
    std::vector<Diagnostic> &diagnostics;
    bool failed = false;
    double max_voltage_residual = 0.0;
    double max_current_residual = 0.0;
    double max_voltage_ratio = 0.0;
    double max_current_ratio = 0.0;

    [[nodiscard]] bool record_voltage(double residual, double scale, std::string message,
                                      std::vector<EntityRef> entities = {}) {
        return record(residual, scale, options.absolute_voltage_tolerance().value(),
                      max_voltage_residual, max_voltage_ratio, std::move(message),
                      std::move(entities));
    }

    [[nodiscard]] bool record_current(double residual, double scale, std::string message,
                                      std::vector<EntityRef> entities = {}) {
        return record(residual, scale, options.absolute_current_tolerance().value(),
                      max_current_residual, max_current_ratio, std::move(message),
                      std::move(entities));
    }

    void publish() {
        metrics.voltage_residual = Quantity{UnitDimension::Voltage, max_voltage_residual};
        metrics.current_residual = Quantity{UnitDimension::Current, max_current_residual};
        metrics.voltage_error_ratio = max_voltage_ratio;
        metrics.current_error_ratio = max_current_ratio;
    }

  private:
    [[nodiscard]] bool record(double residual, double scale, double absolute_tolerance,
                              double &maximum_residual, double &maximum_ratio, std::string message,
                              std::vector<EntityRef> entities) {
        if (!finite(residual) || !finite(scale) || residual < 0.0 || scale < 0.0) {
            return false;
        }
        const auto allowed = static_cast<long double>(absolute_tolerance) +
                             static_cast<long double>(options.relative_residual_tolerance()) *
                                 static_cast<long double>(scale);
        const auto ratio = static_cast<long double>(residual) / allowed;
        if (!std::isfinite(allowed) || !std::isfinite(ratio) ||
            ratio > std::numeric_limits<double>::max()) {
            return false;
        }
        maximum_residual = std::max(maximum_residual, residual);
        maximum_ratio = std::max(maximum_ratio, static_cast<double>(ratio));
        if (ratio > 1.0L) {
            diagnostics.push_back(
                solve_diagnostic(analysis_diagnostic_codes::DcSolveResidualFailure,
                                 std::move(message), std::move(entities)));
            failed = true;
        }
        return true;
    }
};

} // namespace

DcSolveOptions::DcSolveOptions(double relative_rank_threshold, double minimum_reciprocal_condition,
                               double relative_residual_tolerance,
                               Quantity absolute_voltage_tolerance,
                               Quantity absolute_current_tolerance)
    : rank_threshold_{relative_rank_threshold}, minimum_rcond_{minimum_reciprocal_condition},
      relative_residual_{relative_residual_tolerance},
      voltage_tolerance_{absolute_voltage_tolerance},
      current_tolerance_{absolute_current_tolerance} {
    const auto valid_relative = [](double value) {
        return std::isfinite(value) && value > 0.0 && value < 1.0;
    };
    if (!valid_relative(rank_threshold_) || !valid_relative(minimum_rcond_) ||
        !valid_relative(relative_residual_) ||
        voltage_tolerance_.dimension() != UnitDimension::Voltage ||
        current_tolerance_.dimension() != UnitDimension::Current ||
        voltage_tolerance_.value() <= 0.0 || current_tolerance_.value() <= 0.0) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "DC solve options require positive finite typed tolerances; "
                                  "relative settings must be below one"};
    }
}

NonlinearDcSolveOptions::NonlinearDcSolveOptions(DcSolveOptions acceptance,
                                                 std::size_t max_iterations,
                                                 std::size_t max_backtracks,
                                                 std::size_t max_residual_evaluations,
                                                 std::size_t max_jacobian_evaluations)
    : acceptance_{acceptance}, max_iterations_{max_iterations}, max_backtracks_{max_backtracks},
      max_residual_evaluations_{max_residual_evaluations},
      max_jacobian_evaluations_{max_jacobian_evaluations} {
    if (max_iterations == 0 || max_backtracks == 0 || max_backtracks > 24 ||
        max_residual_evaluations == 0 || max_jacobian_evaluations == 0) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Nonlinear DC requires positive work budgets and at most 24 backtracks"};
    }
}

DcSolution::DcSolution(ContentHash analysis_identity, CompiledElectricalModel model,
                       DcSolveOptions options, DcSolveProvenance provenance,
                       std::vector<DcNodeResult> nodes, std::vector<DcBranchResult> branches,
                       std::vector<DcProbeResult> probes,
                       std::optional<NonlinearDcSolveOptions> nonlinear_options)
    : analysis_identity_{std::move(analysis_identity)}, model_{std::move(model)}, options_{options},
      nonlinear_options_{std::move(nonlinear_options)}, provenance_{std::move(provenance)},
      nodes_{std::move(nodes)}, branches_{std::move(branches)}, probes_{std::move(probes)} {}

class DcSolution::Solver final {
  public:
    [[nodiscard]] static std::optional<DcSolution>
    solve(const CompiledElectricalModel &model, const DcSolveOptions &options,
          const ContentHash &identity, const DcSolveProvenance &provenance, DcSolveOutcome &outcome,
          DcSolveMetrics &metrics, std::vector<Diagnostic> &diagnostics,
          const std::vector<double> *external_coordinates = nullptr) {
        for (const auto &branch : model.branches()) {
            if (std::holds_alternative<ShockleyDiodeElement>(branch.law)) {
                outcome = DcSolveOutcome::UnsupportedModel;
                diagnostics.push_back(
                    solve_diagnostic(analysis_diagnostic_codes::DcSolveUnsupportedModel,
                                     "Linear DC requires explicit nonlinear method selection for " +
                                         branch_scope(branch),
                                     branch_entities(model, branch)));
                return std::nullopt;
            }
        }
        const auto tableau = assemble_tableau(model, diagnostics);
        if (!tableau) {
            outcome = DcSolveOutcome::NumericalFailure;
            return std::nullopt;
        }
        metrics.coordinate_count = static_cast<std::size_t>(tableau->coefficients.cols());
        if (external_coordinates != nullptr &&
            (external_coordinates->size() != metrics.coordinate_count ||
             !std::ranges::all_of(*external_coordinates, finite))) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "External DC output has invalid coordinates"};
        }
        const auto system = equilibrate(*tableau, diagnostics);
        if (!system) {
            outcome = DcSolveOutcome::NumericalFailure;
            return std::nullopt;
        }

        auto scaled_solution = Vector{};
        if (system->coefficients.rows() == 0) {
            metrics.rank = 0U;
            metrics.reciprocal_condition = 1.0;
            metrics.scaled_residual = 0.0;
            scaled_solution = Vector::Zero(0);
        } else {
            auto decomposition = Eigen::FullPivLU<Matrix>{system->coefficients};
            decomposition.setThreshold(options.relative_rank_threshold());
            if (!decomposition.matrixLU().allFinite()) {
                diagnostics.push_back(
                    solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                                     "DC solver factorization produced a nonfinite value"));
                outcome = DcSolveOutcome::NumericalFailure;
                return std::nullopt;
            }
            const auto rank = static_cast<std::size_t>(decomposition.rank());
            metrics.rank = rank;
            if (rank != metrics.coordinate_count) {
                auto augmented =
                    Matrix{system->coefficients.rows(), system->coefficients.cols() + 1};
                augmented.leftCols(system->coefficients.cols()) = system->coefficients;
                const auto rhs_maximum = max_abs(system->right_hand_side);
                augmented.rightCols(1) = rhs_maximum > 0.0 ? system->right_hand_side / rhs_maximum
                                                           : system->right_hand_side;
                auto augmented_decomposition = Eigen::FullPivLU<Matrix>{augmented};
                augmented_decomposition.setThreshold(options.relative_rank_threshold());
                if (!augmented.allFinite() || !augmented_decomposition.matrixLU().allFinite()) {
                    diagnostics.push_back(solve_diagnostic(
                        analysis_diagnostic_codes::DcSolveNumericalFailure,
                        "DC solver augmented-rank classification produced a nonfinite value"));
                    outcome = DcSolveOutcome::NumericalFailure;
                    return std::nullopt;
                }
                const auto augmented_rank =
                    static_cast<std::size_t>(augmented_decomposition.rank());
                metrics.augmented_rank = augmented_rank;
                if (augmented_rank > rank) {
                    diagnostics.push_back(solve_diagnostic(
                        analysis_diagnostic_codes::DcSolveInconsistent,
                        "DC branch tableau is inconsistent at rank " + std::to_string(rank) +
                            " of " + std::to_string(metrics.coordinate_count)));
                    outcome = DcSolveOutcome::Inconsistent;
                } else {
                    diagnostics.push_back(solve_diagnostic(
                        analysis_diagnostic_codes::DcSolveRankDeficient,
                        "DC branch tableau does not uniquely determine all coordinates at rank " +
                            std::to_string(rank) + " of " +
                            std::to_string(metrics.coordinate_count)));
                    outcome = DcSolveOutcome::RankDeficient;
                }
                return std::nullopt;
            }

            const auto reciprocal_condition = decomposition.rcond();
            if (!finite(reciprocal_condition)) {
                diagnostics.push_back(
                    solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                                     "DC solver reciprocal-condition estimate is nonfinite"));
                outcome = DcSolveOutcome::NumericalFailure;
                return std::nullopt;
            }
            metrics.reciprocal_condition = reciprocal_condition;
            if (reciprocal_condition < options.minimum_reciprocal_condition()) {
                diagnostics.push_back(solve_diagnostic(
                    analysis_diagnostic_codes::DcSolveIllConditioned,
                    "DC branch tableau reciprocal condition " + number_text(reciprocal_condition) +
                        " is below the accepted minimum " +
                        number_text(options.minimum_reciprocal_condition())));
                outcome = DcSolveOutcome::IllConditioned;
                return std::nullopt;
            }

            if (external_coordinates == nullptr) {
                scaled_solution = decomposition.solve(system->right_hand_side);
            } else {
                // Factorization above establishes trust only. Never compute a replacement
                // answer for the external backend's observations.
                scaled_solution = Vector(static_cast<Eigen::Index>(external_coordinates->size()));
                for (Eigen::Index column = 0; column < scaled_solution.size(); ++column) {
                    scaled_solution(column) =
                        external_coordinates->at(static_cast<std::size_t>(column)) *
                        system->column_maxima(column);
                }
            }
            if (!scaled_solution.allFinite()) {
                diagnostics.push_back(
                    solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                                     "DC solver produced a nonfinite equilibrated solution"));
                outcome = DcSolveOutcome::NumericalFailure;
                return std::nullopt;
            }
        }

        auto coordinates = scaled_solution;
        for (Eigen::Index column = 0; column < coordinates.size(); ++column) {
            if (external_coordinates != nullptr) {
                coordinates(column) = external_coordinates->at(static_cast<std::size_t>(column));
            } else if (system->column_maxima(column) > 0.0) {
                coordinates(column) /= system->column_maxima(column);
            }
        }
        if (!coordinates.allFinite()) {
            diagnostics.push_back(
                solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                                 "DC solver coordinate reconstruction produced a nonfinite value"));
            outcome = DcSolveOutcome::NumericalFailure;
            return std::nullopt;
        }

        const Vector scaled_difference =
            system->coefficients * scaled_solution - system->right_hand_side;
        const auto numerator = max_abs(scaled_difference);
        const auto denominator =
            static_cast<long double>(matrix_infinity_norm(system->coefficients)) *
                static_cast<long double>(max_abs(scaled_solution)) +
            static_cast<long double>(max_abs(system->right_hand_side));
        const auto scaled_residual = denominator == 0.0L
                                         ? static_cast<long double>(numerator)
                                         : static_cast<long double>(numerator) / denominator;
        if (!scaled_difference.allFinite() || !std::isfinite(denominator) ||
            !std::isfinite(scaled_residual) ||
            scaled_residual > std::numeric_limits<double>::max()) {
            diagnostics.push_back(solve_diagnostic(
                analysis_diagnostic_codes::DcSolveNumericalFailure,
                "DC solver scaled residual evaluation produced a nonfinite value"));
            outcome = DcSolveOutcome::NumericalFailure;
            return std::nullopt;
        }
        metrics.scaled_residual = static_cast<double>(scaled_residual);

        return observe(model, options, identity, provenance, outcome, metrics, diagnostics,
                       coordinates, *tableau);
    }

    struct NonlinearEvaluation {
        Vector residual;
        Vector weights;
        Matrix jacobian;
        double reference_current_residual = 0.0;
        double reference_current_weight = 0.0;
    };

    [[nodiscard]] static ShockleyDiodeEvaluationStatus
    evaluate(const CompiledElectricalModel &model, const Tableau &tableau,
             const Vector &coordinates, const DcSolveOptions &options, NonlinearEvaluation &result,
             bool with_jacobian, std::vector<Diagnostic> &diagnostics) {
        result.residual = Vector::Zero(coordinates.size());
        result.weights = Vector::Zero(coordinates.size());
        if (with_jacobian) {
            result.jacobian = tableau.coefficients;
        }
        const auto potential = [&](ElectricalNodeId node) {
            const auto column = tableau.node_columns.at(node.index());
            return column ? coordinates(static_cast<Eigen::Index>(*column)) : 0.0;
        };
        const auto weight = [&](double floor, long double scale) {
            return static_cast<double>(
                static_cast<long double>(floor) +
                static_cast<long double>(options.relative_residual_tolerance()) * scale);
        };
        for (const auto &node : model.nodes()) {
            if (const auto row = tableau.node_columns.at(node.id.index())) {
                auto sum = 0.0L;
                auto scale = 0.0L;
                for (const auto &incidence : node.incidence) {
                    const auto term =
                        static_cast<long double>(incidence.sign) *
                        coordinates(static_cast<Eigen::Index>(tableau.node_coordinate_count +
                                                              incidence.branch.index()));
                    sum += term;
                    scale += std::abs(term);
                }
                result.residual(static_cast<Eigen::Index>(*row)) = static_cast<double>(sum);
                result.weights(static_cast<Eigen::Index>(*row)) =
                    weight(options.absolute_current_tolerance().value(), scale);
            }
        }
        auto reference_sum = 0.0L;
        auto reference_scale = 0.0L;
        for (const auto &incidence : model.nodes().at(model.reference().index()).incidence) {
            const auto term = static_cast<long double>(incidence.sign) *
                              coordinates(static_cast<Eigen::Index>(tableau.node_coordinate_count +
                                                                    incidence.branch.index()));
            reference_sum += term;
            reference_scale += std::abs(term);
        }
        result.reference_current_residual = static_cast<double>(reference_sum);
        result.reference_current_weight =
            weight(options.absolute_current_tolerance().value(), reference_scale);
        for (const auto &branch : model.branches()) {
            const auto row =
                static_cast<Eigen::Index>(tableau.node_coordinate_count + branch.id.index());
            const auto voltage = checked_difference(potential(branch.from), potential(branch.to));
            if (!voltage) {
                return ShockleyDiodeEvaluationStatus::Nonfinite;
            }
            const auto current = coordinates(row);
            auto status = ShockleyDiodeEvaluationStatus::Valid;
            std::visit(
                [&](const auto &law) {
                    using Law = std::decay_t<decltype(law)>;
                    auto residual = 0.0L;
                    auto scale = 0.0L;
                    auto floor = options.absolute_current_tolerance().value();
                    if constexpr (std::same_as<Law, ShockleyDiodeElement>) {
                        const auto diode = evaluate_shockley_diode(law.parameters(), *voltage);
                        status = diode.status;
                        if (status != ShockleyDiodeEvaluationStatus::Valid) {
                            diagnostics.push_back(
                                solve_diagnostic(analysis_diagnostic_codes::DcSolveDiodeEvaluation,
                                                 "Diode evaluation refused " + branch_scope(branch),
                                                 branch_entities(model, branch)));
                            return;
                        }
                        residual = static_cast<long double>(current) - diode.current;
                        scale =
                            std::abs(static_cast<long double>(current)) + std::abs(diode.current);
                        if (with_jacobian) {
                            if (const auto from = tableau.node_columns.at(branch.from.index())) {
                                result.jacobian(row, static_cast<Eigen::Index>(*from)) -=
                                    diode.conductance;
                            }
                            if (const auto to = tableau.node_columns.at(branch.to.index())) {
                                result.jacobian(row, static_cast<Eigen::Index>(*to)) +=
                                    diode.conductance;
                            }
                        }
                    } else if constexpr (std::same_as<Law, ResistanceElement>) {
                        const auto drop =
                            static_cast<long double>(law.parameter().nominal().value()) * current;
                        residual = static_cast<long double>(*voltage) - drop;
                        scale = std::abs(static_cast<long double>(*voltage)) + std::abs(drop);
                        floor = options.absolute_voltage_tolerance().value();
                    } else if constexpr (std::same_as<Law, DcVoltageSource>) {
                        residual = static_cast<long double>(*voltage) - law.value().value();
                        scale = std::abs(static_cast<long double>(*voltage)) +
                                std::abs(law.value().value());
                        floor = options.absolute_voltage_tolerance().value();
                    } else if constexpr (std::same_as<Law, DcCurrentSource>) {
                        residual = static_cast<long double>(current) - law.value().value();
                        scale = std::abs(static_cast<long double>(current)) +
                                std::abs(law.value().value());
                    } else if constexpr (std::same_as<Law, CapacitanceElement>) {
                        residual = current;
                        scale = std::abs(current);
                    } else if constexpr (std::same_as<Law, InductanceElement>) {
                        residual = *voltage;
                        scale = std::abs(*voltage);
                        floor = options.absolute_voltage_tolerance().value();
                    } else {
                        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                                  "AC source cannot enter nonlinear DC"};
                    }
                    result.residual(row) = static_cast<double>(residual);
                    result.weights(row) = weight(floor, scale);
                },
                branch.law);
            if (status != ShockleyDiodeEvaluationStatus::Valid) {
                return status;
            }
        }
        if (!finite(result.reference_current_residual) ||
            !finite(result.reference_current_weight) || result.reference_current_weight <= 0.0 ||
            !coordinates.allFinite() || !result.residual.allFinite() ||
            !result.weights.allFinite() || (result.weights.array() <= 0.0).any() ||
            (with_jacobian && !result.jacobian.allFinite())) {
            return ShockleyDiodeEvaluationStatus::Nonfinite;
        }
        return ShockleyDiodeEvaluationStatus::Valid;
    }

    [[nodiscard]] static std::optional<DcSolution>
    nonlinear(const CompiledElectricalModel &model,
              const NonlinearDcSolveOptions &nonlinear_options, const ContentHash &identity,
              const DcSolveProvenance &provenance, DcSolveOutcome &outcome, DcSolveMetrics &metrics,
              std::vector<Diagnostic> &diagnostics) {
        const auto &options = nonlinear_options.acceptance();
        const auto tableau = assemble_tableau(model, diagnostics);
        if (!tableau) {
            outcome = DcSolveOutcome::NumericalFailure;
            return std::nullopt;
        }
        metrics.coordinate_count = static_cast<std::size_t>(tableau->coefficients.cols());
        auto coordinates = Vector{Vector::Zero(tableau->coefficients.cols())};
        const auto fail = [&](DcSolveOutcome reason, std::string_view code, std::string message) {
            outcome = reason;
            diagnostics.push_back(solve_diagnostic(code, std::move(message)));
            return std::optional<DcSolution>{};
        };
        const auto residual_budget = [&] {
            return metrics.residual_evaluations < nonlinear_options.max_residual_evaluations();
        };
        for (;;) {
            if (!residual_budget() ||
                metrics.jacobian_evaluations >= nonlinear_options.max_jacobian_evaluations()) {
                return fail(DcSolveOutcome::EvaluationLimit,
                            analysis_diagnostic_codes::DcSolveEvaluationLimit,
                            "Nonlinear DC evaluation budget exhausted");
            }
            ++metrics.residual_evaluations;
            ++metrics.jacobian_evaluations;
            // These optional measurements describe this base evaluation, not a prior iterate.
            metrics.rank.reset();
            metrics.reciprocal_condition.reset();
            metrics.scaled_residual.reset();
            metrics.correction_error_ratio.reset();
            metrics.merit.reset();
            metrics.voltage_residual.reset();
            metrics.current_residual.reset();
            metrics.voltage_error_ratio.reset();
            metrics.current_error_ratio.reset();
            metrics.residual_weights.clear();
            auto base = NonlinearEvaluation{};
            const auto status =
                evaluate(model, *tableau, coordinates, options, base, true, diagnostics);
            if (status != ShockleyDiodeEvaluationStatus::Valid) {
                return fail(status == ShockleyDiodeEvaluationStatus::OutsideDomain
                                ? DcSolveOutcome::DomainLimited
                                : DcSolveOutcome::NumericalFailure,
                            analysis_diagnostic_codes::DcSolveNumericalFailure,
                            "Nonlinear DC base evaluation failed");
            }
            const Vector normalized = base.residual.cwiseQuotient(base.weights);
            const auto merit = normalized.stableNorm();
            if (!normalized.allFinite() || !finite(merit)) {
                return fail(DcSolveOutcome::NumericalFailure,
                            analysis_diagnostic_codes::DcSolveNumericalFailure,
                            "Nonlinear DC merit is nonfinite");
            }
            metrics.merit = merit;
            metrics.voltage_residual = Quantity{UnitDimension::Voltage, 0.0};
            metrics.current_residual =
                Quantity{UnitDimension::Current, std::abs(base.reference_current_residual)};
            metrics.voltage_error_ratio = 0.0;
            metrics.current_error_ratio =
                std::abs(base.reference_current_residual) / base.reference_current_weight;
            metrics.residual_weights.clear();
            for (Eigen::Index row = 0; row < base.weights.size(); ++row) {
                auto dimension = UnitDimension::Current;
                if (static_cast<std::size_t>(row) >= tableau->node_coordinate_count) {
                    const auto &law =
                        model.branches()
                            .at(static_cast<std::size_t>(row) - tableau->node_coordinate_count)
                            .law;
                    if (std::holds_alternative<ResistanceElement>(law) ||
                        std::holds_alternative<InductanceElement>(law) ||
                        std::holds_alternative<DcVoltageSource>(law)) {
                        dimension = UnitDimension::Voltage;
                    }
                }
                metrics.residual_weights.emplace_back(dimension, base.weights(row));
                if (dimension == UnitDimension::Voltage) {
                    metrics.voltage_residual =
                        Quantity{dimension, std::max(metrics.voltage_residual->value(),
                                                     std::abs(base.residual(row)))};
                    metrics.voltage_error_ratio =
                        std::max(*metrics.voltage_error_ratio, std::abs(normalized(row)));
                } else {
                    metrics.current_residual =
                        Quantity{dimension, std::max(metrics.current_residual->value(),
                                                     std::abs(base.residual(row)))};
                    metrics.current_error_ratio =
                        std::max(*metrics.current_error_ratio, std::abs(normalized(row)));
                }
            }
            const auto system =
                equilibrate(Tableau{base.jacobian, -base.residual, tableau->node_columns,
                                    tableau->node_coordinate_count},
                            diagnostics);
            if (!system) {
                outcome = DcSolveOutcome::NumericalFailure;
                return std::nullopt;
            }
            auto correction = Vector{Vector::Zero(coordinates.size())};
            if (coordinates.size() != 0) {
                auto decomposition = Eigen::FullPivLU<Matrix>{system->coefficients};
                decomposition.setThreshold(options.relative_rank_threshold());
                if (!decomposition.matrixLU().allFinite()) {
                    return fail(DcSolveOutcome::NumericalFailure,
                                analysis_diagnostic_codes::DcSolveNumericalFailure,
                                "Nonlinear DC Jacobian factorization is nonfinite");
                }
                metrics.rank = static_cast<std::size_t>(decomposition.rank());
                if (*metrics.rank != metrics.coordinate_count) {
                    return fail(DcSolveOutcome::JacobianSingular,
                                analysis_diagnostic_codes::DcSolveJacobianSingular,
                                "Nonlinear DC Jacobian is singular at the current iterate");
                }
                const auto rcond = decomposition.rcond();
                if (!finite(rcond)) {
                    return fail(DcSolveOutcome::NumericalFailure,
                                analysis_diagnostic_codes::DcSolveNumericalFailure,
                                "Nonlinear DC reciprocal condition is nonfinite");
                }
                metrics.reciprocal_condition = rcond;
                if (rcond < options.minimum_reciprocal_condition()) {
                    return fail(DcSolveOutcome::IllConditioned,
                                analysis_diagnostic_codes::DcSolveIllConditioned,
                                "Nonlinear DC Jacobian is below the accepted reciprocal condition");
                }
                const Vector scaled = decomposition.solve(system->right_hand_side);
                const Vector difference = system->coefficients * scaled - system->right_hand_side;
                const auto denominator =
                    static_cast<long double>(matrix_infinity_norm(system->coefficients)) *
                        max_abs(scaled) +
                    max_abs(system->right_hand_side);
                const auto backward = denominator == 0.0L
                                          ? static_cast<long double>(max_abs(difference))
                                          : max_abs(difference) / denominator;
                if (!scaled.allFinite() || !difference.allFinite() || !std::isfinite(denominator) ||
                    !std::isfinite(backward)) {
                    return fail(DcSolveOutcome::NumericalFailure,
                                analysis_diagnostic_codes::DcSolveNumericalFailure,
                                "Nonlinear DC correction residual is nonfinite");
                }
                metrics.scaled_residual = static_cast<double>(backward);
                if (backward > options.relative_residual_tolerance()) {
                    return fail(DcSolveOutcome::ResidualFailure,
                                analysis_diagnostic_codes::DcSolveResidualFailure,
                                "Nonlinear DC linearized correction failed its residual gate");
                }
                correction = scaled.cwiseQuotient(system->column_maxima);
            } else {
                metrics.rank = 0;
                metrics.reciprocal_condition = 1.0;
                metrics.scaled_residual = 0.0;
            }
            if (!correction.allFinite()) {
                return fail(DcSolveOutcome::NumericalFailure,
                            analysis_diagnostic_codes::DcSolveNumericalFailure,
                            "Nonlinear DC undamped correction is nonfinite");
            }
            auto correction_ratio = 0.0L;
            for (Eigen::Index column = 0; column < correction.size(); ++column) {
                const auto floor = static_cast<std::size_t>(column) < tableau->node_coordinate_count
                                       ? options.absolute_voltage_tolerance().value()
                                       : options.absolute_current_tolerance().value();
                const auto next =
                    static_cast<long double>(coordinates(column)) + correction(column);
                const auto allowed =
                    static_cast<long double>(floor) +
                    options.relative_residual_tolerance() *
                        std::max(std::abs(static_cast<long double>(coordinates(column))),
                                 std::abs(next));
                correction_ratio =
                    std::max(correction_ratio,
                             std::abs(static_cast<long double>(correction(column))) / allowed);
            }
            if (!std::isfinite(correction_ratio) ||
                correction_ratio > std::numeric_limits<double>::max()) {
                return fail(DcSolveOutcome::NumericalFailure,
                            analysis_diagnostic_codes::DcSolveNumericalFailure,
                            "Nonlinear DC correction normalization is nonfinite");
            }
            metrics.correction_error_ratio = static_cast<double>(correction_ratio);
            if (*metrics.voltage_error_ratio <= 1.0 && *metrics.current_error_ratio <= 1.0 &&
                correction_ratio <= 1.0L) {
                if (!residual_budget()) {
                    return fail(DcSolveOutcome::EvaluationLimit,
                                analysis_diagnostic_codes::DcSolveEvaluationLimit,
                                "Nonlinear DC final residual evaluation budget exhausted");
                }
                ++metrics.residual_evaluations;
                // Independently evaluate all original laws and all-node KCL, including the
                // reference.
                return observe(model, options, identity, provenance, outcome, metrics, diagnostics,
                               coordinates, *tableau, nonlinear_options);
            }
            if (metrics.iterations >= nonlinear_options.max_iterations()) {
                return fail(DcSolveOutcome::IterationLimit,
                            analysis_diagnostic_codes::DcSolveIterationLimit,
                            "Nonlinear DC accepted-correction budget exhausted");
            }
            auto alpha_bound = 1.0L;
            const ElectricalBranch *limiting_branch = nullptr;
            const auto potential = [&](const Vector &values, ElectricalNodeId node) {
                const auto column = tableau->node_columns.at(node.index());
                return column ? static_cast<long double>(values(static_cast<Eigen::Index>(*column)))
                              : 0.0L;
            };
            for (const auto &branch : model.branches()) {
                if (const auto *diode = std::get_if<ShockleyDiodeElement>(&branch.law)) {
                    const auto voltage =
                        potential(coordinates, branch.from) - potential(coordinates, branch.to);
                    const auto delta =
                        potential(correction, branch.from) - potential(correction, branch.to);
                    const auto &domain = diode->parameters().voltage_domain();
                    auto candidate_bound = 1.0L;
                    if (delta > 0.0L) {
                        candidate_bound = (domain.maximum()->value() - voltage) / delta;
                    } else if (delta < 0.0L) {
                        candidate_bound = (domain.minimum()->value() - voltage) / delta;
                    }
                    if (candidate_bound < alpha_bound) {
                        alpha_bound = candidate_bound;
                        limiting_branch = &branch;
                    }
                }
            }
            auto alpha = static_cast<double>(alpha_bound);
            if (static_cast<long double>(alpha) > alpha_bound) {
                alpha = std::nextafter(alpha, 0.0);
            }
            if (!finite(alpha) || alpha < NonlinearDcSolveOptions::minimum_step()) {
                if (limiting_branch != nullptr) {
                    diagnostics.push_back(
                        solve_diagnostic(analysis_diagnostic_codes::DcSolveDomainLimited,
                                         "Authored diode domain limits progression for " +
                                             branch_scope(*limiting_branch),
                                         branch_entities(model, *limiting_branch)));
                }
                return fail(DcSolveOutcome::DomainLimited,
                            analysis_diagnostic_codes::DcSolveDomainLimited,
                            "Nonlinear DC has no admissible progressing domain step");
            }
            auto accepted = false;
            auto admissible = false;
            auto last_domain_diagnostics = std::vector<Diagnostic>{};
            for (std::size_t halving = 0; halving <= nonlinear_options.max_backtracks() &&
                                          alpha >= NonlinearDcSolveOptions::minimum_step();
                 ++halving) {
                if (!residual_budget()) {
                    return fail(DcSolveOutcome::EvaluationLimit,
                                analysis_diagnostic_codes::DcSolveEvaluationLimit,
                                "Nonlinear DC trial residual budget exhausted");
                }
                if (halving != 0) {
                    ++metrics.backtracks;
                }
                ++metrics.residual_evaluations;
                const Vector trial = coordinates + alpha * correction;
                auto evaluated = NonlinearEvaluation{};
                auto trial_diagnostics = std::vector<Diagnostic>{};
                const auto trial_status =
                    evaluate(model, *tableau, trial, options, evaluated, false, trial_diagnostics);
                if (trial_status == ShockleyDiodeEvaluationStatus::OutsideDomain) {
                    ++metrics.domain_rejections;
                    last_domain_diagnostics = std::move(trial_diagnostics);
                } else if (trial_status == ShockleyDiodeEvaluationStatus::Nonfinite) {
                    ++metrics.nonfinite_rejections;
                } else {
                    admissible = true;
                    const Vector trial_normalized = evaluated.residual.cwiseQuotient(base.weights);
                    const auto trial_merit = trial_normalized.stableNorm();
                    if (!trial_normalized.allFinite() || !finite(trial_merit)) {
                        ++metrics.nonfinite_rejections;
                    } else if (trial_merit <=
                               (1.0 - NonlinearDcSolveOptions::armijo_coefficient() * alpha) *
                                   merit) {
                        coordinates = trial;
                        ++metrics.iterations;
                        accepted = true;
                        break;
                    }
                }
                alpha *= 0.5;
            }
            if (!accepted) {
                if (!admissible) {
                    diagnostics.insert(diagnostics.end(), last_domain_diagnostics.begin(),
                                       last_domain_diagnostics.end());
                }
                return fail(admissible ? DcSolveOutcome::LineSearchFailed
                                       : DcSolveOutcome::DomainLimited,
                            admissible ? analysis_diagnostic_codes::DcSolveLineSearchFailed
                                       : analysis_diagnostic_codes::DcSolveDomainLimited,
                            "Nonlinear DC did not find an admissible Armijo-decreasing trial");
            }
        }
    }

    [[nodiscard]] static std::optional<DcSolution>
    observe(const CompiledElectricalModel &model, const DcSolveOptions &options,
            const ContentHash &identity, const DcSolveProvenance &provenance,
            DcSolveOutcome &outcome, DcSolveMetrics &metrics, std::vector<Diagnostic> &diagnostics,
            const Vector &coordinates, const Tableau &tableau,
            std::optional<NonlinearDcSolveOptions> nonlinear_options = std::nullopt) {
        auto node_values = std::vector<double>(model.nodes().size(), 0.0);
        auto node_results = std::vector<DcNodeResult>{};
        node_results.reserve(model.nodes().size());
        for (const auto &node : model.nodes()) {
            if (const auto column = tableau.node_columns.at(node.id.index())) {
                node_values.at(node.id.index()) = coordinates(static_cast<Eigen::Index>(*column));
            }
            node_results.push_back(DcNodeResult{
                node.id, Quantity{UnitDimension::Voltage, node_values.at(node.id.index())}});
        }

        auto current_values = std::vector<double>(model.branches().size(), 0.0);
        auto branch_results = std::vector<DcBranchResult>{};
        branch_results.reserve(model.branches().size());
        for (const auto &branch : model.branches()) {
            const auto current = coordinates(
                static_cast<Eigen::Index>(tableau.node_coordinate_count + branch.id.index()));
            const auto voltage = checked_difference(node_values.at(branch.from.index()),
                                                    node_values.at(branch.to.index()));
            const auto power = voltage ? checked_product(*voltage, current) : std::nullopt;
            if (!voltage || !power || !finite(current)) {
                diagnostics.push_back(
                    solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                                     "DC solver could not reconstruct finite observations for " +
                                         branch_scope(branch),
                                     branch_entities(model, branch)));
                outcome = DcSolveOutcome::NumericalFailure;
                return std::nullopt;
            }
            current_values.at(branch.id.index()) = current;
            branch_results.push_back(DcBranchResult{
                branch.id, Quantity{UnitDimension::Voltage, *voltage},
                Quantity{UnitDimension::Current, current}, Quantity{UnitDimension::Power, *power}});
        }

        auto residuals = ResidualTracker{options, metrics, diagnostics};
        if (!residuals.record_voltage(
                std::abs(node_values.at(model.reference().index())),
                std::abs(node_values.at(model.reference().index())),
                "DC reference node:" + std::to_string(model.reference().index()) +
                    " exceeds its voltage residual tolerance",
                [&] {
                    auto entities = std::vector<EntityRef>{};
                    append_node_entities(model, model.reference(), entities);
                    return entities;
                }())) {
            diagnostics.push_back(
                solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                                 "DC solver could not evaluate the reference-voltage residual"));
            outcome = DcSolveOutcome::NumericalFailure;
            return std::nullopt;
        }
        for (const auto &node : model.nodes()) {
            auto sum = 0.0L;
            auto scale = 0.0L;
            for (const auto &incidence : node.incidence) {
                const auto term =
                    static_cast<long double>(incidence.sign) *
                    static_cast<long double>(current_values.at(incidence.branch.index()));
                sum += term;
                scale += std::abs(term);
            }
            if (!std::isfinite(sum) || !std::isfinite(scale) ||
                std::abs(sum) > std::numeric_limits<double>::max() ||
                scale > std::numeric_limits<double>::max()) {
                diagnostics.push_back(solve_diagnostic(
                    analysis_diagnostic_codes::DcSolveNumericalFailure,
                    "DC solver could not evaluate KCL at node:" + std::to_string(node.id.index())));
                outcome = DcSolveOutcome::NumericalFailure;
                return std::nullopt;
            }
            auto entities = std::vector<EntityRef>{};
            append_node_entities(model, node.id, entities);
            if (!residuals.record_current(
                    static_cast<double>(std::abs(sum)), static_cast<double>(scale),
                    "KCL residual exceeds tolerance at node:" + std::to_string(node.id.index()),
                    std::move(entities))) {
                diagnostics.push_back(
                    solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                                     "DC solver could not normalize KCL at node:" +
                                         std::to_string(node.id.index())));
                outcome = DcSolveOutcome::NumericalFailure;
                return std::nullopt;
            }
        }

        for (const auto &branch : model.branches()) {
            const auto voltage = branch_results.at(branch.id.index()).voltage.value();
            const auto current = current_values.at(branch.id.index());
            const auto entities = branch_entities(model, branch);
            const auto valid = std::visit(
                [&](const auto &law) -> bool {
                    using Law = std::decay_t<decltype(law)>;
                    if constexpr (std::same_as<Law, ResistanceElement>) {
                        const auto resistance_drop =
                            checked_product(law.parameter().nominal().value(), current);
                        if (!resistance_drop) {
                            return false;
                        }
                        const auto difference = checked_difference(voltage, *resistance_drop);
                        if (!difference) {
                            return false;
                        }
                        return residuals.record_voltage(
                            std::abs(*difference), std::abs(voltage) + std::abs(*resistance_drop),
                            "Resistance-law residual exceeds tolerance for " + branch_scope(branch),
                            entities);
                    } else if constexpr (std::same_as<Law, ShockleyDiodeElement>) {
                        const auto evaluation = evaluate_shockley_diode(law.parameters(), voltage);
                        if (evaluation.status != ShockleyDiodeEvaluationStatus::Valid) {
                            return false;
                        }
                        const auto difference = checked_difference(current, evaluation.current);
                        return difference && residuals.record_current(
                                                 std::abs(*difference),
                                                 std::abs(current) + std::abs(evaluation.current),
                                                 "Diode-law residual exceeds tolerance for " +
                                                     branch_scope(branch),
                                                 entities);
                    } else if constexpr (std::same_as<Law, CapacitanceElement>) {
                        return residuals.record_current(
                            std::abs(current), std::abs(current),
                            "DC capacitor-current residual exceeds tolerance for " +
                                branch_scope(branch),
                            entities);
                    } else if constexpr (std::same_as<Law, InductanceElement>) {
                        return residuals.record_voltage(
                            std::abs(voltage), std::abs(voltage),
                            "DC inductor-voltage residual exceeds tolerance for " +
                                branch_scope(branch),
                            entities);
                    } else if constexpr (std::same_as<Law, DcVoltageSource>) {
                        const auto difference = checked_difference(voltage, law.value().value());
                        if (!difference) {
                            return false;
                        }
                        return residuals.record_voltage(
                            std::abs(*difference),
                            std::abs(voltage) + std::abs(law.value().value()),
                            "Voltage-source residual exceeds tolerance for " + branch_scope(branch),
                            entities);
                    } else if constexpr (std::same_as<Law, DcCurrentSource>) {
                        const auto difference = checked_difference(current, law.value().value());
                        if (!difference) {
                            return false;
                        }
                        return residuals.record_current(
                            std::abs(*difference),
                            std::abs(current) + std::abs(law.value().value()),
                            "Current-source residual exceeds tolerance for " + branch_scope(branch),
                            entities);
                    } else {
                        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                                  "AC source cannot enter DC residual validation"};
                    }
                },
                branch.law);
            if (!valid) {
                diagnostics.push_back(
                    solve_diagnostic(analysis_diagnostic_codes::DcSolveNumericalFailure,
                                     "DC solver could not evaluate the original-unit law for " +
                                         branch_scope(branch),
                                     entities));
                outcome = DcSolveOutcome::NumericalFailure;
                return std::nullopt;
            }
        }
        residuals.publish();
        if (*metrics.scaled_residual > options.relative_residual_tolerance()) {
            diagnostics.push_back(solve_diagnostic(
                analysis_diagnostic_codes::DcSolveResidualFailure,
                "Equilibrated DC branch tableau exceeds its relative residual tolerance"));
            residuals.failed = true;
        }
        if (residuals.failed) {
            outcome = DcSolveOutcome::ResidualFailure;
            return std::nullopt;
        }

        auto probe_results = std::vector<DcProbeResult>{};
        probe_results.reserve(model.probes().size());
        for (const auto &probe : model.probes()) {
            const auto value = std::visit(
                [&](const auto &target) {
                    using Target = std::decay_t<decltype(target)>;
                    if constexpr (std::same_as<Target, ElectricalVoltageObservation>) {
                        const auto voltage = checked_difference(node_values.at(target.from.index()),
                                                                node_values.at(target.to.index()));
                        if (!voltage) {
                            return std::optional<Quantity>{};
                        }
                        return std::optional{Quantity{UnitDimension::Voltage, *voltage}};
                    } else {
                        return std::optional{Quantity{UnitDimension::Current,
                                                      current_values.at(target.branch.index())}};
                    }
                },
                probe.target);
            if (!value) {
                diagnostics.push_back(solve_diagnostic(
                    analysis_diagnostic_codes::DcSolveNumericalFailure,
                    "DC solver could not reconstruct finite probe '" + probe.key.value() + "'"));
                outcome = DcSolveOutcome::NumericalFailure;
                return std::nullopt;
            }
            probe_results.push_back(DcProbeResult{probe.key, *value});
        }

        outcome = nonlinear_options ? DcSolveOutcome::Converged : DcSolveOutcome::Success;
        return DcSolution{identity,
                          model,
                          options,
                          provenance,
                          std::move(node_results),
                          std::move(branch_results),
                          std::move(probe_results),
                          std::move(nonlinear_options)};
    }
};

detail::NonlinearDcEquationEvaluation detail::evaluate_nonlinear_dc_equations(
    const CompiledElectricalModel &model, const DcSolveOptions &options,
    const std::vector<double> &coordinates, const std::vector<double> &direction) {
    auto diagnostics = std::vector<Diagnostic>{};
    const auto tableau = assemble_tableau(model, diagnostics);
    if (!tableau || coordinates.size() != static_cast<std::size_t>(tableau->coefficients.cols()) ||
        direction.size() != coordinates.size()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Invalid private nonlinear evaluation coordinates"};
    }
    const Vector values =
        Eigen::Map<const Vector>{coordinates.data(), static_cast<Eigen::Index>(coordinates.size())};
    const Vector tangent =
        Eigen::Map<const Vector>{direction.data(), static_cast<Eigen::Index>(direction.size())};
    auto evaluation = DcSolution::Solver::NonlinearEvaluation{};
    const auto status = DcSolution::Solver::evaluate(model, *tableau, values, options, evaluation,
                                                     true, diagnostics);
    if (status != ShockleyDiodeEvaluationStatus::Valid) {
        return {status, {}, {}};
    }
    const Vector product = evaluation.jacobian * tangent;
    return {status,
            std::vector<double>{evaluation.residual.data(),
                                evaluation.residual.data() + evaluation.residual.size()},
            std::vector<double>{product.data(), product.data() + product.size()}};
}

DcSolveReport::DcSolveReport(const CompiledElectricalModel &model, const DcSolveOptions &options)
    : model_{model}, options_{options}, provenance_{native_provenance()},
      analysis_identity_{make_analysis_identity(model_, options_, provenance_)},
      solution_{DcSolution::Solver::solve(model_, options_, analysis_identity_, provenance_,
                                          outcome_, metrics_, diagnostics_)} {}

DcSolveReport::DcSolveReport(const CompiledElectricalModel &model,
                             const NonlinearDcSolveOptions &options)
    : model_{model}, options_{options.acceptance()}, nonlinear_options_{options},
      provenance_{nonlinear_provenance()},
      analysis_identity_{make_analysis_identity(model_, options_, provenance_, &options)},
      solution_{DcSolution::Solver::nonlinear(model_, options, analysis_identity_, provenance_,
                                              outcome_, metrics_, diagnostics_)} {}

DcSolveReport::DcSolveReport(const NgspiceDcAnalysis &analysis, std::string_view output,
                             const DcSolveOptions &options)
    : model_{analysis.model()}, options_{options}, provenance_{ngspice_provenance(analysis)},
      analysis_identity_{make_analysis_identity(model_, options_, provenance_)} {
    const auto coordinates = detail::read_ngspice_dc_coordinates(analysis, output);
    solution_ = DcSolution::Solver::solve(model_, options_, analysis_identity_, provenance_,
                                          outcome_, metrics_, diagnostics_, &coordinates);
}

DcSolveReport solve_dc(const CompiledElectricalModel &model, const DcSolveOptions &options) {
    return DcSolveReport{model, options};
}

DcSolveReport solve_dc(const CompiledElectricalModel &model,
                       const NonlinearDcSolveOptions &options) {
    return DcSolveReport{model, options};
}

DcSolveReport solve_ngspice_dc(const NgspiceDcAnalysis &analysis, std::string_view output,
                               const DcSolveOptions &options) {
    return DcSolveReport{analysis, output, options};
}

} // namespace volt
