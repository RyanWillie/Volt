#include <volt/electrical/dc_solve.hpp>
#include <volt/electrical/transient_request.hpp>

#include "request_validation_detail.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace volt {
namespace {
void require(bool valid, const char *message) {
    if (!valid)
        throw KernelArgumentError{ErrorCode::InvalidArgument, message};
}

std::string time_text(double value) {
    std::array<char, 64> buffer{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                      std::chars_format::general, std::numeric_limits<double>::max_digits10);
    return std::string{buffer.data(), result.ptr};
}

void time_value(Quantity time) {
    require(time.dimension() == UnitDimension::Time && time.value() >= 0.0,
            "Transient time must be nonnegative typed SI seconds");
}

void source_dimension(UnitDimension dimension) {
    require(dimension == UnitDimension::Voltage || dimension == UnitDimension::Current,
            "Transient waveform must use voltage or current dimension");
}

template <typename Request> ContentHash participation_identity(const Request &request) {
    std::string bytes = "volt.storage-participation.v1\n";
    bytes += request.input().identity().logical().value() + "\n";
    bytes += request.input().identity().selected_parts().value() + "\n";
    for (std::size_t index = 0;
         index < request.input().circuit().template all<ComponentId>().size(); ++index) {
        const auto occurrence = ComponentId{index};
        bytes += std::to_string(occurrence.index()) +
                 (detail::excluded(request, occurrence) ? ":excluded\n" : ":included\n");
    }
    return sha256_content_hash(bytes);
}

std::optional<TransientStorageKind> storage_kind(const ModelElement &element) {
    if (std::holds_alternative<CapacitanceElement>(element))
        return TransientStorageKind::CapacitorVoltage;
    if (std::holds_alternative<InductanceElement>(element))
        return TransientStorageKind::InductorCurrent;
    return std::nullopt;
}

const ModelElementKey &element_key(const ModelElement &element) {
    return std::visit([](const auto &value) -> const ModelElementKey & { return value.key(); },
                      element);
}

std::size_t storage_count(const TransientRequest &request) {
    std::size_t count = 0;
    for (std::size_t index = 0;
         index < request.input().circuit().template all<ComponentId>().size(); ++index) {
        const auto occurrence = ComponentId{index};
        if (detail::excluded(request, occurrence))
            continue;
        const auto *part = request.input().part(occurrence);
        if (!part || !part->electrical_model())
            continue;
        for (const auto &element : part->electrical_model()->elements())
            count += storage_kind(element).has_value();
    }
    return count;
}
} // namespace

TransientTimeGrid::TransientTimeGrid(std::vector<Quantity> times) : times_{std::move(times)} {
    require(times_.size() >= 2, "Transient output requires at least two samples");
    require(times_.front().value() == 0.0, "Transient output must begin at zero");
    double previous = -1.0;
    for (auto &time : times_) {
        time_value(time);
        require(time.value() > previous, "Transient output times must be strictly increasing");
        time = Quantity{UnitDimension::Time, time.value() == 0.0 ? 0.0 : time.value()};
        previous = time.value();
    }
}

TransientTimeGrid TransientTimeGrid::uniform(Quantity horizon, std::size_t count) {
    time_value(horizon);
    require(horizon.value() > 0.0 && count >= 2,
            "Transient uniform output needs a positive horizon and count >= 2");
    std::vector<Quantity> times;
    times.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        times.emplace_back(UnitDimension::Time,
                           index == count - 1 ? horizon.value()
                                              : horizon.value() * (static_cast<double>(index) /
                                                                   static_cast<double>(count - 1)));
    return TransientTimeGrid{std::move(times)};
}

TransientWaveform::TransientWaveform(Quantity constant)
    : knots_{{Quantity{UnitDimension::Time, 0.0},
              Quantity{constant.dimension(), constant.value() == 0.0 ? 0.0 : constant.value()}}} {
    source_dimension(constant.dimension());
}

TransientWaveform::TransientWaveform(std::vector<TransientWaveformKnot> knots)
    : knots_{std::move(knots)} {
    require(knots_.size() >= 2, "Transient PWL needs at least two knots");
    source_dimension(knots_.front().value.dimension());
    require(knots_.front().time.value() == 0.0, "Transient PWL must begin at zero");
    double previous = -1.0;
    for (auto &knot : knots_) {
        time_value(knot.time);
        require(knot.time.value() > previous, "Transient PWL knots must be strictly increasing");
        require(knot.value.dimension() == dimension(), "Transient PWL dimensions must match");
        knot.time =
            Quantity{UnitDimension::Time, knot.time.value() == 0.0 ? 0.0 : knot.time.value()};
        knot.value = Quantity{dimension(), knot.value.value() == 0.0 ? 0.0 : knot.value.value()};
        previous = knot.time.value();
    }
}

