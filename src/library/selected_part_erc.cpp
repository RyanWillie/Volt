#include <volt/library/part_library.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/errors.hpp>

namespace volt {

namespace {

constexpr auto accepted_voltage_rule = "volt.part_erc.voltage.accepted_range@1";
constexpr auto absolute_voltage_rule = "volt.part_erc.voltage.absolute_limit@1";
constexpr auto absolute_current_rule = "volt.part_erc.current.absolute_limit@1";
constexpr auto current_budget_rule = "volt.part_erc.current.continuous_budget@1";
constexpr auto subject_projection_rule = "volt.part_erc.subject_projection@1";

struct Domain {
    NetId positive;
    NetId negative;

    [[nodiscard]] auto key() const noexcept {
        return std::pair{positive.index(), negative.index()};
    }
};

struct Bounds {
    std::optional<double> minimum;
    std::optional<double> maximum;
};

struct ClaimKey {
    ComponentId component;
    Domain domain;
    ElectricalObservable observable;
    ElectricalMeaning meaning;

    [[nodiscard]] auto key() const noexcept {
        return std::tuple{domain.positive.index(), domain.negative.index(),
                          static_cast<int>(observable), static_cast<int>(meaning),
                          component.index()};
    }
};

struct Claim {
    ComponentId component;
    Domain domain;
    ElectricalObservable observable;
    ElectricalMeaning meaning;
    std::optional<Bounds> bounds;
    std::optional<double> continuous_current;
    bool has_unknown = false;
    bool has_unconditional = false;
    std::string provenance;
};

[[nodiscard]] bool operator<(const ClaimKey &lhs, const ClaimKey &rhs) noexcept {
    return lhs.key() < rhs.key();
}

[[nodiscard]] NetId canonical_net(const Circuit &circuit,
                                  const detail::NetContinuityView &continuity, NetId net) {
    for (std::size_t index = 0; index < circuit.all<NetId>().size(); ++index) {
        const auto candidate = NetId{index};
        if (continuity.same_group(candidate, net)) {
            return candidate;
        }
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Net continuity group has no canonical net"};
}

[[nodiscard]] std::optional<NetId> project_pin(const Circuit &circuit,
                                               const detail::NetContinuityView &continuity,
                                               const std::vector<PinId> &pins,
                                               ElectricalPinIndex index) {
    if (index.value() >= pins.size()) {
        return std::nullopt;
    }
    const auto net = queries::net_of(circuit, pins[index.value()]);
    if (!net.has_value()) {
        return std::nullopt;
    }
    return canonical_net(circuit, continuity, *net);
}

[[nodiscard]] std::optional<Domain> project_subject(const Circuit &circuit,
                                                    const detail::NetContinuityView &continuity,
                                                    ComponentId component,
                                                    const ElectricalSubject &subject) {
    const auto pins = queries::pins_for(circuit, component);
    auto positive = std::vector<ElectricalPinIndex>{};
    auto negative = std::vector<ElectricalPinIndex>{};
    switch (subject.kind()) {
    case ElectricalSubjectKind::FramedPin: {
        const auto &framed = subject.as_framed_pin();
        positive.push_back(framed.pin);
        negative.push_back(framed.reference);
        break;
    }
    case ElectricalSubjectKind::DirectedRelation: {
        const auto &relation = subject.as_directed_relation();
        positive.push_back(relation.from);
        negative.push_back(relation.to);
        break;
    }
    case ElectricalSubjectKind::SupplyDomain: {
        const auto &supply = subject.as_supply_domain();
        positive = supply.positive_pins();
        negative = supply.return_pins();
        break;
    }
    }

    const auto projected_positive = project_pin(circuit, continuity, pins, positive.front());
    const auto projected_negative = project_pin(circuit, continuity, pins, negative.front());
    if (!projected_positive.has_value() || !projected_negative.has_value() ||
        *projected_positive == *projected_negative) {
        return std::nullopt;
    }
    const auto same_projected_net = [&](ElectricalPinIndex index, NetId expected) {
        const auto projected = project_pin(circuit, continuity, pins, index);
        return projected.has_value() && *projected == expected;
    };
    if (!std::ranges::all_of(
            positive, [&](auto index) { return same_projected_net(index, *projected_positive); }) ||
        !std::ranges::all_of(
            negative, [&](auto index) { return same_projected_net(index, *projected_negative); })) {
        return std::nullopt;
    }
    return Domain{*projected_positive, *projected_negative};
}

[[nodiscard]] Bounds bounds(const QuantityRange &range) {
    return Bounds{range.minimum().has_value() ? std::optional<double>{range.minimum()->value()}
                                              : std::nullopt,
                  range.maximum().has_value() ? std::optional<double>{range.maximum()->value()}
                                              : std::nullopt};
}

void intersect(Bounds &target, const Bounds &candidate, bool &conflict) {
    if (candidate.minimum.has_value() &&
        (!target.minimum.has_value() || *candidate.minimum > *target.minimum)) {
        target.minimum = candidate.minimum;
    }
    if (candidate.maximum.has_value() &&
        (!target.maximum.has_value() || *candidate.maximum < *target.maximum)) {
        target.maximum = candidate.maximum;
    }
    conflict = target.minimum.has_value() && target.maximum.has_value() &&
               *target.minimum > *target.maximum;
}

[[nodiscard]] std::string part_provenance(const PartDefinition &part,
                                          const LibraryPartRef &reference) {
    auto result = part.identity().namespace_name() + "/" + part.identity().name() + "@" +
                  part.identity().version() + " [" + reference.part_digest().value() + "]";
    if (!part.provenance().datasheet().empty()) {
        result += " from " + part.provenance().datasheet();
    }
    return result;
}

[[nodiscard]] Diagnostic erc_diagnostic(std::string_view code, std::string message,
                                        std::vector<EntityRef> entities, std::string rule) {
    return Diagnostic{Severity::Error,
                      DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Erc},
                      std::move(message),
                      std::move(entities),
                      {},
                      std::nullopt,
                      std::move(rule)};
}

[[nodiscard]] std::vector<EntityRef> pair_entities(ComponentId consumer, ComponentId provider,
                                                   Domain domain) {
    return {EntityRef::component(consumer), EntityRef::component(provider),
            EntityRef::net(domain.positive), EntityRef::net(domain.negative)};
}

void add_range_claim(Claim &claim, const Bounds &candidate) {
    if (!claim.bounds.has_value()) {
        claim.bounds = candidate;
        return;
    }
    auto conflict = false;
    intersect(*claim.bounds, candidate, conflict);
    if (conflict) {
        claim.bounds.reset();
        claim.has_unknown = true;
    }
}

void add_record(Claim &claim, const ElectricalRecord &record) {
    claim.has_unconditional = claim.has_unconditional || record.conditions().empty();
    if (record.value().kind() == ElectricalValueKind::Unknown) {
        claim.has_unknown = true;
        return;
    }
    if (record.meaning() == ElectricalMeaning::Requirement ||
        record.meaning() == ElectricalMeaning::Capability) {
        const auto value = record.value().as_continuous_current().value().value();
        if (!claim.continuous_current.has_value()) {
            claim.continuous_current = value;
        } else if (record.meaning() == ElectricalMeaning::Requirement) {
            claim.continuous_current = std::max(*claim.continuous_current, value);
        } else {
            claim.continuous_current = std::min(*claim.continuous_current, value);
        }
        return;
    }
    add_range_claim(claim, bounds(record.value().as_range()));
}

[[nodiscard]] bool below(const Bounds &provided, const Bounds &accepted) {
    return provided.minimum.has_value() && accepted.minimum.has_value() &&
           *provided.minimum < *accepted.minimum;
}

[[nodiscard]] bool above(const Bounds &provided, const Bounds &accepted) {
    return provided.maximum.has_value() && accepted.maximum.has_value() &&
           *provided.maximum > *accepted.maximum;
}

} // namespace

DiagnosticReport validate_selected_part_erc(const Circuit &circuit, const PartLibrary &library) {
    const auto continuity = detail::NetContinuityView{circuit};
    auto claims = std::map<ClaimKey, Claim>{};
    auto unresolved_current = std::vector<ComponentId>{};
    auto report = DiagnosticReport{};

    for (std::size_t index = 0; index < circuit.all<ComponentId>().size(); ++index) {
        const auto component = ComponentId{index};
        const auto &reference = queries::selected_library_part_ref(circuit, component);
        if (!reference.has_value()) {
            continue;
        }
        const auto &part = library.resolve(*reference);
        const auto &instance = circuit.get(component);
        if (part.implemented_component() != circuit.get(instance.definition()).content_identity()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Selected exact part implements a different component contract",
                                   EntityRef::component(component)};
        }
        const auto provenance = part_provenance(part, *reference);
        auto subject_unresolved = false;
        auto current_unresolved = false;
        for (const auto &record : part.electrical_records().records()) {
            if (record.meaning() == ElectricalMeaning::Characteristic) {
                continue;
            }
            const auto domain = project_subject(circuit, continuity, component, record.subject());
            if (!domain.has_value()) {
                subject_unresolved = true;
                current_unresolved =
                    current_unresolved || record.observable() == ElectricalObservable::Current;
                continue;
            }
            const auto key = ClaimKey{component, *domain, record.observable(), record.meaning()};
            auto [claim, inserted] = claims.try_emplace(
                key, Claim{component, *domain, record.observable(), record.meaning(), std::nullopt,
                           std::nullopt, false, false, provenance});
            static_cast<void>(inserted);
            add_record(claim->second, record);
        }
        if (subject_unresolved) {
            report.add(erc_diagnostic(
                erc_diagnostic_codes::SelectedPartElectricalSubjectUnresolved,
                "Selected-part electrical subject cannot be projected for " + provenance,
                {EntityRef::component(component), EntityRef::component_def(instance.definition())},
                subject_projection_rule));
        }
        if (current_unresolved) {
            unresolved_current.push_back(component);
        }
    }

