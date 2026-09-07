#include <volt/electrical/dc_request.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

#include <volt/core/errors.hpp>

namespace volt {
namespace {

template <typename Value> const auto &variant_key(const Value &value) {
    return std::visit([](const auto &item) -> const auto & { return item.key(); }, value);
}

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

void require_input(const DcInputIdentity &expected, const DcInputIdentity &actual,
                   const char *message) {
    if (actual != expected) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation, message};
    }
}

[[nodiscard]] const DcSource *source(const DcRequest &request, const DcSourceKey &key) {
    const auto match = std::ranges::find(request.sources(), key, variant_key<DcSource>);
    return match == request.sources().end() ? nullptr : &*match;
}

[[nodiscard]] bool excluded(const DcRequest &request, ComponentId occurrence) {
    return std::ranges::any_of(request.exclusions(), [occurrence](const auto &entry) {
        return entry.occurrence().id() == occurrence;
    });
}

[[nodiscard]] bool model_has_element(const PartElectricalModel &model, const ModelElementKey &key) {
    return std::ranges::any_of(model.elements(), [&](const ModelElement &element) {
        return std::visit([&](const auto &value) { return value.key() == key; }, element);
    });
}

[[nodiscard]] Diagnostic analysis_error(std::string_view code, std::string message,
                                        std::vector<EntityRef> entities = {}) {
    return Diagnostic{Severity::Error, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Analysis}, std::move(message),
                      std::move(entities)};
}

} // namespace

template <typename Tag>
DcRequestKeyValue<Tag>::DcRequestKeyValue(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "DC request key must not be empty"};
    }
}

template class DcRequestKeyValue<DcRequestKeyTag>;
template class DcRequestKeyValue<DcSourceKeyTag>;
template class DcRequestKeyValue<DcProbeKeyTag>;

DcNetPair::DcNetPair(DcNetRef from, DcNetRef to) : from_{std::move(from)}, to_{std::move(to)} {
    require_input(from_.input(), to_.input(),
                  "DC request net pair combines references from different inputs");
}

DcVoltageSource::DcVoltageSource(DcSourceKey key, DcNetPair nets, Quantity value)
    : key_{std::move(key)}, nets_{std::move(nets)},
      value_{source_value(value, UnitDimension::Voltage,
                          "DC voltage source value must have Voltage dimension")} {
    if (nets_.from().id() == nets_.to().id()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "DC voltage source net endpoints must be distinct"};
    }
}

DcCurrentSource::DcCurrentSource(DcSourceKey key, DcNetPair nets, Quantity value)
    : key_{std::move(key)}, nets_{std::move(nets)},
      value_{source_value(value, UnitDimension::Current,
                          "DC current source value must have Current dimension")} {
    if (nets_.from().id() == nets_.to().id()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "DC current source net endpoints must be distinct"};
    }
}

DcReplacedByStimulusExclusion::DcReplacedByStimulusExclusion(std::vector<DcSourceKey> sources)
    : sources_{std::move(sources)} {
    if (sources_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "A replacement exclusion must name at least one source"};
    }
    canonicalize_unique(
        sources_, [](const DcSourceKey &key) { return key; },
        "A replacement exclusion must not repeat a source key");
}