Quantity TransientWaveform::value_at(Quantity time) const {
    time_value(time);
    const auto upper = std::ranges::upper_bound(knots_, time.value(), {},
                                                [](const auto &knot) { return knot.time.value(); });
    if (upper == knots_.end())
        return knots_.back().value;
    const auto &lower = *(upper - 1);
    const double fraction =
        (time.value() - lower.time.value()) / (upper->time.value() - lower.time.value());
    return Quantity{dimension(), std::lerp(lower.value.value(), upper->value.value(), fraction)};
}

template <UnitDimension Dimension>
TransientIndependentSource<Dimension>::TransientIndependentSource(ElectricalSourceKey key,
                                                                  ElectricalNetPair nets,
                                                                  TransientWaveform waveform)
    : key_{std::move(key)}, nets_{std::move(nets)}, waveform_{std::move(waveform)} {
    require(waveform_.dimension() == Dimension, "Transient source waveform dimension is wrong");
    require(nets_.from().id() != nets_.to().id(), "Transient source endpoints must be distinct");
}
template class TransientIndependentSource<UnitDimension::Voltage>;
template class TransientIndependentSource<UnitDimension::Current>;

TransientInitialState::TransientInitialState(ElectricalOccurrenceRef occurrence,
                                             LibraryPartRef part, ModelElementKey element,
                                             TransientStorageKind kind, Quantity value)
    : occurrence_{std::move(occurrence)}, part_{std::move(part)}, element_{std::move(element)},
      kind_{kind}, value_{value.dimension(), value.value() == 0.0 ? 0.0 : value.value()} {
    require(kind_ == TransientStorageKind::CapacitorVoltage ||
                kind_ == TransientStorageKind::InductorCurrent,
            "Unknown transient storage kind");
    require(value_.dimension() == (kind_ == TransientStorageKind::CapacitorVoltage
                                       ? UnitDimension::Voltage
                                       : UnitDimension::Current),
            "Transient initial storage dimension does not match its kind");
}

TransientInitialConditions::TransientInitialConditions(
    std::vector<TransientInitialState> entries, std::optional<TransientDcProvenance> dc_provenance)
    : entries_{std::move(entries)}, dc_provenance_{std::move(dc_provenance)} {
    detail::canonicalize_unique(
        entries_,
        [](const auto &entry) {
            return std::pair{entry.occurrence().id().index(), entry.element()};
        },
        "Transient storage target occurs more than once");
    if (dc_provenance_) {
        require(dc_provenance_->storage_count == entries_.size(),
                "DC-derived storage collection does not match its complete result mapping");
        for (const auto &entry : entries_)
            detail::require_input(dc_provenance_->input, entry.occurrence().input(),
                                  "DC-derived storage belongs to another exact input");
    }
}

TransientRequest::TransientRequest(ElectricalRequestKey key, const ElectricalInput &input,
                                   std::optional<ElectricalNetRef> reference,
                                   TransientTimeGrid grid, std::vector<TransientSource> sources,
                                   std::vector<DcProbe> probes,
                                   std::vector<DcOccurrenceExclusion> exclusions,
                                   TransientInitialConditions initial_conditions)
    : key_{std::move(key)}, input_{input}, reference_{std::move(reference)}, grid_{std::move(grid)},
      sources_{std::move(sources)}, probes_{std::move(probes)}, exclusions_{std::move(exclusions)},
      initial_conditions_{std::move(initial_conditions)} {
    detail::normalize_request(sources_, probes_, exclusions_);
    detail::validate_request(*this);
    for (const auto &source : sources_)
        std::visit(
            [&](const auto &value) {
                require(value.waveform().knots().back().time.value() <= grid_.horizon().value(),
                        "Transient PWL knot exceeds the output horizon");
            },
            source);
    for (const auto &entry : initial_state()) {
        detail::require_input(input_.identity(), entry.occurrence().input(),
                              "Transient storage target belongs to another exact input");
        const auto occurrence = entry.occurrence().id();
        const auto &selected = input_.circuit().get(occurrence).selected_library_part_ref();
        const auto *part = input_.part(occurrence);
        require(selected && *selected == entry.part() && part && part->electrical_model() &&
                    !detail::excluded(*this, occurrence),
                "Transient storage target is stale, unresolved or excluded");
        const auto &elements = part->electrical_model()->elements();
        const auto match = std::ranges::find(elements, entry.element(), element_key);
        require(match != elements.end() && storage_kind(*match) == entry.kind(),
                "Transient initial state does not name the requested storage kind");
    }
    if (initial_conditions_.dc_provenance()) {
        const auto &provenance = *initial_conditions_.dc_provenance();
        detail::require_input(input_.identity(), provenance.input,
                              "DC-derived initial state belongs to another exact input");
        require(provenance.participation_identity == participation_identity(*this) &&
                    provenance.storage_count == storage_count(*this) &&
                    provenance.storage_count == initial_state().size(),
                "DC-derived initial state has changed participation or incomplete storage mapping");
    }
}

