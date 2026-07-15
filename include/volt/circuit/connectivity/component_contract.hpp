#pragma once

#include <compare>
#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <volt/circuit/electrical/records.hpp>
#include <volt/core/errors.hpp>

namespace volt {

/** Strongly typed non-empty stable key used inside immutable component contracts. */
template <typename Tag> class ComponentContractKey {
  public:
    /** Construct a stable key from a non-empty portable label. */
    explicit ComponentContractKey(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Component-contract key must not be empty"};
        }
    }

    /** Return the portable key label. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare typed key labels. */
    [[nodiscard]] bool operator==(const ComponentContractKey &) const noexcept = default;

    /** Order typed key labels lexicographically. */
    [[nodiscard]] std::strong_ordering
    operator<=>(const ComponentContractKey &) const noexcept = default;

  private:
    std::string value_;
};

/** Type tag separating component identities from other contract keys. */
struct ComponentKeyTag;
/** Type tag separating logical pin identities from other contract keys. */
struct PinKeyTag;
/** Type tag separating framed-pin identities from other contract keys. */
struct FramedPinKeyTag;
/** Type tag separating directed-relation identities from other contract keys. */
struct RelationKeyTag;
/** Type tag separating supply-domain identities from other contract keys. */
struct SupplyDomainKeyTag;
/** Type tag separating feature-schema identities from other contract keys. */
struct FeatureSchemaKeyTag;
/** Type tag separating bound feature identities from other contract keys. */
struct FeatureKeyTag;
/** Type tag separating feature-role identities from other contract keys. */
struct FeatureRoleKeyTag;

/** Readable stable key for one reusable component contract. */
using ComponentKey = ComponentContractKey<ComponentKeyTag>;
/** Stable logical pin identity local to one component contract. */
using PinKey = ComponentContractKey<PinKeyTag>;
/** Stable identity for one explicitly framed component pin. */
using FramedPinKey = ComponentContractKey<FramedPinKeyTag>;
/** Stable identity for one directed component relation. */
using RelationKey = ComponentContractKey<RelationKeyTag>;
/** Stable identity for one declared component supply domain. */
using SupplyDomainKey = ComponentContractKey<SupplyDomainKeyTag>;
/** Stable versioned identity for one typed feature schema. */
using FeatureSchemaKey = ComponentContractKey<FeatureSchemaKeyTag>;
/** Stable component-local identity for one feature binding. */
using FeatureKey = ComponentContractKey<FeatureKeyTag>;
/** Stable schema-local identity for one terminal role. */
using FeatureRoleKey = ComponentContractKey<FeatureRoleKeyTag>;

/** One named pin framed against a distinct named reference pin. */
class ContractFramedPin {
  public:
    /** Construct one stable framed-pin subject. */
    ContractFramedPin(FramedPinKey key, PinKey pin, PinKey reference);

    /** Return the stable framed-pin identity. */
    [[nodiscard]] const FramedPinKey &key() const noexcept { return key_; }

    /** Return the pin measured in this frame. */
    [[nodiscard]] const PinKey &pin() const noexcept { return pin_; }

    /** Return the distinct reference pin. */
    [[nodiscard]] const PinKey &reference() const noexcept { return reference_; }

  private:
    FramedPinKey key_;
    PinKey pin_;
    PinKey reference_;
};

/** One stable directed relation from one logical pin to another. */
class ContractDirectedRelation {
  public:
    /** Construct one stable directed relation. */
    ContractDirectedRelation(RelationKey key, PinKey from, PinKey to);

    /** Return the stable relation identity. */
    [[nodiscard]] const RelationKey &key() const noexcept { return key_; }

    /** Return the positive or source-side pin. */
    [[nodiscard]] const PinKey &from() const noexcept { return from_; }

    /** Return the negative or destination-side pin. */
    [[nodiscard]] const PinKey &to() const noexcept { return to_; }

  private:
    RelationKey key_;
    PinKey from_;
    PinKey to_;
};

/** One stable supply domain with unordered positive and return pin sets. */
class ContractSupplyDomain {
  public:
    /** Construct and normalize one non-empty disjoint supply domain. */
    ContractSupplyDomain(SupplyDomainKey key, std::vector<PinKey> positive_pins,
                         std::vector<PinKey> return_pins);

    /** Return the stable supply-domain identity. */
    [[nodiscard]] const SupplyDomainKey &key() const noexcept { return key_; }

    /** Return the sorted non-empty positive-side PinKeys. */
    [[nodiscard]] const std::vector<PinKey> &positive_pins() const noexcept {
        return positive_pins_;
    }

    /** Return the sorted non-empty return-side PinKeys. */
    [[nodiscard]] const std::vector<PinKey> &return_pins() const noexcept { return return_pins_; }

  private:
    SupplyDomainKey key_;
    std::vector<PinKey> positive_pins_;
    std::vector<PinKey> return_pins_;
};