DcRequest::DcRequest(DcRequestKey key, const DcInput &input, std::optional<DcNetRef> reference,
                     std::vector<DcSource> sources, std::vector<DcProbe> probes,
                     std::vector<DcOccurrenceExclusion> exclusions)
    : key_{std::move(key)}, input_{input}, reference_{std::move(reference)},
      sources_{std::move(sources)}, probes_{std::move(probes)}, exclusions_{std::move(exclusions)} {
    canonicalize_unique(sources_, variant_key<DcSource>, "DC request source keys must be unique");
    canonicalize_unique(probes_, variant_key<DcProbe>, "DC request probe keys must be unique");
    canonicalize_unique(
        exclusions_,
        [](const DcOccurrenceExclusion &entry) { return entry.occurrence().id().index(); },
        "A DC request may exclude an occurrence only once");

    const auto &identity = input_.identity();
    if (reference_.has_value()) {
        require_input(identity, reference_->input(),
                      "DC request reference belongs to another exact input");
    }
    for (const auto &entry : sources_) {
        std::visit(
            [&](const auto &value) {
                require_input(identity, value.nets().from().input(),
                              "DC request source belongs to another exact input");
            },
            entry);
    }
    for (const auto &entry : exclusions_) {
        require_input(identity, entry.occurrence().input(),
                      "DC request exclusion belongs to another exact input");
        if (const auto *replacement = std::get_if<DcReplacedByStimulusExclusion>(&entry.reason())) {
            for (const auto &source_key : replacement->sources()) {
                if (source(*this, source_key) == nullptr) {
                    throw KernelRangeError{ErrorCode::UnknownEntity,
                                           "DC replacement exclusion names an unknown source",
                                           EntityRef::component(entry.occurrence().id())};
                }
            }
        }
    }
    for (const auto &entry : probes_) {
        std::visit(
            [&](const auto &probe) {
                using Probe = std::decay_t<decltype(probe)>;
                if constexpr (std::same_as<Probe, DcVoltageProbe>) {
                    require_input(identity, probe.nets().from().input(),
                                  "DC voltage probe belongs to another exact input");
                } else if constexpr (std::same_as<Probe, DcSourceCurrentProbe>) {
                    if (source(*this, probe.source()) == nullptr) {
                        throw KernelRangeError{ErrorCode::UnknownEntity,
                                               "DC current probe names an unknown source"};
                    }
                } else {
                    require_input(identity, probe.occurrence().input(),
                                  "DC model probe belongs to another exact input");
                    const auto occurrence = probe.occurrence().id();
                    const auto *part = input_.part(occurrence);
                    if (part == nullptr || !part->electrical_model().has_value() ||
                        !model_has_element(*part->electrical_model(), probe.element()) ||
                        excluded(*this, occurrence)) {
                        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                               "DC current probe does not name a participating "
                                               "resolved model element",
                                               EntityRef::component(occurrence)};
                    }
                }
            },
            entry);
    }
}

DcOccurrenceCoverage::DcOccurrenceCoverage(DcOccurrenceRef occurrence, DcCoverageStatus status,
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
    const auto &input = request.input();
    const auto &circuit = input.circuit();

    if (!request.reference().has_value()) {
        diagnostics_.push_back(analysis_error(analysis_diagnostic_codes::DcReferenceMissing,
                                              "DC request has no explicit reference net"));
    }

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

    coverage_.reserve(circuit.all<ComponentId>().size());
    for (std::size_t index = 0; index < circuit.all<ComponentId>().size(); ++index) {
        const auto occurrence = ComponentId{index};
        const auto occurrence_ref = input.occurrence(occurrence);
        const auto exclusion = std::ranges::find_if(
            request.exclusions(), [occurrence](const DcOccurrenceExclusion &entry) {
                return entry.occurrence().id() == occurrence;
            });
        const auto &selection = circuit.get(occurrence).selected_library_part_ref();
        if (exclusion != request.exclusions().end()) {
            coverage_.emplace_back(occurrence_ref, DcCoverageStatus::Excluded, selection,
                                   *exclusion);
            continue;
        }
        if (!selection.has_value()) {
            coverage_.emplace_back(occurrence_ref, DcCoverageStatus::Unselected, std::nullopt,
                                   std::nullopt);
            diagnostics_.push_back(
                analysis_error(analysis_diagnostic_codes::DcOccurrenceUnselected,
                               "DC request occurrence has no selected exact Part",
                               {EntityRef::component(occurrence)}));
            continue;
        }
        const auto *part = input.part(occurrence);
        if (part == nullptr) {
            coverage_.emplace_back(occurrence_ref, DcCoverageStatus::Unresolved, selection,
                                   std::nullopt);
            diagnostics_.push_back(analysis_error(analysis_diagnostic_codes::DcOccurrenceUnresolved,
                                                  "DC request occurrence exact Part is unresolved",
                                                  {EntityRef::component(occurrence)}));
            continue;
        }
        if (!part->electrical_model().has_value()) {
            coverage_.emplace_back(occurrence_ref, DcCoverageStatus::ModelAbsent, selection,
                                   std::nullopt);
            diagnostics_.push_back(
                analysis_error(analysis_diagnostic_codes::DcOccurrenceModelAbsent,
                               "DC request occurrence exact Part has no electrical model",
                               {EntityRef::component(occurrence)}));
            continue;
        }
        coverage_.emplace_back(occurrence_ref, DcCoverageStatus::Supported, selection,
                               std::nullopt);
    }

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
