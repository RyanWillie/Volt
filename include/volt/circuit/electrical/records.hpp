#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

#include <volt/core/content_hash.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/quantities.hpp>

namespace volt {

/** Record-set-local index into an ordered component pin contract. */
class ElectricalPinIndex {
  public:
    /** Construct a local pin index. */
    explicit ElectricalPinIndex(std::size_t value) noexcept : value_{value} {}

    /** Return the zero-based local pin index. */
    [[nodiscard]] std::size_t value() const noexcept { return value_; }

    /** Compare local pin indices by numeric position. */
    [[nodiscard]] friend auto operator<=>(ElectricalPinIndex lhs,
                                          ElectricalPinIndex rhs) noexcept = default;

  private:
    std::size_t value_;
};

/** Closed canonical electrical subject shapes admitted by semantic model v1. */
enum class ElectricalSubjectKind {
    FramedPin,        ///< One pin measured relative to another.
    DirectedRelation, ///< One explicitly directed pin pair.
    SupplyDomain,     ///< Positive and return pin sets for one supply.
};

/** One pin measured relative to a distinct reference pin. */
struct FramedPinSubject {
    /** Pin whose quantity is described. */
    ElectricalPinIndex pin;
    /** Pin used as the measurement reference. */
    ElectricalPinIndex reference;

    /** Compare framed-pin subjects by both local indices. */
    [[nodiscard]] friend bool operator==(const FramedPinSubject &,
                                         const FramedPinSubject &) noexcept = default;
};

/** One explicitly oriented from/positive pin to a distinct to/negative pin. */
struct DirectedRelationSubject {
    /** Positive or source side of the relation. */
    ElectricalPinIndex from;
    /** Negative or destination side of the relation. */
    ElectricalPinIndex to;

    /** Compare directed relations without losing orientation. */
    [[nodiscard]] friend bool operator==(const DirectedRelationSubject &,
                                         const DirectedRelationSubject &) noexcept = default;
};

/** Non-empty disjoint positive and return pin sets for one supply domain. */
class SupplyDomainSubject {
  public:
    /** Construct a domain and normalize each unordered pin side. */
    SupplyDomainSubject(std::vector<ElectricalPinIndex> positive_pins,
                        std::vector<ElectricalPinIndex> return_pins);

    /** Return sorted positive-side pins. */
    [[nodiscard]] const std::vector<ElectricalPinIndex> &positive_pins() const noexcept {
        return positive_pins_;
    }

    /** Return sorted return-side pins. */
    [[nodiscard]] const std::vector<ElectricalPinIndex> &return_pins() const noexcept {
        return return_pins_;
    }

    /** Compare normalized supply-domain pin sets. */
    [[nodiscard]] friend bool operator==(const SupplyDomainSubject &,
                                         const SupplyDomainSubject &) noexcept = default;

  private:
    std::vector<ElectricalPinIndex> positive_pins_;
    std::vector<ElectricalPinIndex> return_pins_;
};

/** Explicitly framed subject for canonical Voltage or Current records. */
class ElectricalSubject {
  public:
    /** Construct a pin relative to a distinct reference pin. */
    [[nodiscard]] static ElectricalSubject framed_pin(ElectricalPinIndex pin,
                                                      ElectricalPinIndex reference);

    /** Construct an oriented relation from one distinct pin to another. */
    [[nodiscard]] static ElectricalSubject directed_relation(ElectricalPinIndex from,
                                                             ElectricalPinIndex to);

    /** Construct a supply domain with non-empty disjoint pin sides. */
    [[nodiscard]] static ElectricalSubject
    supply_domain(std::vector<ElectricalPinIndex> positive_pins,
                  std::vector<ElectricalPinIndex> return_pins);

    /** Return the closed subject variant. */
    [[nodiscard]] ElectricalSubjectKind kind() const noexcept;

    /** Return the framed-pin payload. */
    [[nodiscard]] const FramedPinSubject &as_framed_pin() const;

    /** Return the directed-relation payload. */
    [[nodiscard]] const DirectedRelationSubject &as_directed_relation() const;

    /** Return the supply-domain payload. */
    [[nodiscard]] const SupplyDomainSubject &as_supply_domain() const;

    /** Compare subjects by their closed variant and payload. */
    [[nodiscard]] friend bool operator==(const ElectricalSubject &,
                                         const ElectricalSubject &) noexcept = default;

  private:
    explicit ElectricalSubject(FramedPinSubject subject) : value_{subject} {}

    explicit ElectricalSubject(DirectedRelationSubject subject) : value_{subject} {}

