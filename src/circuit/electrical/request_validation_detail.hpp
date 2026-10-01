#pragma once
#include <algorithm>
#include <functional>
#include <type_traits>
#include <volt/core/errors.hpp>
#include <volt/electrical/dc_request.hpp>

namespace volt::detail {
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

inline void require_input(const ElectricalInputIdentity &expected,
                          const ElectricalInputIdentity &actual, const char *message) {
    if (actual != expected) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation, message};
    }
}

template <typename Request>
[[nodiscard]] const auto *source(const Request &request, const ElectricalSourceKey &key) {
    const auto match =
        std::ranges::find(request.sources(), key,
                          [](const auto &entry) -> const auto & { return variant_key(entry); });
    return match == request.sources().end() ? nullptr : &*match;
}

template <typename Request>
[[nodiscard]] bool excluded(const Request &request, ComponentId occurrence) {
    return std::ranges::any_of(request.exclusions(), [occurrence](const auto &entry) {
        return entry.occurrence().id() == occurrence;
    });
}

[[nodiscard]] inline bool model_has_element(const PartElectricalModel &model,
                                            const ModelElementKey &key) {
    return std::ranges::any_of(model.elements(), [&](const ModelElement &element) {
        return std::visit([&](const auto &value) { return value.key() == key; }, element);
    });
}

inline Diagnostic analysis_error(std::string_view code, std::string message,
                                 std::vector<EntityRef> entities = {}) {
    return Diagnostic{Severity::Error, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Analysis}, std::move(message),
                      std::move(entities)};
}

template <typename Sources>
void normalize_request(Sources &sources, std::vector<DcProbe> &probes,
                       std::vector<DcOccurrenceExclusion> &exclusions) {
    canonicalize_unique(
        sources, [](const auto &value) -> const auto & { return variant_key(value); },
        "Electrical request source keys must be unique");
    canonicalize_unique(probes, variant_key<DcProbe>,
                        "Electrical request probe keys must be unique");
    canonicalize_unique(
        exclusions,
        [](const DcOccurrenceExclusion &entry) { return entry.occurrence().id().index(); },
        "A Electrical request may exclude an occurrence only once");
}

template <typename Request> void validate_request(const Request &request) {
    const auto &identity = request.input().identity();
    if (request.reference().has_value()) {
        require_input(identity, request.reference()->input(),
                      "Electrical request reference belongs to another exact input");
    }
    for (const auto &entry : request.sources()) {
        std::visit(
            [&](const auto &value) {
                require_input(identity, value.nets().from().input(),
                              "Electrical request source belongs to another exact input");
            },
            entry);
    }
    for (const auto &entry : request.exclusions()) {
        require_input(identity, entry.occurrence().input(),
                      "Electrical request exclusion belongs to another exact input");
        if (const auto *replacement = std::get_if<DcReplacedByStimulusExclusion>(&entry.reason())) {
            for (const auto &source_key : replacement->sources()) {
                if (source(request, source_key) == nullptr) {
                    throw KernelRangeError{
                        ErrorCode::UnknownEntity,
                        "Electrical replacement exclusion names an unknown source",
                        EntityRef::component(entry.occurrence().id())};
                }
            }
        }
    }
    for (const auto &entry : request.probes()) {
        std::visit(
            [&](const auto &probe) {
                using Probe = std::decay_t<decltype(probe)>;
                if constexpr (std::same_as<Probe, DcVoltageProbe>) {
                    require_input(identity, probe.nets().from().input(),
                                  "Electrical voltage probe belongs to another exact input");
                } else if constexpr (std::same_as<Probe, DcSourceCurrentProbe>) {
                    if (source(request, probe.source()) == nullptr) {
                        throw KernelRangeError{ErrorCode::UnknownEntity,
                                               "Electrical current probe names an unknown source"};
                    }
                } else {
                    require_input(identity, probe.occurrence().input(),
                                  "Electrical model probe belongs to another exact input");
                    const auto occurrence = probe.occurrence().id();
                    const auto *part = request.input().part(occurrence);
                    if (part == nullptr || !part->electrical_model().has_value() ||
                        !model_has_element(*part->electrical_model(), probe.element()) ||
                        excluded(request, occurrence)) {
                        throw KernelLogicError{
                            ErrorCode::CrossReferenceViolation,
                            "Electrical current probe does not name a participating "
                            "resolved model element",
                            EntityRef::component(occurrence)};
                    }
                }
            },
            entry);
    }
}

template <typename Request>
void assess_participation(const Request &request, std::string_view analysis,
                          std::vector<DcOccurrenceCoverage> &coverage,
                          std::vector<Diagnostic> &diagnostics) {
    const auto &input = request.input();
    const auto &circuit = input.circuit();
    coverage.reserve(circuit.template all<ComponentId>().size());
    for (std::size_t index = 0; index < circuit.template all<ComponentId>().size(); ++index) {
        const auto occurrence = ComponentId{index};
        const auto occurrence_ref = input.occurrence(occurrence);
        const auto exclusion = std::ranges::find_if(
            request.exclusions(), [occurrence](const DcOccurrenceExclusion &entry) {
                return entry.occurrence().id() == occurrence;
            });
        const auto &selection = circuit.get(occurrence).selected_library_part_ref();
        if (exclusion != request.exclusions().end()) {
            coverage.emplace_back(occurrence_ref, DcCoverageStatus::Excluded, selection,
                                  *exclusion);
            continue;
        }
        if (!selection.has_value()) {
            coverage.emplace_back(occurrence_ref, DcCoverageStatus::Unselected, std::nullopt,
                                  std::nullopt);
            diagnostics.push_back(analysis_error(
                std::string{analysis} + "_OCCURRENCE_UNSELECTED",
                std::string{analysis} + " request occurrence has no selected exact Part",
                {EntityRef::component(occurrence)}));
            continue;
        }
        const auto *part = input.part(occurrence);
        if (part == nullptr) {
            coverage.emplace_back(occurrence_ref, DcCoverageStatus::Unresolved, selection,
                                  std::nullopt);
            diagnostics.push_back(analysis_error(std::string{analysis} + "_OCCURRENCE_UNRESOLVED",
                                                 std::string{analysis} +
                                                     " request occurrence exact Part is unresolved",
                                                 {EntityRef::component(occurrence)}));
            continue;
        }
        if (!part->electrical_model().has_value()) {
            coverage.emplace_back(occurrence_ref, DcCoverageStatus::ModelAbsent, selection,
                                  std::nullopt);
            diagnostics.push_back(analysis_error(
                std::string{analysis} + "_OCCURRENCE_MODEL_ABSENT",
                std::string{analysis} + " request occurrence exact Part has no electrical model",
                {EntityRef::component(occurrence)}));
            continue;
        }
        coverage.emplace_back(occurrence_ref, DcCoverageStatus::Supported, selection, std::nullopt);
    }
}
} // namespace volt::detail
