#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
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

/** Broad terminal behavior for reusable pin definitions. */
enum class ElectricalTerminalKind {
    Unspecified,
    Passive,
    Signal,
    Power,
    Ground,
    NoConnect,
};

/** Direction of electrical behavior at a reusable pin definition. */
enum class ElectricalDirection {
    Unspecified,
    Input,
    Output,
    Bidirectional,
    Passive,
};

/** Signal domain carried by a reusable pin definition. */
enum class ElectricalSignalDomain {
    Unspecified,
    Digital,
    Analog,
    Mixed,
};

/** Output or terminal drive behavior for reusable pin definitions. */
enum class ElectricalDriveKind {
    Unspecified,
    PushPull,
    OpenCollector,
    OpenDrain,
    HighImpedance,
    Passive,
};

/** Logical polarity for control-oriented reusable pin definitions. */
enum class ElectricalPolarity {
    None,
    ActiveHigh,
    ActiveLow,
};

/** Reusable pin metadata belonging to a component definition. */
class PinDefinition {
  public:
    /**
     * Construct a pin definition with a name, package/symbol number, role, and connection
     * requirement.
     */
    PinDefinition(std::string name, std::string number, PinRole role,
                  ConnectionRequirement connection_requirement = ConnectionRequirement::Required,
                  ElectricalTerminalKind terminal_kind = ElectricalTerminalKind::Unspecified,
                  ElectricalDirection direction = ElectricalDirection::Unspecified,
                  ElectricalSignalDomain signal_domain = ElectricalSignalDomain::Unspecified,
                  ElectricalDriveKind drive_kind = ElectricalDriveKind::Unspecified,
                  ElectricalPolarity polarity = ElectricalPolarity::None);

    /** Return the human-readable pin name, such as VDD or A. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the physical/logical pin number, such as 1 or 17. */
    [[nodiscard]] const std::string &number() const noexcept { return number_; }

    /** Return the pin's declared electrical role. */
    [[nodiscard]] PinRole role() const noexcept { return role_; }

    /** Return whether this pin is expected, allowed, or forbidden to connect. */
    [[nodiscard]] ConnectionRequirement connection_requirement() const noexcept;

    /** Return the pin's broad terminal behavior. */
    [[nodiscard]] ElectricalTerminalKind terminal_kind() const noexcept { return terminal_kind_; }

    /** Return the pin's electrical direction. */
    [[nodiscard]] ElectricalDirection direction() const noexcept { return direction_; }

    /** Return the pin's signal domain. */
    [[nodiscard]] ElectricalSignalDomain signal_domain() const noexcept { return signal_domain_; }

    /** Return the pin's drive behavior. */
    [[nodiscard]] ElectricalDriveKind drive_kind() const noexcept { return drive_kind_; }

    /** Return the pin's logical polarity. */
    [[nodiscard]] ElectricalPolarity polarity() const noexcept { return polarity_; }

  private:
    std::string name_;
    std::string number_;
    PinRole role_;
    ConnectionRequirement connection_requirement_;
    ElectricalTerminalKind terminal_kind_;
    ElectricalDirection direction_;
    ElectricalSignalDomain signal_domain_;
    ElectricalDriveKind drive_kind_;
    ElectricalPolarity polarity_;
};

/** Provenance for a reusable definition imported from a library. */
class DefinitionSource {
  public:
    /** Construct source provenance from non-empty namespace, name, and version fields. */
    DefinitionSource(std::string namespace_name, std::string name, std::string version);

    /** Return the source library namespace. */
    [[nodiscard]] const std::string &namespace_name() const noexcept { return namespace_; }

    /** Return the source library item name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the source library item version. */
    [[nodiscard]] const std::string &version() const noexcept { return version_; }

    /** Return whether two source records identify the same imported provenance. */
    [[nodiscard]] friend bool operator==(const DefinitionSource &lhs,
                                         const DefinitionSource &rhs) noexcept {
        return lhs.namespace_ == rhs.namespace_ && lhs.name_ == rhs.name_ &&
               lhs.version_ == rhs.version_;
    }

  private:
    std::string namespace_;
    std::string name_;
    std::string version_;
};

/** Named schematic symbol choice attached to a reusable component definition. */
class SchematicSymbolReference {
  public:
    /** Construct a schematic symbol reference with a variant label. */
    explicit SchematicSymbolReference(std::string name, std::string variant = "default");

    /** Return the schematic symbol definition name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the component-local symbol variant label. */
    [[nodiscard]] const std::string &variant() const noexcept { return variant_; }

    /** Return whether two references point at the same named variant. */
    [[nodiscard]] friend bool operator==(const SchematicSymbolReference &lhs,
                                         const SchematicSymbolReference &rhs) noexcept = default;

  private:
    std::string name_;
    std::string variant_;
};

/** Reusable logical component definition made from pin definition IDs. */
class ComponentDefinition {
  public:
    /** Construct a component definition with a name, ordered pin definitions, and properties. */
    ComponentDefinition(std::string name, std::vector<PinDefId> pins, PropertyMap properties = {},
                        std::optional<DefinitionSource> source = std::nullopt,
                        std::vector<SchematicSymbolReference> schematic_symbols = {});

    /** Return the reusable component name, such as Resistor or LED. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return ordered pin definition IDs belonging to this component definition. */
    [[nodiscard]] const std::vector<PinDefId> &pins() const noexcept { return pins_; }

    /** Return extensible metadata properties for this component definition. */
    [[nodiscard]] const PropertyMap &properties() const noexcept { return properties_; }

    /** Return source provenance for this definition, if it was imported from a library. */
    [[nodiscard]] const std::optional<DefinitionSource> &source() const noexcept { return source_; }

    /** Return schematic symbol choices available for this component definition. */
    [[nodiscard]] const std::vector<SchematicSymbolReference> &schematic_symbols() const noexcept;

  private:
    void require_unique_schematic_symbol_variants() const;

    std::string name_;
    std::vector<PinDefId> pins_;
    PropertyMap properties_;
    std::optional<DefinitionSource> source_;
    std::vector<SchematicSymbolReference> schematic_symbols_;
};

} // namespace volt
