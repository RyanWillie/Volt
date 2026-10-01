#include "transient_solve_internal.hpp"
#include <Eigen/Dense>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <type_traits>
#include <volt/core/errors.hpp>
#include <volt/electrical/transient_solve.hpp>

namespace volt {
namespace {
using Matrix = Eigen::MatrixXd;
using Vector = Eigen::VectorXd;
static_assert(EIGEN_MAJOR_VERSION == 5 && EIGEN_MINOR_VERSION == 0 && EIGEN_PATCH_VERSION == 0);

[[nodiscard]] std::string number_text(double value) {
    auto buffer = std::array<char, 64>{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                      std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) {
        throw KernelLogicError{ErrorCode::InvalidState, "Failed to encode transient solver number"};
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
                                                 const TransientSolveOptions &options) {
    IdentityEncoder encoder;
    encoder.text("volt.linear-transient-analysis");
    encoder.text(std::to_string(TransientSolveReport::contract_version()));
    encoder.text(model.identity().value());
    encoder.text(TransientSolveReport::backend());
    encoder.text(TransientSolveOptions::scaling());
    encoder.text("frozen-state-projector:adaptive-be-full-two-half:1");
    encoder.number(options.minimum_step().value());
    encoder.number(options.initial_step().value());
    encoder.number(options.maximum_step().value());
    encoder.text(std::to_string(options.maximum_trials()));
    encoder.text(std::to_string(options.maximum_accepted_steps()));
    encoder.number(options.relative_temporal_tolerance());
    encoder.number(options.absolute_temporal_voltage().value());
    encoder.number(options.absolute_temporal_current().value());
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

std::vector<EntityRef> model_entities(const CompiledElectricalModel &model) {
    std::vector<EntityRef> entities;
    for (const auto &branch : model.branches())
        for (const auto &entity : branch_entities(model, branch))
            append_entity(entities, entity);
    return entities;
}

bool finite(double value) { return std::isfinite(value); }

double max_abs(const Vector &value) { return value.size() ? value.cwiseAbs().maxCoeff() : 0.0; }

double matrix_infinity_norm(const Matrix &value) {
    return value.size() ? value.cwiseAbs().rowwise().sum().maxCoeff() : 0.0;
}

struct Tableau {
    Matrix coefficients;
    Vector right_hand_side;
    std::vector<std::optional<std::size_t>> node_columns;
    std::size_t node_coordinate_count;
};

// Bind only retained compiled element origins; never resolve authoring connectivity again.
std::vector<double> initial_storage(const CompiledElectricalModel &model) {
    std::vector<double> result(model.branches().size());
    for (const auto &storage : model.storage()) {
        std::visit(
            [&](const auto &law) {
                const auto &origin = std::get<ElectricalElementOrigin>(
                    model.branches().at(law.branch.index()).origin);
                const auto found =
                    std::ranges::find_if(model.transient_request()->initial_state(),
                                         [&](const TransientInitialState &state) {
                                             return state.occurrence().id() == origin.occurrence &&
                                                    state.part() == origin.part &&
                                                    state.element() == origin.element;
                                         });
                if (found == model.transient_request()->initial_state().end())
                    throw KernelLogicError{ErrorCode::InvalidState,
                                           "Compiled transient storage has no initial state"};
                result.at(law.branch.index()) = found->value().value();
            },
            storage);
    }
    return result;
}

std::optional<Tableau> assemble_tableau(const CompiledElectricalModel &model, double time,
                                        const TransientSample *previous,
                                        const std::vector<double> *initial, double duration,
                                        std::vector<Diagnostic> &diagnostics) {
    const auto node_count = model.nodes().size();
    const auto branch_count = model.branches().size();
    if (node_count == 0 || node_count - 1 > std::numeric_limits<std::size_t>::max() - branch_count)
        return std::nullopt;
    const auto node_coordinates = node_count - 1;
    const auto count = node_coordinates + branch_count;
    if (count > static_cast<std::size_t>(std::numeric_limits<Eigen::Index>::max()))
        return std::nullopt;
    const auto dimension = static_cast<Eigen::Index>(count);
    Tableau result{Matrix::Zero(dimension, dimension), Vector::Zero(dimension),
                   std::vector<std::optional<std::size_t>>(node_count), node_coordinates};
    std::size_t next_column = 0;
    for (const auto &node : model.nodes())
        if (node.id != model.reference())
            result.node_columns.at(node.id.index()) = next_column++;
    const auto add_node = [&](Eigen::Index row, ElectricalNodeId node, double value) {
        if (const auto column = result.node_columns.at(node.index()))
            result.coefficients(row, static_cast<Eigen::Index>(*column)) += value;
    };
    bool valid = true;
    for (const auto &branch : model.branches()) {
        const auto column = static_cast<Eigen::Index>(node_coordinates + branch.id.index());
        if (const auto row = result.node_columns.at(branch.from.index()))
            result.coefficients(static_cast<Eigen::Index>(*row), column) += 1;
        if (const auto row = result.node_columns.at(branch.to.index()))
            result.coefficients(static_cast<Eigen::Index>(*row), column) -= 1;
        const auto row = column;
        const auto voltage = [&](double coefficient) {
            add_node(row, branch.from, coefficient);
            add_node(row, branch.to, -coefficient);
        };
        std::visit(
            [&](const auto &law) {
                using Law = std::decay_t<decltype(law)>;
                if constexpr (std::same_as<Law, ResistanceElement>) {
                    voltage(1);
                    result.coefficients(row, column) = -law.parameter().nominal().value();
                } else if constexpr (std::same_as<Law, CapacitanceElement>) {
                    if (initial) {
                        voltage(1);
                        result.right_hand_side(row) = initial->at(branch.id.index());
                    } else {
                        const auto coefficient = law.parameter().nominal().value() / duration;
                        valid = valid && finite(coefficient) && coefficient > 0;
                        voltage(-coefficient);
                        result.coefficients(row, column) = 1;
                        result.right_hand_side(row) =
                            -coefficient * previous->branches.at(branch.id.index()).voltage.value();
                    }
                } else if constexpr (std::same_as<Law, InductanceElement>) {
                    if (initial) {
                        result.coefficients(row, column) = 1;
                        result.right_hand_side(row) = initial->at(branch.id.index());
                    } else {
                        const auto coefficient = law.parameter().nominal().value() / duration;
                        valid = valid && finite(coefficient) && coefficient > 0;
                        voltage(1);
                        result.coefficients(row, column) = -coefficient;
                        result.right_hand_side(row) =
                            -coefficient * previous->branches.at(branch.id.index()).current.value();
                    }
                } else if constexpr (std::same_as<Law, TransientVoltageSource>) {
                    voltage(1);
                    result.right_hand_side(row) =
                        law.waveform().value_at(Quantity{UnitDimension::Time, time}).value();
                } else if constexpr (std::same_as<Law, TransientCurrentSource>) {
                    result.coefficients(row, column) = 1;
                    result.right_hand_side(row) =
                        law.waveform().value_at(Quantity{UnitDimension::Time, time}).value();
                } else {
                    throw KernelArgumentError{ErrorCode::InvalidArgument,
                                              "Transient model contains a non-transient source"};
                }
            },
            branch.law);
    }
    if (!valid || !result.coefficients.allFinite() || !result.right_hand_side.allFinite()) {
        diagnostics.push_back(
            solve_diagnostic("TRANSIENT_SOLVE_NUMERICAL_FAILURE",
                             "Nonfinite or zero storage coefficient or tableau assembly"));
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
            solve_diagnostic("TRANSIENT_SOLVE_NUMERICAL_FAILURE",
                             "Transient solver equilibration produced a nonfinite value"));
        return std::nullopt;
    }
    return EquilibratedSystem{std::move(coefficients), std::move(right_hand_side),
                              std::move(column_maxima)};
}

std::optional<TransientSample>
evaluate(const CompiledElectricalModel &model, const TransientSolveOptions &options,
         TransientEvaluation &evaluation, const TransientSample *previous,
         const std::vector<double> *initial, std::vector<TransientStorageDerivative> &derivatives) {
    auto &metrics = evaluation.metrics;
    auto &outcome = evaluation.outcome;
    auto &diagnostics = evaluation.diagnostics;
    const auto tableau = assemble_tableau(model, evaluation.time.value(), previous, initial,
                                          evaluation.duration.value(), diagnostics);
    if (!tableau)
        return std::nullopt;
    metrics.coordinate_count = static_cast<std::size_t>(tableau->coefficients.cols());
    const auto system = equilibrate(*tableau, diagnostics);
    if (!system) {
        outcome = TransientSolveOutcome::NumericalFailure;
        return std::nullopt;
    }

    auto scaled_solution = Vector{};
    if (system->coefficients.rows() == 0) {
        metrics.rank = 0U;
        metrics.reciprocal_condition = 1.0;
        metrics.scaled_residual = 0.0;
        scaled_solution = Vector::Zero(0);
    } else {
        ++evaluation.factorization_count;
        auto decomposition = Eigen::FullPivLU<Matrix>{system->coefficients};
        decomposition.setThreshold(options.relative_rank_threshold());
        if (!decomposition.matrixLU().allFinite()) {
            diagnostics.push_back(
                solve_diagnostic("TRANSIENT_SOLVE_NUMERICAL_FAILURE",
                                 "Transient solver factorization produced a nonfinite value"));
            outcome = TransientSolveOutcome::NumericalFailure;
            return std::nullopt;
        }
        const auto rank = static_cast<std::size_t>(decomposition.rank());
        metrics.rank = rank;
        if (rank != metrics.coordinate_count) {
            auto augmented = Matrix{system->coefficients.rows(), system->coefficients.cols() + 1};
            augmented.leftCols(system->coefficients.cols()) = system->coefficients;
            const auto rhs_maximum = max_abs(system->right_hand_side);
            augmented.rightCols(1) =
                rhs_maximum > 0.0 ? system->right_hand_side / rhs_maximum : system->right_hand_side;
            ++evaluation.factorization_count;
            auto augmented_decomposition = Eigen::FullPivLU<Matrix>{augmented};
            augmented_decomposition.setThreshold(options.relative_rank_threshold());
            if (!augmented.allFinite() || !augmented_decomposition.matrixLU().allFinite()) {
                diagnostics.push_back(solve_diagnostic(
                    "TRANSIENT_SOLVE_NUMERICAL_FAILURE",
                    "Transient solver augmented-rank classification produced a nonfinite value"));
                outcome = TransientSolveOutcome::NumericalFailure;
                return std::nullopt;
            }
            const auto augmented_rank = static_cast<std::size_t>(augmented_decomposition.rank());
            metrics.augmented_rank = augmented_rank;
            if (augmented_rank > rank) {
                diagnostics.push_back(solve_diagnostic(
                    initial ? "TRANSIENT_SOLVE_INCONSISTENT_INITIAL_STATE"
                            : "TRANSIENT_SOLVE_INCONSISTENT",
                    "Transient branch tableau is inconsistent at rank " + std::to_string(rank) +
                        " of " + std::to_string(metrics.coordinate_count),
                    model_entities(model)));
                outcome = initial ? TransientSolveOutcome::InconsistentInitialState
                                  : TransientSolveOutcome::Inconsistent;
            } else {
                diagnostics.push_back(solve_diagnostic(
                    initial ? "TRANSIENT_SOLVE_UNSUPPORTED_INITIALIZATION_TOPOLOGY"
                            : "TRANSIENT_SOLVE_RANK_DEFICIENT",
                    "Transient branch tableau does not uniquely determine all coordinates at "
                    "rank " +
                        std::to_string(rank) + " of " + std::to_string(metrics.coordinate_count),
                    model_entities(model)));
                outcome = initial ? TransientSolveOutcome::UnsupportedInitializationTopology
                                  : TransientSolveOutcome::RankDeficient;
            }
            return std::nullopt;
        }

        metrics.augmented_rank = rank;
        const auto reciprocal_condition = decomposition.rcond();
        if (!finite(reciprocal_condition)) {
            diagnostics.push_back(
                solve_diagnostic("TRANSIENT_SOLVE_NUMERICAL_FAILURE",
                                 "Transient solver reciprocal-condition estimate is nonfinite"));
            outcome = TransientSolveOutcome::NumericalFailure;
            return std::nullopt;
        }
        metrics.reciprocal_condition = reciprocal_condition;
        if (reciprocal_condition < options.minimum_reciprocal_condition()) {
            diagnostics.push_back(solve_diagnostic(
                "TRANSIENT_SOLVE_ILL_CONDITIONED",
                "Transient branch tableau reciprocal condition " +
                    number_text(reciprocal_condition) + " is below the accepted minimum " +
                    number_text(options.minimum_reciprocal_condition())));
            outcome = TransientSolveOutcome::IllConditioned;
            return std::nullopt;
        }

        ++evaluation.solve_count;
        scaled_solution = decomposition.solve(system->right_hand_side);
        if (!scaled_solution.allFinite()) {
            diagnostics.push_back(
                solve_diagnostic("TRANSIENT_SOLVE_NUMERICAL_FAILURE",
                                 "Transient solver produced a nonfinite equilibrated solution"));
            outcome = TransientSolveOutcome::NumericalFailure;
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
        diagnostics.push_back(solve_diagnostic(
            "TRANSIENT_SOLVE_NUMERICAL_FAILURE",
            "Transient solver coordinate reconstruction produced a nonfinite value"));
        outcome = TransientSolveOutcome::NumericalFailure;
        return std::nullopt;
    }

    const Vector scaled_difference =
        system->coefficients * scaled_solution - system->right_hand_side;
    const auto numerator = max_abs(scaled_difference);
    const auto denominator = static_cast<long double>(matrix_infinity_norm(system->coefficients)) *
                                 static_cast<long double>(max_abs(scaled_solution)) +
                             static_cast<long double>(max_abs(system->right_hand_side));
    const auto scaled_residual = denominator == 0.0L
                                     ? static_cast<long double>(numerator)
                                     : static_cast<long double>(numerator) / denominator;
    if (!scaled_difference.allFinite() || !std::isfinite(denominator) ||
        !std::isfinite(scaled_residual) || scaled_residual > std::numeric_limits<double>::max()) {
        diagnostics.push_back(solve_diagnostic(
            "TRANSIENT_SOLVE_NUMERICAL_FAILURE",
            "Transient solver scaled residual evaluation produced a nonfinite value"));
        outcome = TransientSolveOutcome::NumericalFailure;
        return std::nullopt;
    }
    metrics.scaled_residual = static_cast<double>(scaled_residual);

    TransientSample result{evaluation.time, {}, {}, {}};
    std::vector<double> potentials(model.nodes().size());
    for (const auto &node : model.nodes()) {
        if (const auto column = tableau->node_columns.at(node.id.index()))
            potentials.at(node.id.index()) = coordinates(static_cast<Eigen::Index>(*column));
        result.nodes.push_back(
            {node.id, Quantity{UnitDimension::Voltage, potentials.at(node.id.index())}});
    }
    for (const auto &branch : model.branches()) {
        const auto v = potentials.at(branch.from.index()) - potentials.at(branch.to.index());
        const auto i = coordinates(
            static_cast<Eigen::Index>(tableau->node_coordinate_count + branch.id.index()));
        const auto power = v * i;
        if (!finite(v) || !finite(i) || !finite(power)) {
            diagnostics.push_back(solve_diagnostic("TRANSIENT_SOLVE_NUMERICAL_FAILURE",
                                                   "Nonfinite branch reconstruction",
                                                   branch_entities(model, branch)));
            return std::nullopt;
        }
        result.branches.push_back({branch.id, Quantity{UnitDimension::Voltage, v},
                                   Quantity{UnitDimension::Current, i},
                                   Quantity{UnitDimension::Power, power}});
    }
    double voltage_residual = 0, current_residual = 0, voltage_ratio = 0, current_ratio = 0;
    bool residual_failed = false, numerical_failed = false;
    const auto record = [&](double difference, long double scale, bool voltage,
                            std::vector<EntityRef> entities) {
        const auto residual = std::abs(difference);
        const long double allowed = (voltage ? options.absolute_voltage_tolerance().value()
                                             : options.absolute_current_tolerance().value()) +
                                    options.relative_residual_tolerance() * scale;
        const long double ratio = residual / allowed;
        if (!finite(difference) || !std::isfinite(scale) || !std::isfinite(ratio) ||
            ratio > std::numeric_limits<double>::max()) {
            numerical_failed = true;
            return;
        }
        (voltage ? voltage_residual : current_residual) =
            std::max(voltage ? voltage_residual : current_residual, residual);
        (voltage ? voltage_ratio : current_ratio) =
            std::max(voltage ? voltage_ratio : current_ratio, static_cast<double>(ratio));
        if (ratio > 1) {
            residual_failed = true;
            diagnostics.push_back(solve_diagnostic("TRANSIENT_SOLVE_RESIDUAL_FAILURE",
                                                   voltage
                                                       ? "Original voltage law exceeds tolerance"
                                                       : "Original current law exceeds tolerance",
                                                   std::move(entities)));
        }
    };
    for (const auto &node : model.nodes()) {
        double sum = 0;
        long double scale = 0;
        for (const auto &term : node.incidence) {
            const auto current =
                term.sign * result.branches.at(term.branch.index()).current.value();
            sum += current;
            scale += std::abs(current);
        }
        std::vector<EntityRef> origin_entities;
        append_node_entities(model, node.id, origin_entities);
        record(sum, scale, false, std::move(origin_entities));
    }
    for (const auto &branch : model.branches()) {
        const auto v = result.branches.at(branch.id.index()).voltage.value();
        const auto i = result.branches.at(branch.id.index()).current.value();
        const auto origins = branch_entities(model, branch);
        std::visit(
            [&](const auto &law) {
                using Law = std::decay_t<decltype(law)>;
                if constexpr (std::same_as<Law, ResistanceElement>) {
                    const auto drop = law.parameter().nominal().value() * i;
                    record(v - drop, std::abs(v) + static_cast<long double>(std::abs(drop)), true,
                           origins);
                } else if constexpr (std::same_as<Law, CapacitanceElement>) {
                    const auto capacitance = law.parameter().nominal().value();
                    if (initial) {
                        const auto supplied = initial->at(branch.id.index());
                        record(v - supplied,
                               std::abs(v) + static_cast<long double>(std::abs(supplied)), true,
                               origins);
                        const auto derivative = i / capacitance;
                        derivatives.push_back({branch.id, derivative});
                        const auto current = capacitance * derivative;
                        record(i - current,
                               std::abs(i) + static_cast<long double>(std::abs(current)), false,
                               origins);
                    } else {
                        const auto previous_v =
                            previous->branches.at(branch.id.index()).voltage.value();
                        const auto current =
                            capacitance / evaluation.duration.value() * (v - previous_v);
                        // Include all original BE terms in the scale to account for subtraction
                        // cancellation.
                        const long double scale =
                            std::abs(i) +
                            static_cast<long double>(capacitance / evaluation.duration.value()) *
                                (std::abs(v) + static_cast<long double>(std::abs(previous_v)));
                        record(i - current, scale, false, origins);
                    }
                } else if constexpr (std::same_as<Law, InductanceElement>) {
                    const auto inductance = law.parameter().nominal().value();
                    if (initial) {
                        const auto supplied = initial->at(branch.id.index());
                        record(i - supplied,
                               std::abs(i) + static_cast<long double>(std::abs(supplied)), false,
                               origins);
                        const auto derivative = v / inductance;
                        derivatives.push_back({branch.id, derivative});
                        const auto drop = inductance * derivative;
                        record(v - drop, std::abs(v) + static_cast<long double>(std::abs(drop)),
                               true, origins);
                    } else {
                        const auto previous_i =
                            previous->branches.at(branch.id.index()).current.value();
                        const auto drop =
                            inductance / evaluation.duration.value() * (i - previous_i);
                        const long double scale =
                            std::abs(v) +
                            static_cast<long double>(inductance / evaluation.duration.value()) *
                                (std::abs(i) + static_cast<long double>(std::abs(previous_i)));
                        record(v - drop, scale, true, origins);
                    }
                } else if constexpr (std::same_as<Law, TransientVoltageSource> ||
                                     std::same_as<Law, TransientCurrentSource>) {
                    const auto value = law.waveform().value_at(evaluation.time).value();
                    constexpr auto voltage = std::same_as<Law, TransientVoltageSource>;
                    const auto actual = voltage ? v : i;
                    record(actual - value,
                           std::abs(actual) + static_cast<long double>(std::abs(value)), voltage,
                           origins);
                }
            },
            branch.law);
    }
    metrics.voltage_residual = Quantity{UnitDimension::Voltage, voltage_residual};
    metrics.current_residual = Quantity{UnitDimension::Current, current_residual};
    metrics.voltage_error_ratio = voltage_ratio;
    metrics.current_error_ratio = current_ratio;
    if (numerical_failed || std::ranges::any_of(derivatives, [](const auto &entry) {
            return !finite(entry.derivative);
        })) {
        diagnostics.push_back(
            solve_diagnostic("TRANSIENT_SOLVE_NUMERICAL_FAILURE",
                             "Nonfinite original electrical residual or initial derivative"));
        outcome = TransientSolveOutcome::NumericalFailure;
        return std::nullopt;
    }
    if (*metrics.scaled_residual > options.relative_residual_tolerance()) {
        residual_failed = true;
        diagnostics.push_back(solve_diagnostic("TRANSIENT_SOLVE_RESIDUAL_FAILURE",
                                               "Equilibrated tableau exceeds residual tolerance"));
    }
    if (residual_failed) {
        outcome = TransientSolveOutcome::ResidualFailure;
        return std::nullopt;
    }
    for (const auto &probe : model.probes()) {
        const auto value = std::visit(
            [&](const auto &target) -> Quantity {
                using Target = std::decay_t<decltype(target)>;
                if constexpr (std::same_as<Target, ElectricalVoltageObservation>)
                    return {UnitDimension::Voltage,
                            potentials.at(target.from.index()) - potentials.at(target.to.index())};
                else
                    return result.branches.at(target.branch.index()).current;
            },
            probe.target);
        result.probes.push_back({probe.key, value});
    }
    outcome = TransientSolveOutcome::Success;
    return result;
}

std::optional<double> temporal_error(const CompiledElectricalModel &model,
                                     const TransientSolveOptions &options,
                                     const TransientSample &previous, const TransientSample &full,
                                     const TransientSample &half) {
    double error = 0;
    for (const auto &storage : model.storage()) {
        const auto ratio = std::visit(
            [&](const auto &law) {
                using Law = std::decay_t<decltype(law)>;
                constexpr bool capacitor = std::same_as<Law, ElectricalCapacitorStorage>;
                const auto state = [&](const TransientSample &sample) {
                    const auto &branch = sample.branches.at(law.branch.index());
                    return capacitor ? branch.voltage.value() : branch.current.value();
                };
                const auto before = state(previous), one = state(full), two = state(half);
                const auto absolute = capacitor ? options.absolute_temporal_voltage().value()
                                                : options.absolute_temporal_current().value();
                const auto denominator =
                    absolute + options.relative_temporal_tolerance() *
                                   std::max({std::abs(before), std::abs(one), std::abs(two)});
                const auto difference = std::abs(two - one);
                if (!finite(denominator) || denominator <= 0 || !finite(difference))
                    return std::numeric_limits<double>::quiet_NaN();
                return difference / denominator;
            },
            storage);
        if (!finite(ratio))
            return std::nullopt;
        error = std::max(error, ratio);
    }
    return error;
}
} // namespace

TransientSolveOptions::TransientSolveOptions(Quantity minimum_step, Quantity initial_step,
                                             Quantity maximum_step, std::size_t maximum_trials,
                                             std::size_t maximum_accepted_steps,
                                             double relative_temporal_tolerance,
                                             Quantity absolute_temporal_voltage,
                                             Quantity absolute_temporal_current)
    : minimum_step_{minimum_step}, initial_step_{initial_step}, maximum_step_{maximum_step},
      maximum_trials_{maximum_trials}, maximum_accepted_steps_{maximum_accepted_steps},
      relative_temporal_{relative_temporal_tolerance}, temporal_voltage_{absolute_temporal_voltage},
      temporal_current_{absolute_temporal_current} {
    if (minimum_step.dimension() != UnitDimension::Time ||
        initial_step.dimension() != UnitDimension::Time ||
        maximum_step.dimension() != UnitDimension::Time || minimum_step.value() <= 0 ||
        minimum_step.value() > initial_step.value() ||
        initial_step.value() > maximum_step.value() || maximum_trials == 0 ||
        maximum_accepted_steps == 0 || !finite(relative_temporal_tolerance) ||
        relative_temporal_tolerance <= 0 || relative_temporal_tolerance >= 1 ||
        absolute_temporal_voltage.dimension() != UnitDimension::Voltage ||
        absolute_temporal_voltage.value() <= 0 ||
        absolute_temporal_current.dimension() != UnitDimension::Current ||
        absolute_temporal_current.value() <= 0)
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Transient options require ordered positive SI step bounds, "
                                  "positive work budgets and valid temporal tolerances"};
}

namespace detail {
TransientSubstep initialize_transient(const CompiledElectricalModel &model,
                                      const TransientSolveOptions &options) {
    TransientSubstep result{{Quantity{UnitDimension::Time, 0},
                             Quantity{UnitDimension::Time, 0},
                             TransientEvaluationKind::Initialization,
                             TransientSolveOutcome::NumericalFailure,
                             {},
                             {},
                             0,
                             0,
                             std::nullopt,
                             false},
                            std::nullopt,
                            {}};
    const auto initial = initial_storage(model);
    result.sample =
        evaluate(model, options, result.evaluation, nullptr, &initial, result.derivatives);
    return result;
}

TransientSubstep transient_be_substep(const CompiledElectricalModel &model,
                                      const TransientSolveOptions &options,
                                      const TransientSample &previous, Quantity time,
                                      TransientEvaluationKind kind) {
    const auto duration = time.value() - previous.time.value();
    TransientSubstep result{{time,
                             Quantity{UnitDimension::Time, duration},
                             kind,
                             TransientSolveOutcome::NumericalFailure,
                             {},
                             {},
                             0,
                             0,
                             std::nullopt,
                             false},
                            std::nullopt,
                            {}};
    if (!finite(duration) || duration <= 0) {
        result.evaluation.diagnostics.push_back(solve_diagnostic(
            "TRANSIENT_SOLVE_STEP_LIMIT", "BE substep makes no finite representable progress"));
        result.evaluation.outcome = TransientSolveOutcome::StepLimit;
        return result;
    }
    result.sample =
        evaluate(model, options, result.evaluation, &previous, nullptr, result.derivatives);
    return result;
}
} // namespace detail

TransientSolution::TransientSolution(ContentHash id, CompiledElectricalModel model,
                                     TransientSolveOptions options,
                                     std::vector<TransientSample> samples)
    : analysis_identity_{std::move(id)}, model_{std::move(model)}, options_{options},
      samples_{std::move(samples)} {}

class TransientSolution::Solver {
  public:
    static std::optional<TransientSolution>
    solve(const CompiledElectricalModel &model, const TransientSolveOptions &options,
          const ContentHash &identity, TransientSolveOutcome &outcome,
          std::vector<Diagnostic> &diagnostics, std::vector<TransientEvaluation> &evaluations,
          std::vector<TransientStorageDerivative> &derivatives,
          std::vector<TransientAcceptedHalfStep> &accepted, Quantity &last, std::size_t &trials,
          std::size_t &steps, std::size_t &solves, std::size_t &factorizations) {
        const auto retain = [&](TransientEvaluation evaluation) {
            solves += evaluation.solve_count;
            factorizations += evaluation.factorization_count;
            if (evaluation.outcome != TransientSolveOutcome::Success)
                outcome = evaluation.outcome;
            diagnostics.insert(diagnostics.end(), evaluation.diagnostics.begin(),
                               evaluation.diagnostics.end());
            evaluations.push_back(std::move(evaluation));
        };
        const auto fail = [&](TransientSolveOutcome reason, std::string_view code,
                              std::string message) -> std::optional<TransientSolution> {
            outcome = reason;
            diagnostics.push_back(solve_diagnostic(code, std::move(message)));
            return std::nullopt;
        };
        auto initialized = detail::initialize_transient(model, options);
        retain(std::move(initialized.evaluation));
        if (!initialized.sample)
            return std::nullopt;
        derivatives = std::move(initialized.derivatives);
        auto state = std::move(*initialized.sample);
        std::vector<TransientSample> samples{state};
        const auto &times = model.transient_request()->times();
        if (model.storage().empty()) {
            for (std::size_t index = 1; index < times.size(); ++index) {
                TransientEvaluation evaluation{times[index],
                                               Quantity{UnitDimension::Time, 0},
                                               TransientEvaluationKind::AlgebraicSample,
                                               TransientSolveOutcome::NumericalFailure,
                                               {},
                                               {},
                                               0,
                                               0,
                                               std::nullopt,
                                               false};
                std::vector<TransientStorageDerivative> unused;
                const auto algebraic =
                    evaluate(model, options, evaluation, nullptr, nullptr, unused);
                retain(std::move(evaluation));
                if (!algebraic)
                    return std::nullopt;
                samples.push_back(*algebraic);
                last = times[index];
            }
            outcome = TransientSolveOutcome::Success;
            return TransientSolution{identity, model, options, std::move(samples)};
        }
        std::vector<double> boundaries;
        for (const auto time : times)
            boundaries.push_back(time.value());
        for (const auto &source : model.transient_request()->sources())
            std::visit(
                [&](const auto &stimulus) {
                    for (const auto &knot : stimulus.waveform().knots())
                        boundaries.push_back(knot.time.value());
                },
                source);
        std::ranges::sort(boundaries);
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        double proposed = options.initial_step().value();
        std::size_t sample_index = 1;
        for (std::size_t boundary_index = 1; boundary_index < boundaries.size(); ++boundary_index) {
            const auto boundary = boundaries[boundary_index];
            while (state.time.value() < boundary) {
                if (trials >= options.maximum_trials() || steps >= options.maximum_accepted_steps())
                    return fail(TransientSolveOutcome::WorkLimit, "TRANSIENT_SOLVE_WORK_LIMIT",
                                "Explicit transient trial or accepted-step budget exhausted at t=" +
                                    number_text(last.value()));
                const auto start = state.time.value();
                const auto remaining = boundary - start;
                const auto trial_duration = std::min(proposed, remaining);
                const auto end = trial_duration == remaining ? boundary : start + trial_duration;
                const auto actual_duration = end - start;
                const auto midpoint = start + actual_duration / 2;
                if (!finite(end) || !finite(actual_duration) || actual_duration <= 0 ||
                    !finite(midpoint) || !(start < midpoint && midpoint < end))
                    return fail(TransientSolveOutcome::StepLimit, "TRANSIENT_SOLVE_STEP_LIMIT",
                                "No strict finite representable full/half progress at t=" +
                                    number_text(start));
                ++trials;
                auto full = detail::transient_be_substep(model, options, state,
                                                         Quantity{UnitDimension::Time, end},
                                                         TransientEvaluationKind::FullStep);
                retain(std::move(full.evaluation));
                if (!full.sample)
                    return std::nullopt;
                auto first = detail::transient_be_substep(model, options, state,
                                                          Quantity{UnitDimension::Time, midpoint},
                                                          TransientEvaluationKind::FirstHalf);
                const auto first_metrics = first.evaluation.metrics;
                retain(std::move(first.evaluation));
                if (!first.sample)
                    return std::nullopt;
                auto second = detail::transient_be_substep(model, options, *first.sample,
                                                           Quantity{UnitDimension::Time, end},
                                                           TransientEvaluationKind::SecondHalf);
                const auto second_metrics = second.evaluation.metrics;
                retain(std::move(second.evaluation));
                if (!second.sample)
                    return std::nullopt;
                const auto error =
                    temporal_error(model, options, state, *full.sample, *second.sample);
                if (!error)
                    return fail(TransientSolveOutcome::NumericalFailure,
                                "TRANSIENT_SOLVE_NUMERICAL_FAILURE",
                                "Nonfinite temporal error normalization at t=" + number_text(end));
                evaluations.back().normalized_error = *error;
                evaluations.back().trial_accepted = *error <= 1;
                const auto factor =
                    *error == 0 ? 2.0 : std::clamp(0.9 / std::sqrt(*error), 0.2, 2.0);
                const auto next = actual_duration * factor;
                if (!finite(next) || next <= 0)
                    return fail(TransientSolveOutcome::StepLimit, "TRANSIENT_SOLVE_STEP_LIMIT",
                                "Adaptive controller has no finite positive next step");
                if (*error > 1) {
                    if (actual_duration < options.minimum_step().value() ||
                        next < options.minimum_step().value())
                        return fail(
                            TransientSolveOutcome::StepLimit, "TRANSIENT_SOLVE_STEP_LIMIT",
                            "Temporal rejection needs a step below the explicit minimum at t=" +
                                number_text(start));
                    proposed = std::min(next, options.maximum_step().value());
                    continue;
                }
                accepted.push_back({Quantity{UnitDimension::Time, midpoint},
                                    Quantity{UnitDimension::Time, midpoint - start}, *error,
                                    first_metrics});
                accepted.push_back({Quantity{UnitDimension::Time, end},
                                    Quantity{UnitDimension::Time, end - midpoint}, *error,
                                    second_metrics});
                ++steps;
                state = std::move(*second.sample);
                last = state.time;
                proposed = std::clamp(next, options.minimum_step().value(),
                                      options.maximum_step().value());
            }
            if (sample_index < times.size() && boundary == times[sample_index].value()) {
                samples.push_back(state);
                ++sample_index;
            }
        }
        outcome = TransientSolveOutcome::Success;
        return TransientSolution{identity, model, options, std::move(samples)};
    }
};

TransientSolveReport::TransientSolveReport(const CompiledElectricalModel &model,
                                           const TransientSolveOptions &options)
    : model_{model}, options_{options}, analysis_identity_{make_analysis_identity(model, options)} {
    if (!model.transient_request())
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Transient solve requires a compiled transient request"};
    solution_ = TransientSolution::Solver::solve(
        model_, options_, analysis_identity_, outcome_, diagnostics_, evaluations_, derivatives_,
        accepted_half_steps_, last_accepted_time_, trial_count_, accepted_step_count_, solve_count_,
        factorization_count_);
}

TransientSolveReport solve_transient(const CompiledElectricalModel &model,
                                     const TransientSolveOptions &options) {
    return {model, options};
}
} // namespace volt
