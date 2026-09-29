#include <Eigen/Dense>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>
#include <volt/core/errors.hpp>
#include <volt/electrical/ac_request.hpp>
#include <volt/electrical/ac_solve.hpp>

namespace volt {
namespace {
using Complex = std::complex<double>;
using Matrix = Eigen::MatrixXcd;
using Vector = Eigen::VectorXcd;
static_assert(EIGEN_MAJOR_VERSION == 5 && EIGEN_MINOR_VERSION == 0 && EIGEN_PATCH_VERSION == 0);
constexpr auto backend_name =
    std::string_view{"eigen-5.0.0-full-piv-lu-complex-double-dense-branch-tableau"};

[[nodiscard]] std::string number_text(double value) {
    auto buffer = std::array<char, 64>{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                      std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) {
        throw KernelLogicError{ErrorCode::InvalidState, "Failed to encode AC solver number"};
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
                                                 const AcSolveOptions &options,
                                                 const AcSolveProvenance &provenance) {
    auto encoder = IdentityEncoder{};
    encoder.text("volt.linear-ac-analysis");
    encoder.text(std::to_string(AcSolveReport::contract_version()));
    encoder.text(model.identity().value());
    encoder.text(provenance.backend);
    encoder.text(AcSolveOptions::scaling());
    encoder.number(options.relative_rank_threshold());
    encoder.number(options.minimum_reciprocal_condition());
    encoder.number(options.relative_residual_tolerance());
    encoder.number(options.absolute_voltage_tolerance().value());
    encoder.number(options.absolute_current_tolerance().value());
    if (provenance.deck_identity) {
        encoder.text(provenance.backend_version);
        encoder.text(provenance.adapter);
        encoder.text(std::to_string(provenance.adapter_contract_version));
        encoder.text(provenance.effective_settings);
        encoder.text(provenance.acceptance_policy);
        encoder.text(provenance.validation_backend);
        encoder.text(provenance.deck_identity->value());
        encoder.text(provenance.mapping_identity->value());
    }
    return encoder.digest();
}

[[nodiscard]] AcSolveProvenance native_provenance() {
    return AcSolveProvenance{std::string{backend_name},
                             "5.0.0",
                             "volt.native-linear-ac",
                             1,
                             std::string{AcSolveOptions::scaling()},
                             "volt.linear-ac-unique-finite-residual:1",
                             std::string{backend_name},
                             std::nullopt,
                             std::nullopt};
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

bool finite(double value) { return std::isfinite(value); }

bool finite(Complex value) {
    return finite(value.real()) && finite(value.imag()) && finite(std::abs(value));
}

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

[[nodiscard]] std::optional<Tableau> assemble_tableau(const CompiledElectricalModel &model,
                                                      double omega,
                                                      std::vector<Diagnostic> &diagnostics) {
    const auto node_count = model.nodes().size();
    const auto branch_count = model.branches().size();
    if (node_count == 0U || model.reference().index() >= node_count ||
        node_count - 1U > std::numeric_limits<std::size_t>::max() - branch_count) {
        diagnostics.push_back(
            solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                             "AC solver cannot construct coordinates for the compiled model"));
        return std::nullopt;
    }
    const auto node_coordinates = node_count - 1U;
    const auto coordinate_count = node_coordinates + branch_count;
    if (coordinate_count > static_cast<std::size_t>(std::numeric_limits<Eigen::Index>::max())) {
        diagnostics.push_back(
            solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                             "AC solver coordinate count exceeds the numerical backend limit"));
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

    const auto add_node_coefficient = [&](Eigen::Index row, ElectricalNodeId node, Complex value) {
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
                    add_node_coefficient(law_row, branch.from,
                                         Complex{0, -omega * law.parameter().nominal().value()});
                    add_node_coefficient(law_row, branch.to,
                                         Complex{0, omega * law.parameter().nominal().value()});
                    result.coefficients(law_row, current_column) = 1.0;
                } else if constexpr (std::same_as<Law, InductanceElement>) {
                    add_node_coefficient(law_row, branch.from, 1.0);
                    add_node_coefficient(law_row, branch.to, -1.0);
                    result.coefficients(law_row, current_column) =
                        Complex{0, -omega * law.parameter().nominal().value()};
                } else if constexpr (std::same_as<Law, AcVoltageSource>) {
                    add_node_coefficient(law_row, branch.from, 1.0);
                    add_node_coefficient(law_row, branch.to, -1.0);
                    result.right_hand_side(law_row) = law.phasor();
                } else if constexpr (std::same_as<Law, AcCurrentSource>) {
                    result.coefficients(law_row, current_column) = 1.0;
                    result.right_hand_side(law_row) = law.phasor();
                } else {
                    throw KernelArgumentError{ErrorCode::InvalidArgument,
                                              "AC model contains a DC source"};
                }
            },
            branch.law);
    }

