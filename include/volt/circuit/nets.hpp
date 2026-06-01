#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>

namespace volt {

/** Human-facing canonical net name, such as GND, 3V3, or LED_A. */
class NetName {
  public:
    /** Construct a non-empty net name. */
    explicit NetName(std::string value);

    /** Return the stored net name string. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two net names carry the same value. */
    [[nodiscard]] friend bool operator==(const NetName &lhs, const NetName &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::string value_;
};

/** Broad classification for a canonical logical net. */
enum class NetKind {
    Signal,
    Power,
    Ground,
    Clock,
    Analog,
    HighCurrent,
};

/** Canonical logical net containing concrete pin instances. */
class Net {
  public:
    /** Construct a net with a name and broad classification. */
    Net(NetName name, NetKind kind);

    /** Return the human-facing net name. */
    [[nodiscard]] const NetName &name() const noexcept { return name_; }

    /** Return the net classification. */
    [[nodiscard]] NetKind kind() const noexcept { return kind_; }

    /** Return concrete pins connected to this net in deterministic insertion order. */
    [[nodiscard]] const std::vector<PinId> &pins() const noexcept { return pins_; }

    /** Return typed electrical attributes for this net. */
    [[nodiscard]] const ElectricalAttributeMap &electrical_attributes() const noexcept;

    /** Return whether the pin is already connected to this net. */
    [[nodiscard]] bool contains(PinId pin) const noexcept;

    /** Connect a pin if it is not already present; returns true when the net changed. */
    bool connect(PinId pin);

    /** Disconnect a pin if present; returns true when the net changed. */
    bool disconnect(PinId pin);

  private:
    friend class Circuit;

    void set_electrical_attribute(const ElectricalAttributeSpec &spec,
                                  ElectricalAttributeValue value);

    NetName name_;
    NetKind kind_;
    std::vector<PinId> pins_;
    ElectricalAttributeMap electrical_attributes_;
};

} // namespace volt