    auto ordered = std::vector<std::reference_wrapper<const Claim>>{};
    ordered.reserve(claims.size());
    for (const auto &[key, claim] : claims) {
        static_cast<void>(key);
        ordered.push_back(std::cref(claim));
    }

    for (const auto provided_ref : ordered) {
        const auto &provided = provided_ref.get();
        if (provided.meaning != ElectricalMeaning::ProvidedRange || !provided.bounds.has_value()) {
            continue;
        }
        for (const auto constraint_ref : ordered) {
            const auto &constraint = constraint_ref.get();
            if (constraint.component == provided.component ||
                constraint.domain.key() != provided.domain.key() ||
                constraint.observable != provided.observable || !constraint.bounds.has_value()) {
                continue;
            }
            const auto entities =
                pair_entities(constraint.component, provided.component, provided.domain);
            if (constraint.meaning == ElectricalMeaning::AcceptedRange &&
                provided.observable == ElectricalObservable::Voltage) {
                if (below(*provided.bounds, *constraint.bounds)) {
                    report.add(erc_diagnostic(
                        erc_diagnostic_codes::SelectedPartVoltageBelowAcceptedRange,
                        "Provided Voltage is below the accepted range; provider " +
                            provided.provenance + ", consumer " + constraint.provenance,
                        entities, accepted_voltage_rule));
                }
                if (above(*provided.bounds, *constraint.bounds)) {
                    report.add(erc_diagnostic(
                        erc_diagnostic_codes::SelectedPartVoltageAboveAcceptedRange,
                        "Provided Voltage is above the accepted range; provider " +
                            provided.provenance + ", consumer " + constraint.provenance,
                        entities, accepted_voltage_rule));
                }
            }
            if (constraint.meaning != ElectricalMeaning::AbsoluteLimit ||
                (!below(*provided.bounds, *constraint.bounds) &&
                 !above(*provided.bounds, *constraint.bounds))) {
                continue;
            }
            if (provided.observable == ElectricalObservable::Voltage) {
                report.add(erc_diagnostic(
                    erc_diagnostic_codes::SelectedPartVoltageAbsoluteLimitViolation,
                    "Provided Voltage exceeds an absolute limit; provider " + provided.provenance +
                        ", constrained part " + constraint.provenance,
                    entities, absolute_voltage_rule));
            } else {
                report.add(erc_diagnostic(
                    erc_diagnostic_codes::SelectedPartCurrentAbsoluteLimitViolation,
                    "Provided Current exceeds an absolute limit; provider " + provided.provenance +
                        ", constrained part " + constraint.provenance,
                    entities, absolute_current_rule));
            }
        }
    }

