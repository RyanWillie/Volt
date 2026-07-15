#include <volt/circuit/connectivity/component_contract.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace volt {

namespace {

template <typename Range, typename KeyFn>
void sort_and_require_unique_keys(Range &items, KeyFn key, const char *message) {
    std::ranges::sort(items, {}, key);
    const auto duplicate =
        std::adjacent_find(items.begin(), items.end(),
                           [&](const auto &lhs, const auto &rhs) { return key(lhs) == key(rhs); });
    if (duplicate != items.end()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, message};
    }
}

template <typename Key> void require_known_pin(const std::set<PinKey> &pins, const Key &pin) {
    if (!pins.contains(pin)) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Component contract references a foreign PinKey"};
    }
}

[[nodiscard]] bool requirement_less(const CanonicalRecordRequirement &lhs,
                                    const CanonicalRecordRequirement &rhs) noexcept {
    return std::pair{lhs.observable, lhs.meaning} < std::pair{rhs.observable, rhs.meaning};
}

[[nodiscard]] bool same_requirement(const CanonicalRecordRequirement &lhs,
                                    const CanonicalRecordRequirement &rhs) noexcept {
    return lhs.observable == rhs.observable && lhs.meaning == rhs.meaning;
}

void validate_required_record(ElectricalSubjectKind subject_kind,
                              const CanonicalRecordRequirement &requirement) {
    if (requirement.meaning != ElectricalMeaning::Requirement &&
        requirement.meaning != ElectricalMeaning::Capability) {
        return;
    }
    if (subject_kind != ElectricalSubjectKind::SupplyDomain ||
        requirement.observable != ElectricalObservable::Current) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Requirement and Capability schemas require Current on a supply domain"};
    }
}

[[nodiscard]] std::string subject_key(const ComponentSubjectRef &subject) {
    switch (subject.kind()) {
    case ElectricalSubjectKind::FramedPin:
        return "framed:" + subject.as_framed_pin().value();
    case ElectricalSubjectKind::DirectedRelation:
        return "relation:" + subject.as_directed_relation().value();
    case ElectricalSubjectKind::SupplyDomain:
        return "domain:" + subject.as_supply_domain().value();
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled component subject kind"};
}

} // namespace

ContractFramedPin::ContractFramedPin(FramedPinKey key, PinKey pin, PinKey reference)
    : key_{std::move(key)}, pin_{std::move(pin)}, reference_{std::move(reference)} {
    if (pin_ == reference_) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Framed pin and reference PinKeys must be distinct"};
    }
}

ContractDirectedRelation::ContractDirectedRelation(RelationKey key, PinKey from, PinKey to)
    : key_{std::move(key)}, from_{std::move(from)}, to_{std::move(to)} {
    if (from_ == to_) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Directed relation PinKeys must be distinct"};
    }
}

ContractSupplyDomain::ContractSupplyDomain(SupplyDomainKey key, std::vector<PinKey> positive_pins,
                                           std::vector<PinKey> return_pins)
    : key_{std::move(key)}, positive_pins_{std::move(positive_pins)},
      return_pins_{std::move(return_pins)} {
    if (positive_pins_.empty() || return_pins_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Supply-domain sides must both be non-empty"};
    }
    std::ranges::sort(positive_pins_);
    std::ranges::sort(return_pins_);
    if (std::adjacent_find(positive_pins_.begin(), positive_pins_.end()) != positive_pins_.end() ||
        std::adjacent_find(return_pins_.begin(), return_pins_.end()) != return_pins_.end()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Supply-domain sides must not contain duplicate PinKeys"};
    }
    for (const auto &pin : positive_pins_) {
        if (std::ranges::binary_search(return_pins_, pin)) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Supply-domain sides must be disjoint"};
        }
    }
}

ComponentSubjectRef ComponentSubjectRef::framed_pin(FramedPinKey key) {
    return ComponentSubjectRef{std::move(key)};
}

