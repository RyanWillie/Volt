#include <volt/electrical/ac_request.hpp>

#include "request_validation_detail.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <volt/core/errors.hpp>

namespace volt {
namespace {
void require(bool valid, const char *message) {
    if (!valid)
        throw KernelArgumentError{ErrorCode::InvalidArgument, message};
}

void frequency(Quantity value) {
    require(value.dimension() == UnitDimension::Frequency && value.value() > 0.0,
            "AC frequency must be a strictly positive SI frequency");
    require(std::isfinite(2.0 * std::numbers::pi * value.value()),
            "AC angular frequency must be finite");
}

AcFrequencySweep materialize(Quantity start, Quantity stop, std::size_t count, bool logarithmic) {
    frequency(start);
    frequency(stop);
    require(count >= 2 && start.value() < stop.value(),
            "AC sweep requires ascending endpoints and count >= 2");
    std::vector<Quantity> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const double t = static_cast<double>(index) / static_cast<double>(count - 1);
        const double sample =
            index == 0           ? start.value()
            : index == count - 1 ? stop.value()
            : logarithmic ? std::exp(std::lerp(std::log(start.value()), std::log(stop.value()), t))
                          : std::lerp(start.value(), stop.value(), t);
        values.emplace_back(UnitDimension::Frequency, sample);
    }
    return AcFrequencySweep{std::move(values)};
}
} // namespace

AcFrequencySweep::AcFrequencySweep(std::vector<Quantity> values) : frequencies_{std::move(values)} {
    require(!frequencies_.empty(), "AC sweep must contain at least one frequency");
    double previous = 0.0;
    for (const auto value : frequencies_) {
        frequency(value);
        require(value.value() > previous, "AC frequencies must be ascending and unique");
        previous = value.value();
    }
}

AcFrequencySweep AcFrequencySweep::linear(Quantity start, Quantity stop, std::size_t count) {
    return materialize(start, stop, count, false);
}

AcFrequencySweep AcFrequencySweep::logarithmic(Quantity start, Quantity stop, std::size_t count) {
    return materialize(start, stop, count, true);
}

template <UnitDimension Dimension>
AcIndependentSource<Dimension>::AcIndependentSource(ElectricalSourceKey key, ElectricalNetPair nets,
                                                    Quantity amplitude, double phase)
    : key_{std::move(key)}, nets_{std::move(nets)},
      amplitude_{Dimension, amplitude.value() == 0.0 ? 0.0 : amplitude.value()},
      phase_{amplitude.value() == 0.0 || phase == 0.0 ? 0.0 : phase} {
    require(amplitude.dimension() == Dimension && amplitude.value() >= 0.0,
            "AC source amplitude has the wrong dimension or is negative");
    require(std::isfinite(phase), "AC source phase must be finite radians");
    require(nets_.from().id() != nets_.to().id(), "AC source endpoints must be distinct");
}
template class AcIndependentSource<UnitDimension::Voltage>;
template class AcIndependentSource<UnitDimension::Current>;

