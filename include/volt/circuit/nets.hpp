#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/ids.hpp>

namespace volt {

/** Human-facing canonical net name, such as GND, 3V3, or LED_A. */
class NetName {
  public:
    /** Construct a non-empty net name. */
    explicit NetName(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Net name must not be empty"};
        }
    }

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
    Net(NetName name, NetKind kind) : name_{std::move(name)}, kind_{kind} {}

    /** Return the human-facing net name. */
    [[nodiscard]] const NetName &name() const noexcept { return name_; }

    /** Return the net classification. */
    [[nodiscard]] NetKind kind() const noexcept { return kind_; }

    /** Return concrete pins connected to this net in deterministic insertion order. */
    [[nodiscard]] const std::vector<PinId> &pins() const noexcept { return pins_; }

    /** Return whether the pin is already connected to this net. */
    [[nodiscard]] bool contains(PinId pin) const noexcept {
        return std::find(pins_.begin(), pins_.end(), pin) != pins_.end();
    }

    /** Connect a pin if it is not already present; returns true when the net changed. */
    bool connect(PinId pin) {
        if (contains(pin)) {
            return false;
        }

        pins_.push_back(pin);
        return true;
    }

    /** Disconnect a pin if present; returns true when the net changed. */
    bool disconnect(PinId pin) {
        const auto it = std::find(pins_.begin(), pins_.end(), pin);
        if (it == pins_.end()) {
            return false;
        }

        pins_.erase(it);
        return true;
    }

  private:
    NetName name_;
    NetKind kind_;
    std::vector<PinId> pins_;
};

} // namespace volt
