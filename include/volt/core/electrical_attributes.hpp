#pragma once

#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <volt/core/quantities.hpp>

namespace volt {

/** Stable name for a typed electrical attribute. */
class ElectricalAttributeName {
  public:
    /** Construct a non-empty electrical attribute name. */
    explicit ElectricalAttributeName(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Electrical attribute name must not be empty"};
        }
    }

    /** Return the stored name. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two names carry the same value. */
    [[nodiscard]] friend bool operator==(const ElectricalAttributeName &lhs,
                                         const ElectricalAttributeName &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

    /** Order names lexicographically by value. */
    [[nodiscard]] friend bool operator<(const ElectricalAttributeName &lhs,
                                        const ElectricalAttributeName &rhs) noexcept {
        return lhs.value_ < rhs.value_;
    }

  private:
    std::string value_;
};

/** Kernel model owner that may carry a typed electrical attribute. */
enum class ElectricalAttributeOwner {
    ComponentInstance,
    SelectedPart,
    PinSpec,
    Net,
    Constraint,
};

/** Meaning of an electrical attribute value. */
enum class ElectricalAttributeKind {
    DesignInput,
    Constraint,
};

/** Plain-number authoring unit metadata for an electrical attribute. */
class AuthoringUnit {
  public:
    /** Construct explicit scale metadata for plain numeric authoring values. */
    AuthoringUnit(UnitDimension dimension, double scale_to_canonical, std::string symbol)
        : dimension_{dimension}, scale_to_canonical_{scale_to_canonical},
          symbol_{std::move(symbol)} {
        if (!std::isfinite(scale_to_canonical_) || scale_to_canonical_ <= 0.0) {
            throw std::invalid_argument{"Authoring unit scale must be finite and positive"};
        }
        if (symbol_.empty()) {
            throw std::invalid_argument{"Authoring unit symbol must not be empty"};
        }
    }

    /** Return the unit dimension. */
    [[nodiscard]] UnitDimension dimension() const noexcept { return dimension_; }

    /** Return the multiplier from an authored number to the canonical quantity value. */
    [[nodiscard]] double scale_to_canonical() const noexcept { return scale_to_canonical_; }

    /** Return the user-facing unit symbol. */
    [[nodiscard]] const std::string &symbol() const noexcept { return symbol_; }

    /** Return whether two authoring units carry the same metadata. */
    [[nodiscard]] friend bool operator==(const AuthoringUnit &lhs,
                                         const AuthoringUnit &rhs) noexcept {
        return lhs.dimension_ == rhs.dimension_ &&
               lhs.scale_to_canonical_ == rhs.scale_to_canonical_ && lhs.symbol_ == rhs.symbol_;
    }

  private:
    UnitDimension dimension_;
    double scale_to_canonical_;
    std::string symbol_;
};

/** Typed attribute value payload kind. */
enum class ElectricalAttributeValueKind {
    Quantity,
    Tolerance,
    Range,
};

/** Typed electrical value that can be checked against an attribute spec. */
class ElectricalAttributeValue {
  public:
    /** Construct an attribute value from a quantity. */
    explicit ElectricalAttributeValue(Quantity value) : value_{value} {}

    /** Construct an attribute value from a tolerance. */
    explicit ElectricalAttributeValue(Tolerance value) : value_{value} {}

    /** Construct an attribute value from a quantity range. */
    explicit ElectricalAttributeValue(QuantityRange value) : value_{value} {}

    /** Return the stored value kind. */
    [[nodiscard]] ElectricalAttributeValueKind kind() const noexcept {
        if (std::holds_alternative<Quantity>(value_)) {
            return ElectricalAttributeValueKind::Quantity;
        }
        if (std::holds_alternative<Tolerance>(value_)) {
            return ElectricalAttributeValueKind::Tolerance;
        }
        return ElectricalAttributeValueKind::Range;
    }

    /** Return the value's electrical dimension. */
    [[nodiscard]] UnitDimension dimension() const noexcept {
        if (std::holds_alternative<Quantity>(value_)) {
            return std::get<Quantity>(value_).dimension();
        }
        if (std::holds_alternative<Tolerance>(value_)) {
            return std::get<Tolerance>(value_).minus().dimension();
        }
        return std::get<QuantityRange>(value_).dimension();
    }

