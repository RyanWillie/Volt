#include <volt/electrical/dc_request.hpp>

#include "request_validation_detail.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

#include <volt/core/errors.hpp>

namespace volt {
namespace {

template <typename Range, typename Key>
void canonicalize_unique(Range &values, Key key, const char *message) {
    std::ranges::sort(values, {}, key);
    if (std::ranges::adjacent_find(values, [&](const auto &lhs, const auto &rhs) {
            return std::invoke(key, lhs) == std::invoke(key, rhs);
        }) != values.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName, message};
    }
}

[[nodiscard]] Quantity source_value(Quantity value, UnitDimension expected, const char *message) {
    if (value.dimension() != expected) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, message};
    }
    return Quantity{expected, value.value() == 0.0 ? 0.0 : value.value()};
}

void require_input(const ElectricalInputIdentity &expected, const ElectricalInputIdentity &actual,
                   const char *message) {
    if (actual != expected) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation, message};
    }
}

[[nodiscard]] Diagnostic analysis_error(std::string_view code, std::string message,
                                        std::vector<EntityRef> entities = {}) {
    return Diagnostic{Severity::Error, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Analysis}, std::move(message),
                      std::move(entities)};
}

} // namespace

DcVoltageSource::DcVoltageSource(ElectricalSourceKey key, ElectricalNetPair nets, Quantity value)
    : key_{std::move(key)}, nets_{std::move(nets)},
      value_{source_value(value, UnitDimension::Voltage,
                          "DC voltage source value must have Voltage dimension")} {
    if (nets_.from().id() == nets_.to().id()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "DC voltage source net endpoints must be distinct"};
    }
}

DcCurrentSource::DcCurrentSource(ElectricalSourceKey key, ElectricalNetPair nets, Quantity value)
    : key_{std::move(key)}, nets_{std::move(nets)},
      value_{source_value(value, UnitDimension::Current,
                          "DC current source value must have Current dimension")} {
    if (nets_.from().id() == nets_.to().id()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "DC current source net endpoints must be distinct"};
    }
}

DcReplacedByStimulusExclusion::DcReplacedByStimulusExclusion(
    std::vector<ElectricalSourceKey> sources)
    : sources_{std::move(sources)} {
    if (sources_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "A replacement exclusion must name at least one source"};
    }
    canonicalize_unique(
        sources_, [](const ElectricalSourceKey &key) { return key; },
        "A replacement exclusion must not repeat a source key");
}

DcRequest::DcRequest(ElectricalRequestKey key, const ElectricalInput &input,
                     std::optional<ElectricalNetRef> reference, std::vector<DcSource> sources,
                     std::vector<DcProbe> probes, std::vector<DcOccurrenceExclusion> exclusions)
    : key_{std::move(key)}, input_{input}, reference_{std::move(reference)},
      sources_{std::move(sources)}, probes_{std::move(probes)}, exclusions_{std::move(exclusions)} {
    detail::normalize_request(sources_, probes_, exclusions_);
    detail::validate_request(*this);
}

DcOccurrenceCoverage::DcOccurrenceCoverage(ElectricalOccurrenceRef occurrence,
                                           DcCoverageStatus status,
                                           std::optional<LibraryPartRef> selected_part,
                                           std::optional<DcOccurrenceExclusion> exclusion)
    : occurrence_{std::move(occurrence)}, status_{status}, selected_part_{std::move(selected_part)},
      exclusion_{std::move(exclusion)} {
    if (exclusion_.has_value()) {
        require_input(occurrence_.input(), exclusion_->occurrence().input(),
                      "DC coverage exclusion belongs to another exact input");
        if (exclusion_->occurrence().id() != occurrence_.id()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "DC coverage exclusion names another occurrence"};
        }
    }
    const auto is_excluded = status_ == DcCoverageStatus::Excluded;
    const auto is_unselected = status_ == DcCoverageStatus::Unselected;
    if (is_excluded != exclusion_.has_value() ||
        (is_unselected ? selected_part_.has_value()
                       : (!is_excluded && !selected_part_.has_value()))) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "DC occurrence coverage payload does not match its status"};
    }
}

DcRequestAssessment::DcRequestAssessment(const DcRequest &request)
    : input_{request.input().identity()}, complete_{true} {
    if (!request.reference())
        diagnostics_.push_back(analysis_error(analysis_diagnostic_codes::DcReferenceMissing,
                                              "DC request has no explicit reference net"));

    auto voltage_constraints = std::map<std::pair<std::size_t, std::size_t>, double>{};
    auto contradictory_pairs = std::set<std::pair<std::size_t, std::size_t>>{};
    for (const auto &entry : request.sources()) {
        const auto *voltage = std::get_if<DcVoltageSource>(&entry);
        if (voltage == nullptr) {
            continue;
        }
        const auto from = voltage->nets().from().id().index();
        const auto to = voltage->nets().to().id().index();
        const auto pair = std::minmax(from, to);
        const auto canonical_value =
            from < to ? voltage->value().value() : -voltage->value().value();
        const auto [match, inserted] = voltage_constraints.emplace(pair, canonical_value);
        if (!inserted && match->second != canonical_value &&
            contradictory_pairs.insert(pair).second) {
            diagnostics_.push_back(analysis_error(
                analysis_diagnostic_codes::DcContradictoryVoltageSources,
                "DC request contains unequal ideal voltage constraints over one authored net pair",
                {EntityRef::net(NetId{pair.first}), EntityRef::net(NetId{pair.second})}));
        }
    }

    detail::assess_participation(request, "DC", coverage_, diagnostics_);

    complete_ = std::ranges::none_of(diagnostics_, [](const Diagnostic &diagnostic) {
        return diagnostic.severity() == Severity::Error;
    });
    if (complete_) {
        complete_ = std::ranges::all_of(coverage_, [](const DcOccurrenceCoverage &entry) {
            return entry.status() == DcCoverageStatus::Supported ||
                   entry.status() == DcCoverageStatus::Excluded;
        });
    }
}

DcRequestAssessment assess_dc_request(const DcRequest &request) {
    return DcRequestAssessment{request};
}

} // namespace volt
