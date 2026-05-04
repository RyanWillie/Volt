#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace volt {

/** Stable key used to name extensible metadata properties. */
class PropertyKey {
  public:
    /** Construct a non-empty property key. */
    explicit PropertyKey(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Property key must not be empty"};
        }
    }

    /** Return the stored key string. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two property keys carry the same value. */
    [[nodiscard]] friend bool operator==(const PropertyKey &lhs, const PropertyKey &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

    /** Order property keys lexicographically by value. */
    [[nodiscard]] friend bool operator<(const PropertyKey &lhs, const PropertyKey &rhs) noexcept {
        return lhs.value_ < rhs.value_;
    }

  private:
    std::string value_;
};

/** Scalar kind stored by a property value. */
enum class PropertyValueKind {
    String,
    Boolean,
    Integer,
    Number,
};

/** Typed scalar metadata value. */
class PropertyValue {
  public:
    /** Construct a string property value. */
    explicit PropertyValue(std::string value) : value_{std::move(value)} {}

    /** Construct a string property value from a string literal. */
    explicit PropertyValue(const char *value) : value_{std::string{value}} {}

    /** Construct a boolean property value. */
    explicit PropertyValue(bool value) : value_{value} {}

    /** Construct a signed integer property value. */
    explicit PropertyValue(std::int64_t value) : value_{value} {}

    /** Construct a double-precision numeric property value. */
    explicit PropertyValue(double value) : value_{value} {}

    /** Return the stored scalar kind. */
    [[nodiscard]] PropertyValueKind kind() const noexcept {
        if (std::holds_alternative<std::string>(value_)) {
            return PropertyValueKind::String;
        }
        if (std::holds_alternative<bool>(value_)) {
            return PropertyValueKind::Boolean;
        }
        if (std::holds_alternative<std::int64_t>(value_)) {
            return PropertyValueKind::Integer;
        }

        return PropertyValueKind::Number;
    }

    /** Return the stored string value. */
    [[nodiscard]] const std::string &as_string() const { return std::get<std::string>(value_); }

    /** Return the stored boolean value. */
    [[nodiscard]] bool as_bool() const { return std::get<bool>(value_); }

    /** Return the stored signed integer value. */
    [[nodiscard]] std::int64_t as_integer() const { return std::get<std::int64_t>(value_); }

    /** Return the stored double-precision numeric value. */
    [[nodiscard]] double as_number() const { return std::get<double>(value_); }

    /** Return whether two property values have the same kind and payload. */
    [[nodiscard]] friend bool operator==(const PropertyValue &lhs,
                                         const PropertyValue &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::variant<std::string, bool, std::int64_t, double> value_;
};

/** Deterministically ordered map of metadata properties. */
class PropertyMap {
  public:
    /** Ordered property entry type. */
    using Entry = std::pair<PropertyKey, PropertyValue>;

    /** Construct an empty property map. */
    PropertyMap() = default;

    /** Construct a property map from key/value entries. */
    PropertyMap(std::initializer_list<Entry> entries) {
        for (const auto &entry : entries) {
            set(entry.first, entry.second);
        }
    }

    /** Set or replace a property value. */
    void set(PropertyKey key, PropertyValue value) {
        entries_.insert_or_assign(std::move(key), std::move(value));
    }

    /** Return a property value or throw if the key is missing. */
    [[nodiscard]] const PropertyValue &get(const PropertyKey &key) const {
        const auto it = entries_.find(key);
        if (it == entries_.end()) {
            throw std::out_of_range{"Property key is not present"};
        }

        return it->second;
    }

    /** Return whether a property exists for the key. */
    [[nodiscard]] bool contains(const PropertyKey &key) const noexcept {
        return entries_.find(key) != entries_.end();
    }

    /** Return whether the map contains no properties. */
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    /** Return the number of properties in the map. */
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /** Return properties in deterministic key order. */
    [[nodiscard]] const std::map<PropertyKey, PropertyValue> &entries() const noexcept {
        return entries_;
    }

  private:
    std::map<PropertyKey, PropertyValue> entries_;
};

} // namespace volt