    explicit ElectricalSubject(SupplyDomainSubject subject) : value_{std::move(subject)} {}

    std::variant<FramedPinSubject, DirectedRelationSubject, SupplyDomainSubject> value_;
};

/** Closed canonical observables admitted by semantic model v1. */
enum class ElectricalObservable {
    Voltage, ///< Electric potential difference.
    Current, ///< Electric charge flow.
};

/** Closed canonical meanings admitted by semantic model v1. */
enum class ElectricalMeaning {
    Characteristic, ///< Typical device behavior.
    AcceptedRange,  ///< Operating range the subject accepts.
    ProvidedRange,  ///< Operating range the subject provides.
    AbsoluteLimit,  ///< Non-operating survival limit.
    Requirement,    ///< Continuous current the subject requires.
    Capability,     ///< Continuous current the subject can provide.
};

/** Typed base selector used by native queries and bounded value references. */
struct ElectricalRecordSelector {
    /** Subject selected by the query or condition reference. */
    ElectricalSubject subject;
    /** Observable selected by the query or condition reference. */
    ElectricalObservable observable;
    /** Meaning selected by the query or condition reference. */
    ElectricalMeaning meaning;

    /** Compare all selector fields. */
    [[nodiscard]] friend bool operator==(const ElectricalRecordSelector &,
                                         const ElectricalRecordSelector &) noexcept = default;
};

/** Deterministic identity of one semantic-key group. */
class ElectricalSemanticKey {
  public:
    /** Construct a semantic key from its canonical SHA-256 label. */
    explicit ElectricalSemanticKey(ContentHash hash) : hash_{std::move(hash)} {}

    /** Return the canonical hash label. */
    [[nodiscard]] const ContentHash &hash() const noexcept { return hash_; }

    /** Compare semantic-key hash labels. */
    [[nodiscard]] friend bool operator==(const ElectricalSemanticKey &,
                                         const ElectricalSemanticKey &) noexcept = default;

    /** Order semantic keys lexicographically by canonical hash label. */
    [[nodiscard]] friend std::strong_ordering
    operator<=>(const ElectricalSemanticKey &lhs, const ElectricalSemanticKey &rhs) noexcept {
        return lhs.hash_.value() <=> rhs.hash_.value();
    }

  private:
    ContentHash hash_;
};

/** Explicit typed Unknown value state. */
struct UnknownElectricalValue {
    /** Compare explicit Unknown markers. */
    [[nodiscard]] friend bool operator==(UnknownElectricalValue,
                                         UnknownElectricalValue) noexcept = default;
};

/** Ordered minimum/typical/maximum characteristic envelope. */
class CharacteristicEnvelope {
  public:
    /** Construct an ordered same-dimension characteristic envelope. */
    CharacteristicEnvelope(Quantity minimum, Quantity typical, Quantity maximum);

    /** Return the characteristic minimum. */
    [[nodiscard]] const Quantity &minimum() const noexcept { return minimum_; }

    /** Return the characteristic typical value. */
    [[nodiscard]] const Quantity &typical() const noexcept { return typical_; }

    /** Return the characteristic maximum. */
    [[nodiscard]] const Quantity &maximum() const noexcept { return maximum_; }

    /** Compare all three characteristic values. */
    [[nodiscard]] friend bool operator==(const CharacteristicEnvelope &,
                                         const CharacteristicEnvelope &) noexcept = default;

  private:
    Quantity minimum_;
    Quantity typical_;
    Quantity maximum_;
};

/** Nominal quantity plus authoring tolerance, normalized before storage. */
class TolerancedQuantity {
  public:
    /** Construct a nominal quantity with a compatible absolute or percent tolerance. */
    TolerancedQuantity(Quantity nominal, Tolerance tolerance);

    /** Return the authored nominal quantity. */
    [[nodiscard]] const Quantity &nominal() const noexcept { return nominal_; }

    /** Return the authored absolute or percent tolerance. */
    [[nodiscard]] const Tolerance &tolerance() const noexcept { return tolerance_; }

  private:
    Quantity nominal_;
    Tolerance tolerance_;
};

/** Non-negative continuous Current magnitude for Requirement or Capability. */
class ContinuousCurrent {
  public:
    /** Construct a non-negative Current quantity. */
    explicit ContinuousCurrent(Quantity value);

    /** Return the non-negative Current quantity. */
    [[nodiscard]] const Quantity &value() const noexcept { return value_; }

    /** Compare continuous-current magnitudes. */
    [[nodiscard]] friend bool operator==(const ContinuousCurrent &,
                                         const ContinuousCurrent &) noexcept = default;