/** Typed reference to one component-contract electrical subject. */
class ComponentSubjectRef {
  public:
    /** Reference one named framed-pin subject. */
    [[nodiscard]] static ComponentSubjectRef framed_pin(FramedPinKey key);
    /** Reference one named directed-relation subject. */
    [[nodiscard]] static ComponentSubjectRef directed_relation(RelationKey key);
    /** Reference one named supply-domain subject. */
    [[nodiscard]] static ComponentSubjectRef supply_domain(SupplyDomainKey key);

    /** Return the closed canonical subject kind. */
    [[nodiscard]] ElectricalSubjectKind kind() const noexcept;
    /** Return the framed-pin key or throw when the kind differs. */
    [[nodiscard]] const FramedPinKey &as_framed_pin() const;
    /** Return the directed-relation key or throw when the kind differs. */
    [[nodiscard]] const RelationKey &as_directed_relation() const;
    /** Return the supply-domain key or throw when the kind differs. */
    [[nodiscard]] const SupplyDomainKey &as_supply_domain() const;

  private:
    explicit ComponentSubjectRef(FramedPinKey key) : value_{std::move(key)} {}

    explicit ComponentSubjectRef(RelationKey key) : value_{std::move(key)} {}

    explicit ComponentSubjectRef(SupplyDomainKey key) : value_{std::move(key)} {}

    std::variant<FramedPinKey, RelationKey, SupplyDomainKey> value_;
};

/** Cardinality required for one feature-schema terminal role. */
enum class FeatureRoleCardinality {
    ExactlyOne,
    OneOrMore,
};

/** One ordered terminal role declared by a feature schema. */
class FeatureRole {
  public:
    /** Construct one named role with explicit cardinality. */
    FeatureRole(FeatureRoleKey key, FeatureRoleCardinality cardinality)
        : key_{std::move(key)}, cardinality_{cardinality} {}

    /** Return the schema-local role identity. */
    [[nodiscard]] const FeatureRoleKey &key() const noexcept { return key_; }

    /** Return the required binding cardinality. */
    [[nodiscard]] FeatureRoleCardinality cardinality() const noexcept { return cardinality_; }

  private:
    FeatureRoleKey key_;
    FeatureRoleCardinality cardinality_;
};

/** Required canonical P1 record shape, independent of feature naming. */
struct CanonicalRecordRequirement {
    /** Canonical P1 observable required on the bound subject. */
    ElectricalObservable observable;
    /** Canonical P1 meaning required on the bound subject. */
    ElectricalMeaning meaning;
};

/** One versioned typed feature schema over exactly one canonical subject. */
class FeatureSchema {
  public:
    /** Construct and validate a generic feature schema. */
    FeatureSchema(FeatureSchemaKey key, ElectricalSubjectKind subject_kind,
                  std::vector<FeatureRole> roles,
                  std::vector<CanonicalRecordRequirement> required_records);

    /** Return the stable versioned schema identity. */
    [[nodiscard]] const FeatureSchemaKey &key() const noexcept { return key_; }

    /** Return the canonical subject shape accepted by this schema. */
    [[nodiscard]] ElectricalSubjectKind subject_kind() const noexcept { return subject_kind_; }

    /** Return the ordered terminal roles. */
    [[nodiscard]] const std::vector<FeatureRole> &roles() const noexcept { return roles_; }

    /** Return the sorted canonical P1 record shapes required by the schema. */
    [[nodiscard]] const std::vector<CanonicalRecordRequirement> &required_records() const noexcept {
        return required_records_;
    }

  private:
    FeatureSchemaKey key_;
    ElectricalSubjectKind subject_kind_;
    std::vector<FeatureRole> roles_;
    std::vector<CanonicalRecordRequirement> required_records_;
};

/** One schema-role assignment to contract-local logical pins. */
class FeatureRoleBinding {
  public:
    /** Bind one named schema role to one or more contract-local PinKeys. */
    FeatureRoleBinding(FeatureRoleKey role, std::vector<PinKey> pins);

    /** Return the bound schema-role identity. */
    [[nodiscard]] const FeatureRoleKey &role() const noexcept { return role_; }

    /** Return the normalized bound PinKeys. */
    [[nodiscard]] const std::vector<PinKey> &pins() const noexcept { return pins_; }

  private:
    FeatureRoleKey role_;
    std::vector<PinKey> pins_;
};

/** One component-local binding of a feature schema to one named canonical subject. */
class FeatureBinding {
  public:
    /** Bind one feature schema and named subject to complete role assignments. */
    FeatureBinding(FeatureKey key, FeatureSchemaKey schema, ComponentSubjectRef subject,
                   std::vector<FeatureRoleBinding> roles);

    /** Return the component-local feature identity. */
    [[nodiscard]] const FeatureKey &key() const noexcept { return key_; }

