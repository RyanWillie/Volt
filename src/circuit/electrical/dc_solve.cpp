#include <volt/electrical/dc_solve.hpp>

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

[[nodiscard]] ContentHash make_analysis_identity(const CompiledElectricalModel &model,
                                                 const DcSolveOptions &options) {
    auto encoder = IdentityEncoder{};
    encoder.text("volt.linear-dc-analysis");
    encoder.text(std::to_string(DcSolveReport::contract_version()));
    encoder.text(model.identity().value());
    encoder.text(DcSolveReport::backend());
    encoder.text(DcSolveOptions::scaling());
    encoder.number(options.relative_rank_threshold());
    encoder.number(options.minimum_reciprocal_condition());
    encoder.number(options.relative_residual_tolerance());
    encoder.number(options.absolute_voltage_tolerance().value());
    encoder.number(options.absolute_current_tolerance().value());
    return encoder.digest();
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
                } else if constexpr (std::same_as<Law, DcVoltageSource>) {
                    add_node_coefficient(law_row, branch.from, 1.0);
                    add_node_coefficient(law_row, branch.to, -1.0);
                    result.right_hand_side(law_row) = law.value().value();
                } else {
                    static_assert(std::same_as<Law, DcCurrentSource>);
                    result.coefficients(law_row, current_column) = 1.0;
                    result.right_hand_side(law_row) = law.value().value();
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

std::string_view DcSolveReport::backend() noexcept { return backend_name; }

DcSolution::DcSolution(ContentHash analysis_identity, CompiledElectricalModel model,
                       DcSolveOptions options, std::vector<DcNodeResult> nodes,
                       std::vector<DcBranchResult> branches, std::vector<DcProbeResult> probes)
    : analysis_identity_{std::move(analysis_identity)}, model_{std::move(model)},
      options_{std::move(options)}, nodes_{std::move(nodes)}, branches_{std::move(branches)},
      probes_{std::move(probes)} {}

class DcSolution::Solver final {
  public:
    [[nodiscard]] static std::optional<DcSolution>
    solve(const CompiledElectricalModel &model, const DcSolveOptions &options,
          const ContentHash &identity, DcSolveOutcome &outcome, DcSolveMetrics &metrics,
          std::vector<Diagnostic> &diagnostics) {
        const auto tableau = assemble_tableau(model, diagnostics);
        if (!tableau) {
            outcome = DcSolveOutcome::NumericalFailure;
            return std::nullopt;
        }
        metrics.coordinate_count = static_cast<std::size_t>(tableau->coefficients.cols());
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

            scaled_solution = decomposition.solve(system->right_hand_side);
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
            if (system->column_maxima(column) > 0.0) {
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

        auto node_values = std::vector<double>(model.nodes().size(), 0.0);
        auto node_results = std::vector<DcNodeResult>{};
        node_results.reserve(model.nodes().size());
        for (const auto &node : model.nodes()) {
            if (const auto column = tableau->node_columns.at(node.id.index())) {
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
                static_cast<Eigen::Index>(tableau->node_coordinate_count + branch.id.index()));
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
                [&](const auto &law) {
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
                    } else {
                        static_assert(std::same_as<Law, DcCurrentSource>);
                        const auto difference = checked_difference(current, law.value().value());
                        if (!difference) {
                            return false;
                        }
                        return residuals.record_current(
                            std::abs(*difference),
                            std::abs(current) + std::abs(law.value().value()),
                            "Current-source residual exceeds tolerance for " + branch_scope(branch),
                            entities);
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

        outcome = DcSolveOutcome::Success;
        return DcSolution{identity,
                          model,
                          options,
                          std::move(node_results),
                          std::move(branch_results),
                          std::move(probe_results)};
    }
};

DcSolveReport::DcSolveReport(const CompiledElectricalModel &model, const DcSolveOptions &options)
    : model_{model}, options_{options},
      analysis_identity_{make_analysis_identity(model_, options_)},
      solution_{DcSolution::Solver::solve(model_, options_, analysis_identity_, outcome_, metrics_,
                                          diagnostics_)} {}

DcSolveReport solve_dc(const CompiledElectricalModel &model, const DcSolveOptions &options) {
    return DcSolveReport{model, options};
}

} // namespace volt