    /** Return the stored quantity value. */
    [[nodiscard]] const Quantity &as_quantity() const { return std::get<Quantity>(value_); }

    /** Return the stored tolerance value. */
    [[nodiscard]] const Tolerance &as_tolerance() const { return std::get<Tolerance>(value_); }

    /** Return the stored range value. */
    [[nodiscard]] const QuantityRange &as_range() const { return std::get<QuantityRange>(value_); }

  private:
    std::variant<Quantity, Tolerance, QuantityRange> value_;
};

/** Declares one typed electrical attribute that may be stored by a future owner. */
class ElectricalAttributeSpec {
  public:
    /** Construct an electrical attribute spec. */
    ElectricalAttributeSpec(ElectricalAttributeName name, ElectricalAttributeOwner owner,
                            ElectricalAttributeKind kind, UnitDimension dimension,
                            std::optional<AuthoringUnit> default_authoring_unit = std::nullopt)
        : name_{std::move(name)}, owner_{owner}, kind_{kind}, dimension_{dimension},
          default_authoring_unit_{std::move(default_authoring_unit)} {
        if (default_authoring_unit_.has_value() &&
            default_authoring_unit_.value().dimension() != dimension_) {
            throw std::invalid_argument{
                "Electrical attribute default authoring unit dimension must match the spec"};
        }
    }

    /** Return the attribute name. */
    [[nodiscard]] const ElectricalAttributeName &name() const noexcept { return name_; }

    /** Return the model owner this attribute applies to. */
    [[nodiscard]] ElectricalAttributeOwner owner() const noexcept { return owner_; }

    /** Return whether this attribute is design input or a constraint. */
    [[nodiscard]] ElectricalAttributeKind kind() const noexcept { return kind_; }

    /** Return the expected value dimension. */
    [[nodiscard]] UnitDimension dimension() const noexcept { return dimension_; }

    /** Return optional plain-number authoring metadata. */
    [[nodiscard]] const std::optional<AuthoringUnit> &default_authoring_unit() const noexcept {
        return default_authoring_unit_;
    }

    /** Throw when a value is not compatible with the spec dimension. */
    void require_compatible(const ElectricalAttributeValue &value) const {
        if (value.dimension() != dimension_) {
            throw std::invalid_argument{"Electrical attribute value dimension does not match spec"};
        }
    }

  private:
    ElectricalAttributeName name_;
    ElectricalAttributeOwner owner_;
    ElectricalAttributeKind kind_;
    UnitDimension dimension_;
    std::optional<AuthoringUnit> default_authoring_unit_;
};

/** Deterministically ordered storage for typed electrical attribute values. */
class ElectricalAttributeMap {
  public:
    /** Construct an empty electrical attribute map. */
    ElectricalAttributeMap() = default;

    /** Set or replace an attribute value after checking it against the spec. */
    void set(const ElectricalAttributeSpec &spec, ElectricalAttributeValue value) {
        spec.require_compatible(value);
        entries_.insert_or_assign(spec.name(), std::move(value));
    }

    /** Return an attribute value or throw if the name is missing. */
    [[nodiscard]] const ElectricalAttributeValue &get(const ElectricalAttributeName &name) const {
        const auto it = entries_.find(name);
        if (it == entries_.end()) {
            throw std::out_of_range{"Electrical attribute is not present"};
        }

        return it->second;
    }

    /** Return whether an attribute exists for the name. */
    [[nodiscard]] bool contains(const ElectricalAttributeName &name) const noexcept {
        return entries_.find(name) != entries_.end();
    }

    /** Return whether the map contains no electrical attributes. */
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    /** Return the number of electrical attributes. */
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /** Return attributes in deterministic name order. */
    [[nodiscard]] const std::map<ElectricalAttributeName, ElectricalAttributeValue> &
    entries() const noexcept {
        return entries_;
    }

  private:
    std::map<ElectricalAttributeName, ElectricalAttributeValue> entries_;
};

} // namespace volt
