#pragma once

#include <compare>
#include <string>

#include <volt/electrical/electrical_input.hpp>

namespace volt {

/** Strongly typed non-empty identity local to one electrical request collection. */
template <typename Tag> class ElectricalRequestKeyValue {
  public:
    /** Construct a stable request-local key. */
    explicit ElectricalRequestKeyValue(std::string value);

    /** Return the stable key spelling. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare keys in the same request-local collection. */
    [[nodiscard]] bool operator==(const ElectricalRequestKeyValue &) const noexcept = default;
    /** Order keys for deterministic request normalization. */
    [[nodiscard]] std::strong_ordering
    operator<=>(const ElectricalRequestKeyValue &) const noexcept = default;

  private:
    std::string value_;
};

/** Type tag for request identities. */
struct ElectricalRequestKeyTag;
/** Type tag for independent-source identities. */
struct ElectricalSourceKeyTag;
/** Type tag for probe identities. */
struct ElectricalProbeKeyTag;
/** Stable identity of one electrical request. */
using ElectricalRequestKey = ElectricalRequestKeyValue<ElectricalRequestKeyTag>;
/** Stable identity of one independent source within a request. */
using ElectricalSourceKey = ElectricalRequestKeyValue<ElectricalSourceKeyTag>;
/** Stable identity of one requested observation within a request. */
using ElectricalProbeKey = ElectricalRequestKeyValue<ElectricalProbeKeyTag>;

/** One ordered pair of exact-input logical nets. */
class ElectricalNetPair {
  public:
    /** Construct an oriented pair from references to the same exact input. */
    ElectricalNetPair(ElectricalNetRef from, ElectricalNetRef to);

    /** Return the positive-voltage or outgoing-current net. */
    [[nodiscard]] const ElectricalNetRef &from() const noexcept { return from_; }

    /** Return the negative-voltage or incoming-current net. */
    [[nodiscard]] const ElectricalNetRef &to() const noexcept { return to_; }

    /** Compare exact input, authored endpoints, and orientation. */
    [[nodiscard]] bool operator==(const ElectricalNetPair &) const noexcept = default;

  private:
    ElectricalNetRef from_;
    ElectricalNetRef to_;
};

} // namespace volt