  private:
    Quantity value_;
};

/** Closed value payload kinds, including the authoring-only tolerance input. */
enum class ElectricalValueKind {
    Unknown,                ///< Explicitly unknown value.
    Quantity,               ///< Scalar Voltage or Current.
    CharacteristicEnvelope, ///< Ordered minimum, typical, and maximum.
    Range,                  ///< One- or two-sided quantity range.
    ContinuousCurrent,      ///< Non-negative continuous Current.
    TolerancedQuantity,     ///< Authoring-only nominal and tolerance input.
};

/** Typed Voltage or Current value normalized according to record meaning. */
class ElectricalValue {
  public:
    /** Construct an explicit Unknown value. */
    explicit ElectricalValue(UnknownElectricalValue value) : value_{value} {}

    /** Construct a scalar Voltage or Current. */
    explicit ElectricalValue(Quantity value) : value_{value} {}

    /** Construct a characteristic envelope. */
    explicit ElectricalValue(CharacteristicEnvelope value) : value_{value} {}

    /** Construct a one- or two-sided quantity range. */
    explicit ElectricalValue(QuantityRange value) : value_{value} {}

    /** Construct a non-negative continuous Current value. */
    explicit ElectricalValue(ContinuousCurrent value) : value_{value} {}

    /** Construct an authoring-only nominal and tolerance input. */
    explicit ElectricalValue(TolerancedQuantity value) : value_{value} {}

    /** Return the closed value variant. */
    [[nodiscard]] ElectricalValueKind kind() const noexcept;
    /** Return the scalar quantity payload. */
    [[nodiscard]] const Quantity &as_quantity() const;
    /** Return the characteristic envelope payload. */
    [[nodiscard]] const CharacteristicEnvelope &as_characteristic_envelope() const;
    /** Return the quantity-range payload. */
    [[nodiscard]] const QuantityRange &as_range() const;
    /** Return the continuous-Current payload. */
    [[nodiscard]] const ContinuousCurrent &as_continuous_current() const;
    /** Return the authoring-only tolerance payload. */
    [[nodiscard]] const TolerancedQuantity &as_toleranced_quantity() const;

    /** Compare values by variant and payload. */
    friend bool operator==(const ElectricalValue &lhs, const ElectricalValue &rhs) noexcept;

  private:
    std::variant<UnknownElectricalValue, Quantity, CharacteristicEnvelope, QuantityRange,
                 ContinuousCurrent, TolerancedQuantity>
        value_;
};

/** One bounded, dimension-preserving reference multiplied by a finite scalar. */
class ScaledElectricalValueReference {
  public:
    /** Construct a reference to one unambiguous scalar record selector. */
    ScaledElectricalValueReference(ElectricalRecordSelector selector, double scale);

    /** Return the referenced record selector. */
    [[nodiscard]] const ElectricalRecordSelector &selector() const noexcept { return selector_; }

    /** Return the finite dimension-preserving scale. */
    [[nodiscard]] double scale() const noexcept { return scale_; }

    /** Compare selector and scale. */
    [[nodiscard]] friend bool operator==(const ScaledElectricalValueReference &,
                                         const ScaledElectricalValueReference &) noexcept = default;

  private:
    ElectricalRecordSelector selector_;
    double scale_;
};

/** Literal quantity or one bounded reference-times-scalar condition expression. */
class ElectricalValueExpression {
  public:
    /** Construct a literal quantity expression. */
    [[nodiscard]] static ElectricalValueExpression literal(Quantity value);
    /** Construct a bounded reference-times-scalar expression. */
    [[nodiscard]] static ElectricalValueExpression
    scaled_reference(ElectricalRecordSelector selector, double scale);

    /** Return whether this expression holds a literal quantity. */
    [[nodiscard]] bool is_literal() const noexcept;
    /** Return the literal quantity payload. */
    [[nodiscard]] const Quantity &as_literal() const;
    /** Return the scaled-reference payload. */
    [[nodiscard]] const ScaledElectricalValueReference &as_scaled_reference() const;

    /** Compare expressions by variant and payload. */
    [[nodiscard]] friend bool operator==(const ElectricalValueExpression &,
                                         const ElectricalValueExpression &) noexcept = default;

  private:
    explicit ElectricalValueExpression(Quantity value) : value_{value} {}

    explicit ElectricalValueExpression(ScaledElectricalValueReference value)
        : value_{std::move(value)} {}

