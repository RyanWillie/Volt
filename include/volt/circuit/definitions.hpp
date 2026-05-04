#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace volt {

/** Electrical role declared by a reusable pin definition. */
enum class PinRole {
    Passive,
    PowerInput,
    PowerOutput,
    Ground,
    DigitalInput,
    DigitalOutput,
    Bidirectional,
    AnalogInput,
    AnalogOutput,
    NoConnect,
};

/** Whether a pin is expected, allowed, or forbidden to connect in normal use. */
enum class ConnectionRequirement {
    Optional,
    Required,
    MustNotConnect,
};

/** Reusable pin metadata belonging to a component definition. */
class PinDefinition {
  public:
    /**
     * Construct a pin definition with a name, package/symbol number, role, and connection
     * requirement.
     */
    PinDefinition(std::string name, std::string number, PinRole role,
                  ConnectionRequirement connection_requirement = ConnectionRequirement::Required)
        : name_{std::move(name)}, number_{std::move(number)}, role_{role},
          connection_requirement_{connection_requirement} {
        if (name_.empty()) {
            throw std::invalid_argument{"Pin definition name must not be empty"};
        }
        if (number_.empty()) {
            throw std::invalid_argument{"Pin definition number must not be empty"};
        }
    }

    /** Return the human-readable pin name, such as VDD or A. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the physical/logical pin number, such as 1 or 17. */
    [[nodiscard]] const std::string &number() const noexcept { return number_; }

    /** Return the pin's declared electrical role. */
    [[nodiscard]] PinRole role() const noexcept { return role_; }

    /** Return whether this pin is expected, allowed, or forbidden to connect. */
    [[nodiscard]] ConnectionRequirement connection_requirement() const noexcept {
        return connection_requirement_;
    }

  private:
    std::string name_;
    std::string number_;
    PinRole role_;
    ConnectionRequirement connection_requirement_;
};

/** Reusable logical component definition made from pin definition IDs. */
class ComponentDefinition {
  public:
    /** Construct a component definition with a name, ordered pin definitions, and properties. */
    ComponentDefinition(std::string name, std::vector<PinDefId> pins, PropertyMap properties = {})
        : name_{std::move(name)}, pins_{std::move(pins)}, properties_{std::move(properties)} {
        if (name_.empty()) {
            throw std::invalid_argument{"Component definition name must not be empty"};
        }
        if (pins_.empty()) {
            throw std::invalid_argument{"Component definition must contain at least one pin"};
        }
    }

    /** Return the reusable component name, such as Resistor or LED. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return ordered pin definition IDs belonging to this component definition. */
    [[nodiscard]] const std::vector<PinDefId> &pins() const noexcept { return pins_; }

    /** Return extensible metadata properties for this component definition. */
    [[nodiscard]] const PropertyMap &properties() const noexcept { return properties_; }

  private:
    std::string name_;
    std::vector<PinDefId> pins_;
    PropertyMap properties_;
};

} // namespace volt