AcRequest::AcRequest(ElectricalRequestKey key, const ElectricalInput &input,
                     std::optional<ElectricalNetRef> reference, AcFrequencySweep sweep,
                     std::vector<AcSource> sources, std::vector<DcProbe> probes,
                     std::vector<DcOccurrenceExclusion> exclusions, std::vector<AcGainProbe> gains,
                     std::vector<AcImpedanceProbe> impedances)
    : key_{std::move(key)}, input_{input}, reference_{std::move(reference)},
      sweep_{std::move(sweep)}, sources_{std::move(sources)}, probes_{std::move(probes)},
      exclusions_{std::move(exclusions)}, gains_{std::move(gains)},
      impedances_{std::move(impedances)} {
    detail::normalize_request(sources_, probes_, exclusions_);
    detail::validate_request(*this);
    std::ranges::sort(sources_, {}, [](const AcSource &value) {
        return std::visit([](const auto &source) { return source.key(); }, value);
    });
    std::map<ElectricalProbeKey, UnitDimension> primitive_dimensions;
    std::set<ElectricalProbeKey> keys;
    for (const auto &probe : probes_) {
        std::visit(
            [&](const auto &value) {
                keys.insert(value.key());
                primitive_dimensions.emplace(
                    value.key(), std::same_as<std::decay_t<decltype(value)>, DcVoltageProbe>
                                     ? UnitDimension::Voltage
                                     : UnitDimension::Current);
            },
            probe);
    }
    std::ranges::sort(gains_, {}, &AcGainProbe::key);
    std::ranges::sort(impedances_, {}, &AcImpedanceProbe::key);
    for (const auto &gain : gains_) {
        require(keys.insert(gain.key()).second, "AC observation keys must be unique");
        const auto numerator = primitive_dimensions.find(gain.numerator());
        const auto denominator = primitive_dimensions.find(gain.denominator());
        require(numerator != primitive_dimensions.end() &&
                    denominator != primitive_dimensions.end(),
                "AC gain must name two primitive probes");
        require(numerator->second == denominator->second,
                "AC gain probes must have the same dimension");
    }
    for (const auto &impedance : impedances_) {
        require(keys.insert(impedance.key()).second, "AC observation keys must be unique");
        require(impedance.nets().from().input() == input_.identity(),
                "AC impedance port belongs to another input");
        require(impedance.nets().from().id() != impedance.nets().to().id(),
                "AC impedance port endpoints must be distinct");
        const auto match = std::ranges::find_if(sources_, [&](const auto &source) {
            return std::visit([&](const auto &value) { return value.key() == impedance.source(); },
                              source);
        });
        require(match != sources_.end(), "AC impedance names an unknown source");
        const auto *source = std::get_if<AcCurrentSource>(&*match);
        require(source != nullptr && source->nets() == impedance.nets(),
                "AC impedance requires a current test source in the port orientation");
    }
}

AcRequestAssessment::AcRequestAssessment(const AcRequest &request)
    : input_{request.input().identity()}, complete_{false} {
    if (!request.reference())
        diagnostics_.push_back(detail::analysis_error("AC_REQUEST_REFERENCE_MISSING",
                                                      "AC request has no explicit reference net"));
    detail::assess_participation(request, "AC", coverage_, diagnostics_);
    if (request.sources().empty()) {
        diagnostics_.emplace_back(
            Severity::Error,
            DiagnosticCode{std::string{analysis_diagnostic_codes::AcExcitationMissing}},
            DiagnosticCategory{diagnostic_categories::Analysis},
            "AC request has no explicit source assignments");
    }
    const auto nonzero = std::ranges::count_if(request.sources(), [](const auto &source) {
        return std::visit([](const auto &value) { return value.amplitude().value() != 0.0; },
                          source);
    });
    if (!request.gains().empty() && nonzero != 1) {
        diagnostics_.emplace_back(
            Severity::Error,
            DiagnosticCode{std::string{analysis_diagnostic_codes::AcTransferStimulusInvalid}},
            DiagnosticCategory{diagnostic_categories::Analysis},
            "AC gain requires exactly one nonzero source assignment");
    }
    for (const auto &impedance : request.impedances()) {
        const auto match = std::ranges::find_if(request.sources(), [&](const auto &source) {
            return std::visit([&](const auto &value) { return value.key() == impedance.source(); },
                              source);
        });
        if (nonzero != 1 || std::get<AcCurrentSource>(*match).amplitude().value() == 0.0) {
            diagnostics_.emplace_back(
                Severity::Error,
                DiagnosticCode{std::string{analysis_diagnostic_codes::AcImpedanceStimulusInvalid}},
                DiagnosticCategory{diagnostic_categories::Analysis},
                "AC impedance requires its nonzero current test source and all other sources zero");
        }
    }
    complete_ = std::ranges::none_of(diagnostics_,
                                     [](const Diagnostic &diagnostic) {
                                         return diagnostic.severity() == Severity::Error;
                                     }) &&
                std::ranges::all_of(coverage_, [](const DcOccurrenceCoverage &entry) {
                    return entry.status() == DcCoverageStatus::Supported ||
                           entry.status() == DcCoverageStatus::Excluded;
                });
}

AcRequestAssessment assess_ac_request(const AcRequest &request) {
    return AcRequestAssessment{request};
}
} // namespace volt