    std::variant<Quantity, ScaledElectricalValueReference> value_;
};

/** Equality or one-/two-sided range condition predicate. */
enum class ElectricalConditionPredicateKind {
    Equal, ///< Exact equality.
    Range, ///< One- or two-sided inclusive range.
};

/** One typed operating condition over an explicitly framed subject. */
class ElectricalCondition {
  public:
    /** Construct an equality condition. */
    [[nodiscard]] static ElectricalCondition equal(ElectricalSubject subject,
                                                   ElectricalObservable observable,
                                                   ElectricalValueExpression value);

    /** Construct a one- or two-sided range condition. */
    [[nodiscard]] static ElectricalCondition
    range(ElectricalSubject subject, ElectricalObservable observable,
          std::optional<ElectricalValueExpression> minimum,
          std::optional<ElectricalValueExpression> maximum);

    /** Return the condition subject. */
    [[nodiscard]] const ElectricalSubject &subject() const noexcept { return subject_; }

    /** Return the condition observable. */
    [[nodiscard]] ElectricalObservable observable() const noexcept { return observable_; }

    /** Return the equality or range predicate kind. */
    [[nodiscard]] ElectricalConditionPredicateKind predicate_kind() const noexcept {
        return predicate_kind_;
    }

    /** Return the equality value or inclusive range minimum. */
    [[nodiscard]] const std::optional<ElectricalValueExpression> &minimum() const noexcept {
        return minimum_;
    }

    /** Return the equality value or inclusive range maximum. */
    [[nodiscard]] const std::optional<ElectricalValueExpression> &maximum() const noexcept {
        return maximum_;
    }

    /** Compare the normalized condition shape and values. */
    [[nodiscard]] friend bool operator==(const ElectricalCondition &,
                                         const ElectricalCondition &) noexcept = default;

  private:
    ElectricalCondition(ElectricalSubject subject, ElectricalObservable observable,
                        ElectricalConditionPredicateKind predicate_kind,
                        std::optional<ElectricalValueExpression> minimum,
                        std::optional<ElectricalValueExpression> maximum);

    ElectricalSubject subject_;
    ElectricalObservable observable_;
    ElectricalConditionPredicateKind predicate_kind_;
    std::optional<ElectricalValueExpression> minimum_;
    std::optional<ElectricalValueExpression> maximum_;
};

/** Complete generic canonical record input. */
struct ElectricalRecordSpec {
    /** Subject whose behavior is claimed. */
    ElectricalSubject subject;
    /** Voltage or Current observable. */
    ElectricalObservable observable;
    /** Semantic meaning of the claim. */
    ElectricalMeaning meaning;
    /** Typed claim value or explicit Unknown. */
    ElectricalValue value;
    /** Bounded operating conditions for the claim. */
    std::vector<ElectricalCondition> conditions = {};
    /** Immutable content hashes supporting the claim. */
    std::vector<ContentHash> evidence = {};
};

/** Compute the deterministic normalized semantic key for one structurally valid record input. */
[[nodiscard]] ElectricalSemanticKey electrical_semantic_key(ElectricalRecordSpec record);

/** Build one canonical Voltage record through the generic record shape. */
[[nodiscard]] ElectricalRecordSpec voltage_record(ElectricalSubject subject,
                                                  ElectricalMeaning meaning, ElectricalValue value,
                                                  std::vector<ElectricalCondition> conditions = {},
                                                  std::vector<ContentHash> evidence = {});

/** Build one canonical Current record through the generic record shape. */
[[nodiscard]] ElectricalRecordSpec current_record(ElectricalSubject subject,
                                                  ElectricalMeaning meaning, ElectricalValue value,
                                                  std::vector<ElectricalCondition> conditions = {},
                                                  std::vector<ContentHash> evidence = {});

/** One normalized canonical source record. */
class ElectricalRecord {
  public:
    /** Normalize and structurally validate one source record independent of owner references. */
    [[nodiscard]] static ElectricalRecord from(ElectricalRecordSpec record);

    /** Return the deterministic normalized semantic key. */
    [[nodiscard]] const ElectricalSemanticKey &semantic_key() const noexcept {
        return semantic_key_;
    }

    /** Return the normalized record subject. */
    [[nodiscard]] const ElectricalSubject &subject() const noexcept { return subject_; }

    /** Return the Voltage or Current observable. */
    [[nodiscard]] ElectricalObservable observable() const noexcept { return observable_; }

    /** Return the record meaning. */
    [[nodiscard]] ElectricalMeaning meaning() const noexcept { return meaning_; }

