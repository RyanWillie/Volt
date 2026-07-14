#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
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
    Net(NetName name, NetKind kind, ElectricalAttributeMap electrical_attributes = {});

    /** Return the human-facing net name. */
    [[nodiscard]] const NetName &name() const noexcept { return name_; }

    /** Return the net classification. */
    [[nodiscard]] NetKind kind() const noexcept { return kind_; }

    /** Return concrete pins connected to this net in deterministic insertion order. */
    [[nodiscard]] const std::vector<PinId> &pins() const noexcept { return pins_; }

    /** Return typed electrical attributes owned by this logical net. */
    [[nodiscard]] const ElectricalAttributeMap &electrical_attributes() const noexcept {
        return electrical_attributes_;
    }

    /** Return whether the author intentionally declared this net as a stub. */
    [[nodiscard]] bool intentional_stub() const noexcept { return intentional_stub_; }

    /** Return the first-authored order of this net's intentional-stub marker. */
    [[nodiscard]] const std::optional<std::size_t> &intentional_stub_order() const noexcept {
        return intentional_stub_order_;
    }

    /** Return the explicitly assigned net class, if one exists. */
    [[nodiscard]] const std::optional<NetClassId> &net_class() const noexcept { return net_class_; }

    /** Return the first-authored order of this net's class assignment. */
    [[nodiscard]] const std::optional<std::size_t> &net_class_assignment_order() const noexcept {
        return net_class_assignment_order_;
    }

    /** Return whether the pin is already connected to this net. */
    [[nodiscard]] bool contains(PinId pin) const noexcept;

    /** Connect a pin if it is not already present; returns true when the net changed. */
    bool connect(PinId pin);

    /** Disconnect a pin if present; returns true when the net changed. */
    bool disconnect(PinId pin);

    /** Return a copy with one net electrical attribute set or replaced. */
    [[nodiscard]] Net with_electrical_attribute(const ElectricalAttributeSpec &spec,
                                                ElectricalAttributeValue value) const;

    /** Return a copy marked as an intentional stub. */
    [[nodiscard]] Net with_intentional_stub(std::size_t first_authored_order) const;

    /** Return a copy assigned to the requested net class. */
    [[nodiscard]] Net with_net_class(NetClassId net_class, std::size_t first_authored_order) const;

  private:
    NetName name_;
    NetKind kind_;
    std::vector<PinId> pins_;
    ElectricalAttributeMap electrical_attributes_;
    bool intentional_stub_;
    std::optional<std::size_t> intentional_stub_order_;
    std::optional<NetClassId> net_class_;
    std::optional<std::size_t> net_class_assignment_order_;
};

/** Complete canonical net input; pin membership is authored separately through connect. */
struct NetSpec {
    /** Unique human-facing net name. */
    NetName name;
    /** Broad electrical role of the net. */
    NetKind kind = NetKind::Signal;
};

} // namespace volt