ComponentSubjectRef ComponentSubjectRef::directed_relation(RelationKey key) {
    return ComponentSubjectRef{std::move(key)};
}

ComponentSubjectRef ComponentSubjectRef::supply_domain(SupplyDomainKey key) {
    return ComponentSubjectRef{std::move(key)};
}

ElectricalSubjectKind ComponentSubjectRef::kind() const noexcept {
    if (std::holds_alternative<FramedPinKey>(value_)) {
        return ElectricalSubjectKind::FramedPin;
    }
    if (std::holds_alternative<RelationKey>(value_)) {
        return ElectricalSubjectKind::DirectedRelation;
    }
    return ElectricalSubjectKind::SupplyDomain;
}

const FramedPinKey &ComponentSubjectRef::as_framed_pin() const {
    return std::get<FramedPinKey>(value_);
}

const RelationKey &ComponentSubjectRef::as_directed_relation() const {
    return std::get<RelationKey>(value_);
}

const SupplyDomainKey &ComponentSubjectRef::as_supply_domain() const {
    return std::get<SupplyDomainKey>(value_);
}

FeatureSchema::FeatureSchema(FeatureSchemaKey key, ElectricalSubjectKind subject_kind,
                             std::vector<FeatureRole> roles,
                             std::vector<CanonicalRecordRequirement> required_records)
    : key_{std::move(key)}, subject_kind_{subject_kind}, roles_{std::move(roles)},
      required_records_{std::move(required_records)} {
    if (roles_.size() != 2U) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "V1 feature schemas must declare exactly two ordered roles"};
    }
    const auto expected = subject_kind_ == ElectricalSubjectKind::SupplyDomain
                              ? FeatureRoleCardinality::OneOrMore
                              : FeatureRoleCardinality::ExactlyOne;
    for (const auto &role : roles_) {
        if (role.cardinality() != expected) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Feature-schema role cardinality does not match subject kind"};
        }
    }
    if (roles_[0].key() == roles_[1].key()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Feature-schema role keys must be unique"};
    }
    for (const auto &requirement : required_records_) {
        validate_required_record(subject_kind_, requirement);
    }
    std::ranges::sort(required_records_, requirement_less);
    if (std::adjacent_find(required_records_.begin(), required_records_.end(), same_requirement) !=
        required_records_.end()) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Feature schema contains duplicate canonical record requirements"};
    }
}

FeatureRoleBinding::FeatureRoleBinding(FeatureRoleKey role, std::vector<PinKey> pins)
    : role_{std::move(role)}, pins_{std::move(pins)} {
    if (pins_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Feature role binding must contain at least one PinKey"};
    }
    std::ranges::sort(pins_);
    if (std::adjacent_find(pins_.begin(), pins_.end()) != pins_.end()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Feature role binding must not repeat a PinKey"};
    }
}

FeatureBinding::FeatureBinding(FeatureKey key, FeatureSchemaKey schema, ComponentSubjectRef subject,
                               std::vector<FeatureRoleBinding> roles)
    : key_{std::move(key)}, schema_{std::move(schema)}, subject_{std::move(subject)},
      roles_{std::move(roles)} {
    sort_and_require_unique_keys(
        roles_, [](const FeatureRoleBinding &role) { return role.role(); },
        "Feature binding contains a duplicate role key");
}

ComponentContract::ComponentContract(ComponentContractSpec spec)
    : ComponentContract{std::move(spec), true} {}

ComponentContract ComponentContract::standard(ComponentKey key, std::vector<PinKey> pin_keys) {
    return ComponentContract{ComponentContractSpec{std::move(key), std::move(pin_keys)}, false};
}

