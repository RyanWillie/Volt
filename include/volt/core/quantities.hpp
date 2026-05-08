#pragma once

#include <cmath>
#include <optional>
#include <stdexcept>

namespace volt {

/** Electrical quantity dimensions understood by the kernel. */
enum class UnitDimension {
    Resistance,
    Capacitance,
    Inductance,
    Voltage,
    Current,
    Power,
    Frequency,
    Time,
    Temperature,
    Ratio,
};

/** Canonical scalar quantity tagged with its electrical dimension. */
class Quantity {
  public:
    /** Construct a finite quantity value for the given dimension. */
    Quantity(UnitDimension dimension, double value) : dimension_{dimension}, value_{value} {
        if (!std::isfinite(value_)) {
            throw std::invalid_argument{"Quantity value must be finite"};
        }
    }

    /** Return the quantity dimension. */
    [[nodiscard]] UnitDimension dimension() const noexcept { return dimension_; }

    /** Return the canonical numeric value. */
    [[nodiscard]] double value() const noexcept { return value_; }

    /** Return whether two quantities have the same dimension and value. */
    [[nodiscard]] friend bool operator==(const Quantity &lhs, const Quantity &rhs) noexcept {
        return lhs.dimension_ == rhs.dimension_ && lhs.value_ == rhs.value_;
    }

  private:
    UnitDimension dimension_;
    double value_;
};

/** Tolerance interpretation. */
enum class ToleranceMode {
    Absolute,
    Percent,
};

/** Plus/minus tolerance around a nominal design value. */
class Tolerance {
  public:
    /** Construct an absolute tolerance with compatible plus/minus magnitudes. */
    [[nodiscard]] static Tolerance absolute(Quantity minus, Quantity plus) {
        if (minus.dimension() != plus.dimension()) {
            throw std::invalid_argument{"Absolute tolerance dimensions must match"};
        }
        validate_non_negative(minus);
        validate_non_negative(plus);
        return Tolerance{ToleranceMode::Absolute, minus, plus};
    }

    /** Construct a symmetric percent tolerance from a ratio value such as 0.01 for 1%. */
    [[nodiscard]] static Tolerance percent(double value) { return percent(value, value); }

    /** Construct an asymmetric percent tolerance from ratio values. */
    [[nodiscard]] static Tolerance percent(double minus, double plus) {
        return absolute_or_percent(ToleranceMode::Percent, Quantity{UnitDimension::Ratio, minus},
                                   Quantity{UnitDimension::Ratio, plus});
    }

    /** Return whether this is an absolute or percent tolerance. */
    [[nodiscard]] ToleranceMode mode() const noexcept { return mode_; }

    /** Return the negative-side tolerance magnitude. */
    [[nodiscard]] const Quantity &minus() const noexcept { return minus_; }

    /** Return the positive-side tolerance magnitude. */
    [[nodiscard]] const Quantity &plus() const noexcept { return plus_; }

  private:
    Tolerance(ToleranceMode mode, Quantity minus, Quantity plus)
        : mode_{mode}, minus_{minus}, plus_{plus} {}

    [[nodiscard]] static Tolerance absolute_or_percent(ToleranceMode mode, Quantity minus,
                                                       Quantity plus) {
        validate_non_negative(minus);
        validate_non_negative(plus);
        return Tolerance{mode, minus, plus};
    }

    static void validate_non_negative(const Quantity &quantity) {
        if (quantity.value() < 0.0) {
            throw std::invalid_argument{"Tolerance magnitude must not be negative"};
        }
    }

    ToleranceMode mode_;
    Quantity minus_;
    Quantity plus_;
};

/** Optional lower and/or upper bounds for a dimensioned quantity. */
class QuantityRange {
  public:
    /** Construct a range with both minimum and maximum bounds. */
    [[nodiscard]] static QuantityRange bounded(Quantity minimum, Quantity maximum) {
        if (minimum.dimension() != maximum.dimension()) {
            throw std::invalid_argument{"Quantity range dimensions must match"};
        }
        if (minimum.value() > maximum.value()) {
            throw std::invalid_argument{"Quantity range minimum must not exceed maximum"};
        }
        return QuantityRange{minimum.dimension(), minimum, maximum};
    }

    /** Construct a range with only a minimum bound. */
    [[nodiscard]] static QuantityRange minimum(Quantity minimum) {
        return QuantityRange{minimum.dimension(), minimum, std::nullopt};
    }

    /** Construct a range with only a maximum bound. */
    [[nodiscard]] static QuantityRange maximum(Quantity maximum) {
        return QuantityRange{maximum.dimension(), std::nullopt, maximum};
    }

    /** Return the range dimension. */
    [[nodiscard]] UnitDimension dimension() const noexcept { return dimension_; }

    /** Return the optional minimum bound. */
    [[nodiscard]] const std::optional<Quantity> &minimum() const noexcept { return minimum_; }

    /** Return the optional maximum bound. */
    [[nodiscard]] const std::optional<Quantity> &maximum() const noexcept { return maximum_; }

  private:
    QuantityRange(UnitDimension dimension, std::optional<Quantity> minimum,
                  std::optional<Quantity> maximum)
        : dimension_{dimension}, minimum_{minimum}, maximum_{maximum} {}

    UnitDimension dimension_;
    std::optional<Quantity> minimum_;
    std::optional<Quantity> maximum_;
};

} // namespace volt