    /** Return the normalized typed value or explicit Unknown. */
    [[nodiscard]] const ElectricalValue &value() const noexcept { return value_; }

    /** Return the normalized bounded conditions. */
    [[nodiscard]] const std::vector<ElectricalCondition> &conditions() const noexcept {
        return conditions_;
    }

    /** Return sorted unique evidence content hashes. */
    [[nodiscard]] const std::vector<ContentHash> &evidence() const noexcept { return evidence_; }

  private:
    ElectricalRecord(ElectricalSemanticKey semantic_key, ElectricalSubject subject,
                     ElectricalObservable observable, ElectricalMeaning meaning,
                     ElectricalValue value, std::vector<ElectricalCondition> conditions,
                     std::vector<ContentHash> evidence);

    ElectricalSemanticKey semantic_key_;
    ElectricalSubject subject_;
    ElectricalObservable observable_;
    ElectricalMeaning meaning_;
    ElectricalValue value_;
    std::vector<ElectricalCondition> conditions_;
    std::vector<ContentHash> evidence_;
};

/** Append-only canonical record owner validated against one ordered pin contract. */
class ElectricalRecordSet {
  public:
    /** Build and atomically validate a complete record set. */
    ElectricalRecordSet(std::size_t pin_count, std::vector<ElectricalRecordSpec> records = {});

    /** Return a copy with one appended record after validating the complete result. */
    [[nodiscard]] ElectricalRecordSet with_record(ElectricalRecordSpec record) const;

    /** Return the ordered component pin-contract size. */
    [[nodiscard]] std::size_t pin_count() const noexcept { return pin_count_; }

    /** Return deterministic normalized source records. */
    [[nodiscard]] const std::vector<ElectricalRecord> &records() const noexcept { return records_; }

  private:
    std::size_t pin_count_;
    std::vector<ElectricalRecord> records_;
};

/** Effective merge status for one semantic key. */
enum class ElectricalMergeStatus {
    Effective, ///< A known effective value was derived.
    Unknown,   ///< No known value was available.
    Conflict,  ///< Known values contradict one another.
};

/** Native query result for one normalized semantic-key group. */
class ElectricalRecordGroup {
  public:
    /** Derive one deterministic typed query result from same-key normalized source records. */
    [[nodiscard]] static ElectricalRecordGroup from(std::vector<ElectricalRecord> source_records);

    /** Return the shared normalized semantic key. */
    [[nodiscard]] const ElectricalSemanticKey &semantic_key() const noexcept {
        return semantic_key_;
    }

    /** Return the shared subject, observable, and meaning selector. */
    [[nodiscard]] const ElectricalRecordSelector &selector() const noexcept { return selector_; }

    /** Return the shared normalized conditions. */
    [[nodiscard]] const std::vector<ElectricalCondition> &conditions() const noexcept {
        return conditions_;
    }

    /** Return source claims contributing to this semantic-key group. */
    [[nodiscard]] const std::vector<ElectricalRecord> &source_records() const noexcept {
        return source_records_;
    }

    /** Return the effective, Unknown, or conflict merge status. */
    [[nodiscard]] ElectricalMergeStatus status() const noexcept { return status_; }

    /** Return the effective value when the group has one. */
    [[nodiscard]] const std::optional<ElectricalValue> &effective_value() const noexcept {
        return effective_value_;
    }

    /** Return whether any contributing claim explicitly used Unknown. */
    [[nodiscard]] bool has_unknown() const noexcept { return has_unknown_; }

    /** Return sorted unique evidence from all contributing claims. */
    [[nodiscard]] const std::vector<ContentHash> &evidence() const noexcept { return evidence_; }

  private:
    ElectricalRecordGroup(ElectricalSemanticKey semantic_key, ElectricalRecordSelector selector,
                          std::vector<ElectricalCondition> conditions,
                          std::vector<ElectricalRecord> source_records,
                          ElectricalMergeStatus status,
                          std::optional<ElectricalValue> effective_value, bool has_unknown,
                          std::vector<ContentHash> evidence);

    ElectricalSemanticKey semantic_key_;
    ElectricalRecordSelector selector_;
    std::vector<ElectricalCondition> conditions_;
    std::vector<ElectricalRecord> source_records_;
    ElectricalMergeStatus status_;
    std::optional<ElectricalValue> effective_value_;
    bool has_unknown_;
    std::vector<ContentHash> evidence_;
};

/** Report Unknown and contradictory but structurally valid record groups. */
[[nodiscard]] DiagnosticReport validate_electrical_records(const ElectricalRecordSet &records);

} // namespace volt
