#include "selected_part_erc_detail.hpp"
#include <cmath>
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
#include <volt/core/rule_set.hpp>

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
    bool has_conditions = false;
    std::vector<std::size_t> groups;
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

struct OrientedPins {
    std::vector<ElectricalPinIndex> positive;
    std::vector<ElectricalPinIndex> negative;
};

OrientedPins subject_indices(const ElectricalSubject &subject) {
    auto result = OrientedPins{};
    auto &positive = result.positive;
    auto &negative = result.negative;
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

    return result;
}

[[nodiscard]] std::optional<Domain> project_subject(const Circuit &circuit,
                                                    const detail::NetContinuityView &continuity,
                                                    const std::vector<PinId> &pins,
                                                    const ElectricalSubject &subject) {
    const auto [positive, negative] = subject_indices(subject);
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
    claim.has_conditions = claim.has_conditions || !record.conditions().empty();
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

[[nodiscard]] std::optional<double> authored_net_voltage(const Circuit &circuit, NetId net) {
    const auto &attributes = queries::net_electrical_attributes(circuit, net);
    const auto name = ElectricalAttributeName{"voltage"};
    if (attributes.contains(name)) {
        const auto &attribute = attributes.get(name);
        if (attribute.kind() == ElectricalAttributeValueKind::Quantity &&
            attribute.as_quantity().dimension() == UnitDimension::Voltage) {
            return attribute.as_quantity().value();
        }
        return std::nullopt;
    }
    if (circuit.get(net).kind() == NetKind::Ground) {
        return 0.0;
    }
    return std::nullopt;
}

} // namespace

namespace {
std::string rating_rule(const ElectricalRecord &record) {
    if (record.meaning() == ElectricalMeaning::ProvidedRange)
        return "volt.part_erc.provided_range@1";
    if (record.observable() == ElectricalObservable::Voltage) {
        if (record.meaning() == ElectricalMeaning::AcceptedRange)
            return accepted_voltage_rule;
        if (record.meaning() == ElectricalMeaning::AbsoluteLimit)
            return absolute_voltage_rule;
    } else {
        if (record.meaning() == ElectricalMeaning::AbsoluteLimit)
            return absolute_current_rule;
        if (record.meaning() == ElectricalMeaning::Requirement ||
            record.meaning() == ElectricalMeaning::Capability)
            return current_budget_rule;
    }
    return "volt.part_erc.unsupported@1";
}

struct RatingAudit {
    SelectedPartRatings result;
    std::vector<bool> incomplete;

    void gap(const Claim &claim, const std::string &reason) {
        for (const auto group : claim.groups) {
            incomplete[group] = true;
            auto &subject = result.subjects[group];
            if (subject.state != SelectedPartRatingState::Failed)
                subject.reason = reason;
        }
    }

    void compare(const Claim &claim, std::vector<EntityRef> entities,
                 ElectricalObservable observable, double value, const Bounds &limit,
                 std::string_view rule) {
        const auto failed =
            below(Bounds{value, value}, limit) || above(Bounds{value, value}, limit);
        for (const auto group : claim.groups) {
            auto &subject = result.subjects[group];
            subject.observations.push_back(
                {entities, observable, value, limit.minimum, limit.maximum, std::string{rule},
                 failed ? SelectedPartRatingState::Failed : SelectedPartRatingState::Pass});
            subject.entities.insert(subject.entities.end(), entities.begin(), entities.end());
            if (failed) {
                subject.state = SelectedPartRatingState::Failed;
                subject.reason = "Supported unconditional native rating comparison failed";
            }
        }
    }