ComponentContract::ComponentContract(ComponentContractSpec spec, bool explicitly_authored)
    : key_{std::move(spec.key)}, pin_keys_{std::move(spec.pin_keys)},
      framed_pins_{std::move(spec.framed_pins)}, relations_{std::move(spec.relations)},
      supply_domains_{std::move(spec.supply_domains)},
      feature_schemas_{std::move(spec.feature_schemas)},
      feature_bindings_{std::move(spec.feature_bindings)},
      explicitly_authored_{explicitly_authored} {
    if (pin_keys_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Component contract must contain at least one PinKey"};
    }
    const auto pins = std::set<PinKey>{pin_keys_.begin(), pin_keys_.end()};
    if (pins.size() != pin_keys_.size()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Component contract contains duplicate PinKeys"};
    }

    sort_and_require_unique_keys(
        framed_pins_, [](const ContractFramedPin &subject) { return subject.key(); },
        "Component contract contains a duplicate framed-pin key");
    sort_and_require_unique_keys(
        relations_, [](const ContractDirectedRelation &subject) { return subject.key(); },
        "Component contract contains a duplicate relation key");
    sort_and_require_unique_keys(
        supply_domains_, [](const ContractSupplyDomain &subject) { return subject.key(); },
        "Component contract contains a duplicate supply-domain key");
    sort_and_require_unique_keys(
        feature_schemas_, [](const FeatureSchema &schema) { return schema.key(); },
        "Component contract contains a duplicate feature-schema key");
    sort_and_require_unique_keys(
        feature_bindings_, [](const FeatureBinding &binding) { return binding.key(); },
        "Component contract contains a duplicate feature-binding key");

    for (const auto &subject : framed_pins_) {
        require_known_pin(pins, subject.pin());
        require_known_pin(pins, subject.reference());
    }
    for (const auto &subject : relations_) {
        require_known_pin(pins, subject.from());
        require_known_pin(pins, subject.to());
    }
    for (const auto &subject : supply_domains_) {
        for (const auto &pin : subject.positive_pins()) {
            require_known_pin(pins, pin);
        }
        for (const auto &pin : subject.return_pins()) {
            require_known_pin(pins, pin);
        }
    }

    for (const auto &binding : feature_bindings_) {
        const auto schema =
            std::ranges::find(feature_schemas_, binding.schema(), &FeatureSchema::key);
        if (schema == feature_schemas_.end()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Feature binding references a foreign feature-schema key"};
        }
        if (schema->subject_kind() != binding.subject().kind()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Feature binding subject kind does not match its schema"};
        }
        if (binding.roles().size() != schema->roles().size()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Feature binding is missing a schema role"};
        }

        auto ordered_role_pins = std::vector<std::vector<PinKey>>{};
        ordered_role_pins.reserve(schema->roles().size());
        for (const auto &schema_role : schema->roles()) {
            const auto role =
                std::ranges::find(binding.roles(), schema_role.key(), &FeatureRoleBinding::role);
            if (role == binding.roles().end()) {
                throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                       "Feature binding references a foreign or missing role key"};
            }
            if (schema_role.cardinality() == FeatureRoleCardinality::ExactlyOne &&
                role->pins().size() != 1U) {
                throw KernelArgumentError{ErrorCode::InvalidArgument,
                                          "Feature binding violates exactly-one role cardinality"};
            }
            for (const auto &pin : role->pins()) {
                require_known_pin(pins, pin);
            }
            ordered_role_pins.push_back(role->pins());
        }

        switch (binding.subject().kind()) {
        case ElectricalSubjectKind::FramedPin: {
            const auto subject = std::ranges::find(framed_pins_, binding.subject().as_framed_pin(),
                                                   &ContractFramedPin::key);
            if (subject == framed_pins_.end()) {
                throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                       "Feature binding references a foreign framed-pin key"};
            }
            if (ordered_role_pins[0].front() != subject->pin() ||
                ordered_role_pins[1].front() != subject->reference()) {
                throw KernelArgumentError{ErrorCode::InvalidArgument,
                                          "Feature binding roles do not match its framed pin"};
            }
            break;
        }
        case ElectricalSubjectKind::DirectedRelation: {
            const auto subject =
                std::ranges::find(relations_, binding.subject().as_directed_relation(),
                                  &ContractDirectedRelation::key);
            if (subject == relations_.end()) {
                throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                       "Feature binding references a foreign relation key"};
            }
            if (ordered_role_pins[0].front() != subject->from() ||
                ordered_role_pins[1].front() != subject->to()) {
                throw KernelArgumentError{ErrorCode::InvalidArgument,
                                          "Feature binding roles do not match its relation"};
            }
            break;
        }
        case ElectricalSubjectKind::SupplyDomain: {
            const auto subject = std::ranges::find(
                supply_domains_, binding.subject().as_supply_domain(), &ContractSupplyDomain::key);
            if (subject == supply_domains_.end()) {
                throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                       "Feature binding references a foreign supply-domain key"};
            }
            if (ordered_role_pins[0] != subject->positive_pins() ||
                ordered_role_pins[1] != subject->return_pins()) {
                throw KernelArgumentError{ErrorCode::InvalidArgument,
                                          "Feature binding roles do not match its supply domain"};
            }
            break;
        }
        }

        for (const auto &requirement : schema->required_records()) {
            required_records_.push_back(BoundCanonicalRecordRequirement{
                binding.subject(), requirement.observable, requirement.meaning});
        }
    }

    std::ranges::sort(required_records_, [](const auto &lhs, const auto &rhs) {
        return std::tuple{subject_key(lhs.subject), lhs.observable, lhs.meaning} <
               std::tuple{subject_key(rhs.subject), rhs.observable, rhs.meaning};
    });
    required_records_.erase(std::unique(required_records_.begin(), required_records_.end(),
                                        [](const auto &lhs, const auto &rhs) {
                                            return subject_key(lhs.subject) ==
                                                       subject_key(rhs.subject) &&
                                                   lhs.observable == rhs.observable &&
                                                   lhs.meaning == rhs.meaning;
                                        }),
                            required_records_.end());
}