    auto domains = std::map<std::pair<std::size_t, std::size_t>, Domain>{};
    for (const auto claim_ref : ordered) {
        const auto &claim = claim_ref.get();
        domains.try_emplace(claim.domain.key(), claim.domain);
    }
    for (const auto &[domain_key, domain] : domains) {
        static_cast<void>(domain_key);
        auto requirements = std::vector<std::reference_wrapper<const Claim>>{};
        auto capabilities = std::vector<std::reference_wrapper<const Claim>>{};
        for (const auto claim_ref : ordered) {
            const auto &claim = claim_ref.get();
            if (claim.domain.key() != domain.key() ||
                claim.observable != ElectricalObservable::Current) {
                continue;
            }
            if (claim.meaning == ElectricalMeaning::Requirement) {
                requirements.push_back(std::cref(claim));
            } else if (claim.meaning == ElectricalMeaning::Capability) {
                capabilities.push_back(std::cref(claim));
            }
        }
        if (requirements.empty()) {
            continue;
        }
        auto entities = std::vector<EntityRef>{};
        auto demand = 0.0;
        auto unknown = false;
        for (const auto requirement_ref : requirements) {
            const auto &requirement = requirement_ref.get();
            entities.push_back(EntityRef::component(requirement.component));
            if (!requirement.continuous_current.has_value() || requirement.has_unknown) {
                unknown = true;
            } else {
                demand += *requirement.continuous_current;
            }
        }
        const Claim *source = nullptr;
        if (capabilities.size() == 1U) {
            source = &capabilities.front().get();
            entities.push_back(EntityRef::component(source->component));
            unknown = unknown || !source->continuous_current.has_value() || source->has_unknown ||
                      !source->has_unconditional;
        } else {
            unknown = true;
            for (const auto capability : capabilities) {
                entities.push_back(EntityRef::component(capability.get().component));
            }
        }
        entities.push_back(EntityRef::net(domain.positive));
        entities.push_back(EntityRef::net(domain.negative));
        if (!unknown && source != nullptr && demand > *source->continuous_current) {
            report.add(
                erc_diagnostic(erc_diagnostic_codes::SelectedPartCurrentCapabilityInsufficient,
                               "Continuous Current demand exceeds the compatible source capability",
                               std::move(entities), current_budget_rule));
        } else if (unknown) {
            report.add(
                erc_diagnostic(erc_diagnostic_codes::SelectedPartCurrentBudgetUnknown,
                               "Continuous Current budget cannot be certified for this domain",
                               std::move(entities), current_budget_rule));
        }
    }

    if (!unresolved_current.empty()) {
        auto entities = std::vector<EntityRef>{};
        for (const auto component : unresolved_current) {
            entities.push_back(EntityRef::component(component));
        }
        report.add(erc_diagnostic(
            erc_diagnostic_codes::SelectedPartCurrentBudgetUnknown,
            "Continuous Current budget cannot be certified because a subject is unresolved",
            std::move(entities), current_budget_rule));
    }
    return report;
}

} // namespace volt