TransientRequestAssessment::TransientRequestAssessment(const TransientRequest &request)
    : input_{request.input().identity()} {
    if (!request.reference())
        diagnostics_.push_back(
            detail::analysis_error("TRANSIENT_REQUEST_REFERENCE_MISSING",
                                   "Transient request has no explicit reference net"));
    detail::assess_participation(request, "TRANSIENT", coverage_, diagnostics_);
    for (const auto &coverage : coverage_) {
        if (coverage.status() != DcCoverageStatus::Supported)
            continue;
        const auto occurrence = coverage.occurrence().id();
        for (const auto &element :
             request.input().part(occurrence)->electrical_model()->elements()) {
            if (!storage_kind(element))
                continue;
            const auto found = std::ranges::any_of(request.initial_state(), [&](const auto &entry) {
                return entry.occurrence().id() == occurrence &&
                       entry.element() == element_key(element);
            });
            if (!found)
                diagnostics_.push_back(detail::analysis_error(
                    "TRANSIENT_INITIAL_STATE_MISSING",
                    "Transient request lacks initial storage for model element '" +
                        element_key(element).value() + "'",
                    {EntityRef::component(occurrence)}));
        }
    }
    std::set<double> knots{0.0, request.grid().horizon().value()};
    for (const auto &source : request.sources())
        std::visit(
            [&](const auto &value) {
                for (const auto &knot : value.waveform().knots())
                    knots.insert(knot.time.value());
            },
            source);
    std::map<std::pair<std::size_t, std::size_t>, const TransientVoltageSource *> constraints;
    std::set<std::pair<std::size_t, std::size_t>> contradicted;
    for (const auto &source : request.sources()) {
        const auto *voltage = std::get_if<TransientVoltageSource>(&source);
        if (!voltage)
            continue;
        const auto from = voltage->nets().from().id().index();
        const auto to = voltage->nets().to().id().index();
        const auto pair = std::pair{std::min(from, to), std::max(from, to)};
        const auto [prior, inserted] = constraints.emplace(pair, voltage);
        if (inserted)
            continue;
        for (const auto time : knots) {
            const auto oriented = [&](const TransientVoltageSource &value) {
                return (value.nets().from().id().index() < value.nets().to().id().index() ? 1.0
                                                                                          : -1.0) *
                       value.waveform().value_at(Quantity{UnitDimension::Time, time}).value();
            };
            if (oriented(*prior->second) != oriented(*voltage) && contradicted.insert(pair).second)
                diagnostics_.push_back(detail::analysis_error(
                    "TRANSIENT_CONTRADICTORY_VOLTAGE_SOURCES",
                    "Unequal parallel transient voltage constraints at time " + time_text(time) +
                        " s",
                    {EntityRef::net(NetId{pair.first}), EntityRef::net(NetId{pair.second})}));
        }
    }
    complete_ = std::ranges::none_of(
        diagnostics_, [](const auto &value) { return value.severity() == Severity::Error; });
}

TransientRequestAssessment assess_transient_request(const TransientRequest &request) {
    return TransientRequestAssessment{request};
}

TransientInitialConditions initial_state_from(const DcSolution &solution) {
    const auto &model = solution.model();
    const auto provenance =
        TransientDcProvenance{model.input().identity(), solution.analysis_identity(),
                              participation_identity(model.request()), model.storage().size()};
    require(solution.branches().size() == model.branches().size(),
            "DC solution does not contain complete branch observations");
    std::vector<TransientInitialState> result;
    for (const auto &storage : model.storage())
        std::visit(
            [&](const auto &value) {
                const auto &origin = std::get<ElectricalElementOrigin>(
                    model.branches().at(value.branch.index()).origin);
                const auto &observation = solution.branches().at(value.branch.index());
                require(observation.branch == value.branch,
                        "DC storage observation mapping is incomplete");
                constexpr bool capacitor =
                    std::same_as<std::decay_t<decltype(value)>, ElectricalCapacitorStorage>;
                result.emplace_back(model.input().occurrence(origin.occurrence), origin.part,
                                    origin.element,
                                    capacitor ? TransientStorageKind::CapacitorVoltage
                                              : TransientStorageKind::InductorCurrent,
                                    capacitor ? observation.voltage : observation.current);
            },
            storage);
    detail::canonicalize_unique(
        result,
        [](const auto &entry) {
            return std::pair{entry.occurrence().id().index(), entry.element()};
        },
        "DC storage observation mapping is not one-to-one");
    return TransientInitialConditions{std::move(result), provenance};
}

TransientInitialConditions initial_state_from(const DcSolveReport &report) {
    require(report.success() && report.solution(),
            "Cannot copy storage from an unsuccessful DC report");
    return initial_state_from(*report.solution());
}
} // namespace volt