    /** Return the referenced feature-schema identity. */
    [[nodiscard]] const FeatureSchemaKey &schema() const noexcept { return schema_; }

    /** Return the referenced named canonical subject. */
    [[nodiscard]] const ComponentSubjectRef &subject() const noexcept { return subject_; }

    /** Return the normalized complete role assignments. */
    [[nodiscard]] const std::vector<FeatureRoleBinding> &roles() const noexcept { return roles_; }

  private:
    FeatureKey key_;
    FeatureSchemaKey schema_;
    ComponentSubjectRef subject_;
    std::vector<FeatureRoleBinding> roles_;
};

/** Fully bound canonical requirement exposed by an immutable component contract. */
struct BoundCanonicalRecordRequirement {
    /** Named component-contract subject that must carry the record. */
    ComponentSubjectRef subject;
    /** Canonical P1 observable required on the subject. */
    ElectricalObservable observable;
    /** Canonical P1 meaning required on the subject. */
    ElectricalMeaning meaning;
};

/** Complete portable component-contract semantic input. */
struct ComponentContractSpec {
    /** Readable stable identity of the reusable component contract. */
    ComponentKey key;
    /** Stable PinKeys aligned with ordered PinDefinition content. */
    std::vector<PinKey> pin_keys;
    /** Named framed-pin subjects. */
    std::vector<ContractFramedPin> framed_pins = {};
    /** Named directed-relation subjects. */
    std::vector<ContractDirectedRelation> relations = {};
    /** Named supply-domain subjects. */
    std::vector<ContractSupplyDomain> supply_domains = {};
    /** Typed reusable feature schemas used by this contract. */
    std::vector<FeatureSchema> feature_schemas = {};
    /** Component-local schema bindings to named subjects and PinKeys. */
    std::vector<FeatureBinding> feature_bindings = {};
};

/** Normalized immutable stable identity and feature-binding content. */
class ComponentContract {
  public:
    /** Validate and normalize an explicitly authored contract. */
    explicit ComponentContract(ComponentContractSpec spec);

    /** Build the standard lowering used by existing helpers and simple custom components. */
    [[nodiscard]] static ComponentContract standard(ComponentKey key, std::vector<PinKey> pin_keys);

    /** Return the readable stable component identity. */
    [[nodiscard]] const ComponentKey &key() const noexcept { return key_; }

    /** Return the ordered stable PinKeys. */
    [[nodiscard]] const std::vector<PinKey> &pin_keys() const noexcept { return pin_keys_; }

    /** Return named framed-pin subjects in stable key order. */
    [[nodiscard]] const std::vector<ContractFramedPin> &framed_pins() const noexcept {
        return framed_pins_;
    }

    /** Return named directed relations in stable key order. */
    [[nodiscard]] const std::vector<ContractDirectedRelation> &relations() const noexcept {
        return relations_;
    }

    /** Return named supply domains in stable key order. */
    [[nodiscard]] const std::vector<ContractSupplyDomain> &supply_domains() const noexcept {
        return supply_domains_;
    }

    /** Return feature schemas in stable key order. */
    [[nodiscard]] const std::vector<FeatureSchema> &feature_schemas() const noexcept {
        return feature_schemas_;
    }

    /** Return feature bindings in stable key order. */
    [[nodiscard]] const std::vector<FeatureBinding> &feature_bindings() const noexcept {
        return feature_bindings_;
    }

    /** Return normalized canonical P1 record requirements after subject binding. */
    [[nodiscard]] const std::vector<BoundCanonicalRecordRequirement> &
    required_records() const noexcept {
        return required_records_;
    }

    /** Return whether portable contract content was authored explicitly. */
    [[nodiscard]] bool explicitly_authored() const noexcept { return explicitly_authored_; }

  private:
    ComponentContract(ComponentContractSpec spec, bool explicitly_authored);

    ComponentKey key_;
    std::vector<PinKey> pin_keys_;
    std::vector<ContractFramedPin> framed_pins_;
    std::vector<ContractDirectedRelation> relations_;
    std::vector<ContractSupplyDomain> supply_domains_;
    std::vector<FeatureSchema> feature_schemas_;
    std::vector<FeatureBinding> feature_bindings_;
    std::vector<BoundCanonicalRecordRequirement> required_records_;
    bool explicitly_authored_;
};

/** Standard supply-consumer schema lowered through the generic FeatureSchema model. */
[[nodiscard]] FeatureSchema supply_consumer_feature_schema();
/** Standard supply-source schema lowered through the generic FeatureSchema model. */
[[nodiscard]] FeatureSchema supply_source_feature_schema();
/** Standard diode-junction schema lowered through the generic FeatureSchema model. */
[[nodiscard]] FeatureSchema diode_junction_feature_schema();

} // namespace volt