    if (!result.coefficients.allFinite() || !result.right_hand_side.allFinite()) {
        diagnostics.push_back(solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                                               "AC solver assembled a nonfinite branch tableau"));
        return std::nullopt;
    }
    return result;
}

struct EquilibratedSystem {
    Matrix coefficients;
    Vector right_hand_side;
    Eigen::VectorXd column_maxima;
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
    Eigen::VectorXd column_maxima = Eigen::VectorXd::Zero(coefficients.cols());
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
            solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                             "AC solver equilibration produced a nonfinite value"));
        return std::nullopt;
    }
    return EquilibratedSystem{std::move(coefficients), std::move(right_hand_side),
                              std::move(column_maxima)};
}

std::optional<AcFrequencyResult> solve_point(const CompiledElectricalModel &model,
                                             const AcSolveOptions &options, AcPointReport &point) {
    auto &metrics = point.metrics;
    auto &outcome = point.outcome;
    auto &diagnostics = point.diagnostics;
    const double omega = 2.0 * std::numbers::pi * point.frequency.value();
    if (!finite(omega)) {
        diagnostics.push_back(solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                                               "Nonfinite angular frequency"));
        return std::nullopt;
    }
    const auto tableau = assemble_tableau(model, omega, diagnostics);
    if (!tableau)
        return std::nullopt;
    metrics.coordinate_count = static_cast<std::size_t>(tableau->coefficients.cols());
    const auto system = equilibrate(*tableau, diagnostics);
    if (!system) {
        outcome = AcSolveOutcome::NumericalFailure;
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
                solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                                 "AC solver factorization produced a nonfinite value"));
            outcome = AcSolveOutcome::NumericalFailure;
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
            auto augmented_decomposition = Eigen::FullPivLU<Matrix>{augmented};
            augmented_decomposition.setThreshold(options.relative_rank_threshold());
            if (!augmented.allFinite() || !augmented_decomposition.matrixLU().allFinite()) {
                diagnostics.push_back(solve_diagnostic(
                    analysis_diagnostic_codes::AcSolveNumericalFailure,
                    "AC solver augmented-rank classification produced a nonfinite value"));
                outcome = AcSolveOutcome::NumericalFailure;
                return std::nullopt;
            }
            const auto augmented_rank = static_cast<std::size_t>(augmented_decomposition.rank());
            metrics.augmented_rank = augmented_rank;
            if (augmented_rank > rank) {
                diagnostics.push_back(solve_diagnostic(
                    analysis_diagnostic_codes::AcSolveInconsistent,
                    "AC branch tableau is inconsistent at rank " + std::to_string(rank) + " of " +
                        std::to_string(metrics.coordinate_count)));
                outcome = AcSolveOutcome::Inconsistent;
            } else {
                diagnostics.push_back(solve_diagnostic(
                    analysis_diagnostic_codes::AcSolveRankDeficient,
                    "AC branch tableau does not uniquely determine all coordinates at rank " +
                        std::to_string(rank) + " of " + std::to_string(metrics.coordinate_count)));
                outcome = AcSolveOutcome::RankDeficient;
            }
            return std::nullopt;
        }

        metrics.augmented_rank = rank;
        const auto reciprocal_condition = decomposition.rcond();
        if (!finite(reciprocal_condition)) {
            diagnostics.push_back(
                solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                                 "AC solver reciprocal-condition estimate is nonfinite"));
            outcome = AcSolveOutcome::NumericalFailure;
            return std::nullopt;
        }
        metrics.reciprocal_condition = reciprocal_condition;
        if (reciprocal_condition < options.minimum_reciprocal_condition()) {
            diagnostics.push_back(solve_diagnostic(
                analysis_diagnostic_codes::AcSolveIllConditioned,
                "AC branch tableau reciprocal condition " + number_text(reciprocal_condition) +
                    " is below the accepted minimum " +
                    number_text(options.minimum_reciprocal_condition())));
            outcome = AcSolveOutcome::IllConditioned;
            return std::nullopt;
        }

        scaled_solution = decomposition.solve(system->right_hand_side);
        if (!scaled_solution.allFinite()) {
            diagnostics.push_back(
                solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                                 "AC solver produced a nonfinite equilibrated solution"));
            outcome = AcSolveOutcome::NumericalFailure;
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
            solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                             "AC solver coordinate reconstruction produced a nonfinite value"));
        outcome = AcSolveOutcome::NumericalFailure;
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
        diagnostics.push_back(
            solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                             "AC solver scaled residual evaluation produced a nonfinite value"));
        outcome = AcSolveOutcome::NumericalFailure;
        return std::nullopt;
    }
    metrics.scaled_residual = static_cast<double>(scaled_residual);

    AcFrequencyResult result{point.frequency, {}, {}, {}};
    auto potentials = std::vector<Complex>(model.nodes().size());
    for (const auto &node : model.nodes()) {
        if (const auto column = tableau->node_columns.at(node.id.index()))
            potentials.at(node.id.index()) = coordinates(static_cast<Eigen::Index>(*column));
        if (!finite(potentials.at(node.id.index()))) {
            diagnostics.push_back(
                solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                                 "AC node potential or magnitude is nonfinite at node:" +
                                     std::to_string(node.id.index())));
            return std::nullopt;
        }
        const auto v = potentials.at(node.id.index());
        result.nodes.push_back(
            {node.id, AcComplexQuantity{UnitDimension::Voltage, v.real(), v.imag()}});
    }
    for (const auto &branch : model.branches()) {
        const auto v = potentials.at(branch.from.index()) - potentials.at(branch.to.index());
        const auto i = coordinates(
            static_cast<Eigen::Index>(tableau->node_coordinate_count + branch.id.index()));
        if (!finite(v) || !finite(i)) {
            diagnostics.push_back(
                solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                                 "AC branch observation or magnitude is nonfinite at branch:" +
                                     std::to_string(branch.id.index()),
                                 branch_entities(model, branch)));
            return std::nullopt;
        }
        result.branches.push_back({branch.id,
                                   AcComplexQuantity{UnitDimension::Voltage, v.real(), v.imag()},
                                   AcComplexQuantity{UnitDimension::Current, i.real(), i.imag()}});
    }
    double voltage_residual = 0, current_residual = 0, voltage_ratio = 0, current_ratio = 0;
    bool residual_failed = false, numerical_failed = false;
    const auto record = [&](Complex difference, long double scale, bool voltage,
                            std::vector<EntityRef> entities) {
        const long double residual = std::abs(difference);
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
            std::max(voltage ? voltage_residual : current_residual, static_cast<double>(residual));
        (voltage ? voltage_ratio : current_ratio) =
            std::max(voltage ? voltage_ratio : current_ratio, static_cast<double>(ratio));
        if (ratio > 1) {
            residual_failed = true;
            diagnostics.push_back(
                solve_diagnostic(analysis_diagnostic_codes::AcSolveResidualFailure,
                                 voltage ? "Original complex voltage law exceeds tolerance"
                                         : "Original complex current law exceeds tolerance",
                                 std::move(entities)));
        }
    };
    for (const auto &node : model.nodes()) {
        Complex sum{};
        long double scale = 0;
        for (const auto &term : node.incidence) {
            const auto i = static_cast<double>(term.sign) *
                           result.branches.at(term.branch.index()).current.value();
            sum += i;
            scale += std::abs(i);
        }
        std::vector<EntityRef> entities;
        append_node_entities(model, node.id, entities);
        record(sum, scale, false, std::move(entities));
    }
    for (const auto &branch : model.branches()) {
        const auto v = result.branches.at(branch.id.index()).voltage.value();
        const auto i = result.branches.at(branch.id.index()).current.value();
        std::visit(
            [&](const auto &law) {
                using Law = std::decay_t<decltype(law)>;
                if constexpr (std::same_as<Law, ResistanceElement>) {
                    const auto drop = law.parameter().nominal().value() * i;
                    record(v - drop, std::abs(v) + static_cast<long double>(std::abs(drop)), true,
                           branch_entities(model, branch));
                } else if constexpr (std::same_as<Law, CapacitanceElement>) {
                    const auto current = Complex{0, omega * law.parameter().nominal().value()} * v;
                    record(i - current, std::abs(i) + static_cast<long double>(std::abs(current)),
                           false, branch_entities(model, branch));
                } else if constexpr (std::same_as<Law, InductanceElement>) {
                    const auto drop = Complex{0, omega * law.parameter().nominal().value()} * i;
                    record(v - drop, std::abs(v) + static_cast<long double>(std::abs(drop)), true,
                           branch_entities(model, branch));
                } else if constexpr (std::same_as<Law, AcVoltageSource>)
                    record(v - law.phasor(),
                           std::abs(v) + static_cast<long double>(std::abs(law.phasor())), true,
                           branch_entities(model, branch));
                else if constexpr (std::same_as<Law, AcCurrentSource>)
                    record(i - law.phasor(),
                           std::abs(i) + static_cast<long double>(std::abs(law.phasor())), false,
                           branch_entities(model, branch));
            },
            branch.law);
    }
    metrics.voltage_residual = Quantity{UnitDimension::Voltage, voltage_residual};
    metrics.current_residual = Quantity{UnitDimension::Current, current_residual};
    metrics.voltage_error_ratio = voltage_ratio;
    metrics.current_error_ratio = current_ratio;
    if (numerical_failed) {
        diagnostics.push_back(
            solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                             "AC original-unit residual cannot be evaluated finitely"));
        outcome = AcSolveOutcome::NumericalFailure;
        return std::nullopt;
    }
    if (*metrics.scaled_residual > options.relative_residual_tolerance()) {
        diagnostics.push_back(
            solve_diagnostic(analysis_diagnostic_codes::AcSolveResidualFailure,
                             "Equilibrated AC tableau exceeds its scaled residual tolerance"));
        residual_failed = true;
    }
    if (residual_failed) {
        outcome = AcSolveOutcome::ResidualFailure;
        return std::nullopt;
    }
    for (const auto &probe : model.probes()) {
        const auto value = std::visit(
            [&](const auto &target) -> std::optional<AcComplexQuantity> {
                using Target = std::decay_t<decltype(target)>;
                if constexpr (std::same_as<Target, ElectricalVoltageObservation>) {
                    const auto v =
                        potentials.at(target.from.index()) - potentials.at(target.to.index());
                    if (!finite(v))
                        return std::nullopt;
                    return AcComplexQuantity{UnitDimension::Voltage, v.real(), v.imag()};
                } else
                    return result.branches.at(target.branch.index()).current;
            },
            probe.target);
        if (!value) {
            diagnostics.push_back(solve_diagnostic(
                analysis_diagnostic_codes::AcSolveNumericalFailure,
                "AC probe '" + probe.key.value() + "' cannot be reconstructed finitely"));
            return std::nullopt;
        }
        result.probes.push_back({probe.key, *value});
    }
    const auto publish_ratio = [&](DcProbeKey key, Complex numerator, Complex denominator,
                                   UnitDimension dimension) {
        if (denominator == Complex{}) {
            outcome = AcSolveOutcome::UndefinedMeasurement;
            diagnostics.push_back(solve_diagnostic(
                analysis_diagnostic_codes::AcSolveUndefinedMeasurement,
                "Derived AC observation '" + key.value() + "' has a zero denominator"));
            return false;
        }
        const auto value = numerator / denominator;
        if (!finite(value)) {
            outcome = AcSolveOutcome::NumericalFailure;
            diagnostics.push_back(
                solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                                 "Derived AC observation '" + key.value() + "' is nonfinite"));
            return false;
        }
        result.probes.push_back({key, AcComplexQuantity{dimension, value.real(), value.imag()}});
        return true;
    };
    for (const auto &gain : model.ac_request()->gains()) {
        const auto probe_value = [&](const DcProbeKey &key) {
            const auto found = std::ranges::find(result.probes, key, &AcProbeResult::key);
            return found->value.value();
        };
        if (!publish_ratio(gain.key(), probe_value(gain.numerator()),
                           probe_value(gain.denominator()), UnitDimension::Ratio))
            return std::nullopt;
    }
    for (const auto &impedance : model.ac_request()->impedances()) {
        const auto node_for = [&](const DcNetRef &net) {
            for (const auto &node : model.nodes()) {
                if (const auto *origin = std::get_if<ElectricalNetOrigin>(&node.origin)) {
                    if (std::ranges::find(origin->nets, net.id()) != origin->nets.end())
                        return node.id;
                }
            }
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Compiled AC impedance port has no node"};
        };
        const auto source =
            std::ranges::find_if(model.branches(), [&](const ElectricalBranch &branch) {
                const auto *key = std::get_if<DcSourceKey>(&branch.origin);
                return key && *key == impedance.source();
            });
        const auto voltage = potentials.at(node_for(impedance.nets().from()).index()) -
                             potentials.at(node_for(impedance.nets().to()).index());
        if (!publish_ratio(impedance.key(), voltage,
                           -result.branches.at(source->id.index()).current.value(),
                           UnitDimension::Resistance))
            return std::nullopt;
    }
    std::ranges::sort(result.probes, {}, &AcProbeResult::key);
    outcome = AcSolveOutcome::Success;
    return result;
}
} // namespace