    void range(const Claim &constraint, const Claim *provider, const Bounds &provided,
               std::vector<EntityRef> entities, std::string_view rule) {
        if ((constraint.bounds->minimum && !provided.minimum) ||
            (constraint.bounds->maximum && !provided.maximum)) {
            gap(constraint, "Provider is missing a required range bound");
            if (provider)
                gap(*provider, "Provider is missing a required range bound");
        }
        if (constraint.bounds->minimum && provided.minimum) {
            const auto limit = Bounds{constraint.bounds->minimum, {}};
            compare(constraint, entities, constraint.observable, *provided.minimum, limit, rule);
            if (provider)
                compare(*provider, entities, constraint.observable, *provided.minimum, limit, rule);
        }
        if (constraint.bounds->maximum && provided.maximum) {
            const auto limit = Bounds{{}, constraint.bounds->maximum};
            compare(constraint, entities, constraint.observable, *provided.maximum, limit, rule);
            if (provider)
                compare(*provider, entities, constraint.observable, *provided.maximum, limit, rule);
        }
    }
};

bool usable(const Claim &claim) { return !claim.has_unknown && !claim.has_conditions; }

// Preserve the live ERC's canonical-representative behavior. A noncanonical assertion
// cannot silently vanish or supply a value the existing rule never examined.
bool canonical_voltage_complete(const Circuit &circuit, const detail::NetContinuityView &continuity,
                                NetId canonical) {
    const auto expected = authored_net_voltage(circuit, canonical);
    for (std::size_t i = 0; i < circuit.all<NetId>().size(); ++i) {
        const auto id = NetId{i};
        if (!continuity.same_group(id, canonical))
            continue;
        const auto actual = authored_net_voltage(circuit, id);
        if (actual && (!expected || *actual != *expected))
            return false;
    }
    return true;
}

DiagnosticReport run_selected_part_erc(const Circuit &circuit,
                                       const detail::NetContinuityView &continuity,
                                       std::span<const detail::SelectedPartErcSubject> subjects,
                                       RatingAudit *audit) {
    auto claims = std::map<ClaimKey, Claim>{};
    auto unresolved_current = std::vector<ComponentId>{};
    auto report = DiagnosticReport{};

    for (const auto &input : subjects) {
        const auto component = input.component;
        const auto &reference = queries::selected_library_part_ref(circuit, component);
        const auto &instance = circuit.get(component);
        const auto dnp = queries::component_dnp(circuit, component).value_or(false);
        const auto add_group = [&](std::optional<ElectricalSemanticKey> key,
                                   std::optional<ElectricalRecordSet> records, std::string reason,
                                   std::string rule = "volt.part_erc.input@1") {
            const auto group = audit->result.subjects.size();
            audit->result.subjects.push_back(
                {component,
                 reference,
                 std::move(key),
                 std::move(records),
                 std::move(rule),
                 dnp ? SelectedPartRatingState::NotApplicable
                     : SelectedPartRatingState::NotEvaluated,
                 dnp ? "Explicit DNP occurrence supplies no installed rating guarantee"
                     : std::move(reason),
                 {EntityRef::component(component)},
                 {}});
            audit->incomplete.push_back(false);
            return group;
        };
        if (!reference || !input.part) {
            if (audit)
                add_group({}, {}, "Populated occurrence has no exact selected part");
            continue;
        }
        const auto &part = *input.part;
        const auto provenance = part_provenance(part, *reference);
        auto subject_unresolved = false;
        auto current_unresolved = false;
        auto groups = std::map<std::string, std::size_t>{};
        for (const auto &record : part.electrical_records().records()) {
            if (record.meaning() == ElectricalMeaning::Characteristic)
                continue;
            auto group = std::optional<std::size_t>{};
            if (audit) {
                const auto key = record.semantic_key().hash().value();
                if (!groups.contains(key)) {
                    auto originals = std::vector<ElectricalRecordSpec>{};
                    for (const auto &source : part.electrical_records().records())
                        if (source.semantic_key() == record.semantic_key())
                            originals.push_back({source.subject(), source.observable(),
                                                 source.meaning(), source.value(),
                                                 source.conditions(), source.evidence()});
                    groups.emplace(
                        key,
                        add_group(
                            record.semantic_key(),
                            ElectricalRecordSet{part.electrical_records().pin_count(), originals},
                            "No supported rating comparison executed for this semantic group",
                            rating_rule(record)));
                }
                group = groups.at(key);
                const auto indices = subject_indices(record.subject());
                auto &entities = audit->result.subjects[*group].entities;
                for (const auto &side : {indices.positive, indices.negative})
                    for (const auto index : side)
                        entities.push_back(EntityRef::pin(input.pins[index.value()]));
                if (dnp)
                    continue;
            }
            const auto domain = project_subject(circuit, continuity, input.pins, record.subject());
            if (!domain.has_value()) {
                subject_unresolved = true;
                current_unresolved =
                    current_unresolved || record.observable() == ElectricalObservable::Current;
                if (audit) {
                    audit->incomplete[*group] = true;
                    audit->result.subjects[*group].reason =
                        "Electrical subject endpoints do not project to distinct complete domains";
                }
                continue;
            }
            if (group) {
                auto &entities = audit->result.subjects[*group].entities;
                entities.push_back(EntityRef::net(domain->positive));
                entities.push_back(EntityRef::net(domain->negative));
            }
            const auto key = ClaimKey{component, *domain, record.observable(), record.meaning()};
            auto [claim, inserted] = claims.try_emplace(key, Claim{component,
                                                                   *domain,
                                                                   record.observable(),
                                                                   record.meaning(),
                                                                   std::nullopt,
                                                                   std::nullopt,
                                                                   false,
                                                                   false,
                                                                   provenance,
                                                                   false,
                                                                   {}});
            static_cast<void>(inserted);
            add_record(claim->second, record);
            if (group &&
                std::ranges::find(claim->second.groups, *group) == claim->second.groups.end())
                claim->second.groups.push_back(*group);
        }
        if (audit && groups.empty())
            add_group({}, {}, "Exact part declares no non-characteristic rating obligations");
        if (subject_unresolved) {
            report.add(erc_diagnostic(
                erc_diagnostic_codes::SelectedPartElectricalSubjectUnresolved,
                "Selected-part electrical subject cannot be projected for " + provenance,
                {EntityRef::component(component), EntityRef::component_def(instance.definition())},
                subject_projection_rule));
        }
        if (current_unresolved)
            unresolved_current.push_back(component);
    }
    auto ordered = std::vector<std::reference_wrapper<const Claim>>{};
    ordered.reserve(claims.size());
    for (const auto &[key, claim] : claims) {
        static_cast<void>(key);
        ordered.push_back(std::cref(claim));
        if (audit && !usable(claim))
            audit->gap(claim, "Conditional, Unknown or conflicting effective rating claim");
    }

    for (const auto constraint_ref : ordered) {
        const auto &constraint = constraint_ref.get();
        if (constraint.observable != ElectricalObservable::Voltage ||
            constraint.meaning != ElectricalMeaning::AbsoluteLimit ||
            !constraint.bounds.has_value()) {
            continue;
        }
        if (audit && !usable(constraint)) {
            audit->gap(constraint, "Conditional, Unknown or conflicting effective rating claim");
            continue;
        }
        if (audit &&
            (!canonical_voltage_complete(circuit, continuity, constraint.domain.positive) ||
             !canonical_voltage_complete(circuit, continuity, constraint.domain.negative))) {
            audit->gap(constraint,
                       "Noncanonical or contradictory authored domain voltage is unsupported");
            continue;
        }
        const auto positive = authored_net_voltage(circuit, constraint.domain.positive);
        const auto negative = authored_net_voltage(circuit, constraint.domain.negative);
        if (!positive.has_value() || !negative.has_value()) {
            continue;
        }
        const auto voltage = *positive - *negative;
        if (audit && !std::isfinite(voltage)) {
            audit->gap(constraint, "Authored potential subtraction is non-finite");
            continue;
        }
        const auto authored = Bounds{voltage, voltage};
        if (audit)
            audit->range(constraint, nullptr, authored,
                         {EntityRef::component(constraint.component),
                          EntityRef::net(constraint.domain.positive),
                          EntityRef::net(constraint.domain.negative)},
                         absolute_voltage_rule);
        if (!below(authored, *constraint.bounds) && !above(authored, *constraint.bounds)) {
            continue;
        }
        report.add(erc_diagnostic(
            erc_diagnostic_codes::SelectedPartVoltageAbsoluteLimitViolation,
            "Authored net Voltage exceeds an exact-part absolute limit; constrained part " +
                constraint.provenance,
            {EntityRef::component(constraint.component), EntityRef::net(constraint.domain.positive),
             EntityRef::net(constraint.domain.negative)},
            absolute_voltage_rule));
    }

    for (const auto provided_ref : ordered) {
        const auto &provided = provided_ref.get();
        if (provided.meaning != ElectricalMeaning::ProvidedRange)
            continue;
        if (!provided.bounds) {
            if (audit)
                for (const auto constraint_ref : ordered) {
                    const auto &constraint = constraint_ref.get();
                    if (constraint.component != provided.component &&
                        constraint.domain.key() == provided.domain.key() &&
                        constraint.observable == provided.observable &&
                        (constraint.meaning == ElectricalMeaning::AbsoluteLimit ||
                         (constraint.meaning == ElectricalMeaning::AcceptedRange &&
                          constraint.observable == ElectricalObservable::Voltage)))
                        audit->gap(constraint, "Applicable provider has no known effective range");
                }
            continue;
        }
        for (const auto constraint_ref : ordered) {
            const auto &constraint = constraint_ref.get();
            if (constraint.component == provided.component ||
                constraint.domain.key() != provided.domain.key() ||
                constraint.observable != provided.observable || !constraint.bounds.has_value()) {
                continue;
            }
            const auto supported = constraint.meaning == ElectricalMeaning::AbsoluteLimit ||
                                   (constraint.meaning == ElectricalMeaning::AcceptedRange &&
                                    provided.observable == ElectricalObservable::Voltage);
            if (audit && supported && (!usable(constraint) || !usable(provided))) {
                audit->gap(constraint,
                           "Conditional, Unknown or conflicting effective rating claim");
                audit->gap(provided, "Conditional, Unknown or conflicting effective rating claim");
                continue;
            }
            const auto entities =
                pair_entities(constraint.component, provided.component, provided.domain);
            if (audit && supported)
                audit->range(
                    constraint, &provided, *provided.bounds, entities,
                    constraint.meaning == ElectricalMeaning::AcceptedRange ? accepted_voltage_rule
                    : provided.observable == ElectricalObservable::Voltage ? absolute_voltage_rule
                                                                           : absolute_current_rule);
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
        if (audit) {
            unknown = unknown || !std::isfinite(demand);
            for (const auto claim : requirements)
                unknown = unknown || !usable(claim.get());
            for (const auto claim : capabilities)
                unknown = unknown || !usable(claim.get());
            for (const auto claim : requirements) {
                if (unknown)
                    audit->gap(claim.get(), "Continuous budget needs finite known unconditional "
                                            "demand and exactly one source");
                else
                    audit->compare(claim.get(), entities, ElectricalObservable::Current, demand,
                                   Bounds{{}, source->continuous_current}, current_budget_rule);
            }
            for (const auto claim : capabilities) {
                if (unknown)
                    audit->gap(claim.get(), "Continuous budget needs finite known unconditional "
                                            "demand and exactly one source");
                else
                    audit->compare(claim.get(), entities, ElectricalObservable::Current, demand,
                                   Bounds{{}, source->continuous_current}, current_budget_rule);
            }
        }
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

} // namespace

DiagnosticReport validate_selected_part_erc(const Circuit &circuit,
                                            const ExactPartResolver &resolver) {
    auto subjects = std::vector<detail::SelectedPartErcSubject>{};
    for (std::size_t i = 0; i < circuit.all<ComponentId>().size(); ++i) {
        const auto component = ComponentId{i};
        const auto &reference = queries::selected_library_part_ref(circuit, component);
        if (!reference)
            continue;
        const auto &part = resolver.resolve(*reference);
        const auto &definition = circuit.get(circuit.get(component).definition());
        if (part.implemented_component() != definition.content_identity())
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Selected exact part implements a different component contract",
                                   EntityRef::component(component)};
        subjects.push_back({component, &part, &definition, queries::pins_for(circuit, component)});
    }
    return run_selected_part_erc(circuit, detail::NetContinuityView{circuit}, subjects, nullptr);
}

SelectedPartRatings
detail::assess_selected_part_ratings(const Circuit &circuit, const NetContinuityView &continuity,
                                     std::span<const SelectedPartErcSubject> subjects,
                                     bool continuity_complete) {
    auto audit = RatingAudit{};
    audit.result.diagnostics = run_selected_part_erc(circuit, continuity, subjects, &audit);
    for (std::size_t i = 0; i < audit.result.subjects.size(); ++i) {
        auto &subject = audit.result.subjects[i];
        std::ranges::sort(subject.entities);
        subject.entities.erase(std::unique(subject.entities.begin(), subject.entities.end()),
                               subject.entities.end());
        if (subject.state == SelectedPartRatingState::NotApplicable)
            continue;
        if (!continuity_complete) {
            subject.state = SelectedPartRatingState::NotEvaluated;
            subject.reason = "Historical artifact has no authoritative logical continuity";
            subject.observations.clear();
        } else if (subject.state != SelectedPartRatingState::Failed && !audit.incomplete[i] &&
                   !subject.observations.empty()) {
            subject.state = SelectedPartRatingState::Pass;
            subject.reason = "All applicable supported unconditional rating comparisons passed";
        }
    }
    if (!continuity_complete)
        audit.result.diagnostics = DiagnosticReport{};
    return std::move(audit.result);
}

[[nodiscard]] DiagnosticReport validate_design(const Circuit &circuit,
                                               const ExactPartResolver &resolver) {
    auto report = validate_circuit(circuit);

    const auto selected_part_report = validate_selected_part_erc(circuit, resolver);
    for (const auto &diagnostic : selected_part_report.diagnostics()) {
        report.add(diagnostic);
    }

    return report;
}

[[nodiscard]] DiagnosticReport validate_for_pcb(const Circuit &circuit,
                                                const ExactPartResolver &resolver) {
    auto report = validate_design(circuit, resolver);

    auto rules = RuleSet<Circuit>{};
    rules.add([](const Circuit &rule_circuit, DiagnosticReport &rule_report) {
        detail::validate_physical_part_selection(rule_circuit, rule_report);
    });
    rules.run(circuit, report);

    return report;
}

} // namespace volt