FeatureSchema supply_consumer_feature_schema() {
    return FeatureSchema{
        FeatureSchemaKey{"volt.feature/supply-consumer@1"},
        ElectricalSubjectKind::SupplyDomain,
        {FeatureRole{FeatureRoleKey{"positive"}, FeatureRoleCardinality::OneOrMore},
         FeatureRole{FeatureRoleKey{"return"}, FeatureRoleCardinality::OneOrMore}},
        {{ElectricalObservable::Voltage, ElectricalMeaning::AcceptedRange},
         {ElectricalObservable::Current, ElectricalMeaning::Requirement}}};
}

FeatureSchema supply_source_feature_schema() {
    return FeatureSchema{
        FeatureSchemaKey{"volt.feature/supply-source@1"},
        ElectricalSubjectKind::SupplyDomain,
        {FeatureRole{FeatureRoleKey{"positive"}, FeatureRoleCardinality::OneOrMore},
         FeatureRole{FeatureRoleKey{"return"}, FeatureRoleCardinality::OneOrMore}},
        {{ElectricalObservable::Voltage, ElectricalMeaning::ProvidedRange},
         {ElectricalObservable::Current, ElectricalMeaning::Capability}}};
}

FeatureSchema diode_junction_feature_schema() {
    return FeatureSchema{
        FeatureSchemaKey{"volt.feature/diode-junction@1"},
        ElectricalSubjectKind::DirectedRelation,
        {FeatureRole{FeatureRoleKey{"positive"}, FeatureRoleCardinality::ExactlyOne},
         FeatureRole{FeatureRoleKey{"negative"}, FeatureRoleCardinality::ExactlyOne}},
        {{ElectricalObservable::Voltage, ElectricalMeaning::Characteristic},
         {ElectricalObservable::Current, ElectricalMeaning::AbsoluteLimit},
         {ElectricalObservable::Voltage, ElectricalMeaning::AbsoluteLimit}}};
}

} // namespace volt