AcComplexQuantity::AcComplexQuantity(UnitDimension dimension, double real, double imaginary)
    : dimension_{dimension}, value_{real, imaginary} {
    if ((dimension != UnitDimension::Voltage && dimension != UnitDimension::Current &&
         dimension != UnitDimension::Resistance && dimension != UnitDimension::Ratio) ||
        !finite(value_))
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Complex observation and magnitude must be finite"};
}

std::optional<double> AcComplexQuantity::phase() const noexcept {
    return magnitude() == 0 ? std::nullopt : std::optional{std::arg(value_)};
}

AcSolution::AcSolution(ContentHash id, CompiledElectricalModel model, AcSolveOptions options,
                       AcSolveProvenance provenance, std::vector<AcFrequencyResult> points)
    : analysis_identity_{std::move(id)}, model_{std::move(model)}, options_{options},
      provenance_{std::move(provenance)}, points_{std::move(points)} {}

class AcSolution::Solver {
  public:
    static std::optional<AcSolution> solve(const CompiledElectricalModel &model,
                                           const AcSolveOptions &options, const ContentHash &id,
                                           const AcSolveProvenance &provenance,
                                           std::vector<AcPointReport> &reports,
                                           AcSolveOutcome &outcome,
                                           std::vector<Diagnostic> &diagnostics) {
        std::vector<AcFrequencyResult> results;
        outcome = AcSolveOutcome::Success;
        for (const auto frequency : model.ac_request()->frequencies()) {
            AcPointReport point{frequency, AcSolveOutcome::NumericalFailure, {}, {}};
            auto result = solve_point(model, options, point);
            if (!result) {
                if (outcome == AcSolveOutcome::Success)
                    outcome = point.outcome;
                if (point.diagnostics.empty())
                    point.diagnostics.push_back(
                        solve_diagnostic(analysis_diagnostic_codes::AcSolveNumericalFailure,
                                         "Nonfinite complex observation or residual"));
            } else
                results.push_back(std::move(*result));
            diagnostics.insert(diagnostics.end(), point.diagnostics.begin(),
                               point.diagnostics.end());
            reports.push_back(std::move(point));
        }
        if (outcome != AcSolveOutcome::Success)
            return std::nullopt;
        return AcSolution{id, model, options, provenance, std::move(results)};
    }
};

AcSolveReport::AcSolveReport(const CompiledElectricalModel &model, const AcSolveOptions &options)
    : model_{model}, options_{options}, provenance_{native_provenance()},
      analysis_identity_{make_analysis_identity(model_, options_, provenance_)} {
    if (!model_.ac_request())
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "AC solve requires a compiled AC request"};
    solution_ = AcSolution::Solver::solve(model_, options_, analysis_identity_, provenance_,
                                          points_, outcome_, diagnostics_);
}

AcSolveReport solve_ac(const CompiledElectricalModel &model, const AcSolveOptions &options) {
    return AcSolveReport{model, options};
}
} // namespace volt
