#include <volt/circuit/definitions.hpp>

namespace volt {

PinDefinition::PinDefinition(std::string name, std::string number, PinRole role,
                             ConnectionRequirement connection_requirement,
                             ElectricalTerminalKind terminal_kind, ElectricalDirection direction,
                             ElectricalSignalDomain signal_domain, ElectricalDriveKind drive_kind,
                             ElectricalPolarity polarity)
    : name_{std::move(name)}, number_{std::move(number)}, role_{role},
      connection_requirement_{connection_requirement}, terminal_kind_{terminal_kind},
      direction_{direction}, signal_domain_{signal_domain}, drive_kind_{drive_kind},
      polarity_{polarity} {
    if (name_.empty()) {
        throw std::invalid_argument{"Pin definition name must not be empty"};
    }
    if (number_.empty()) {
        throw std::invalid_argument{"Pin definition number must not be empty"};
    }
}
[[nodiscard]] ConnectionRequirement PinDefinition::connection_requirement() const noexcept {
    return connection_requirement_;
}
DefinitionSource::DefinitionSource(std::string namespace_name, std::string name,
                                   std::string version)
    : namespace_{std::move(namespace_name)}, name_{std::move(name)}, version_{std::move(version)} {
    if (namespace_.empty()) {
        throw std::invalid_argument{"Definition source namespace must not be empty"};
    }
    if (name_.empty()) {
        throw std::invalid_argument{"Definition source name must not be empty"};
    }
    if (version_.empty()) {
        throw std::invalid_argument{"Definition source version must not be empty"};
    }
}
SchematicSymbolReference::SchematicSymbolReference(std::string name, std::string variant)
    : name_{std::move(name)}, variant_{std::move(variant)} {
    if (name_.empty()) {
        throw std::invalid_argument{"Schematic symbol name must not be empty"};
    }
    if (variant_.empty()) {
        throw std::invalid_argument{"Schematic symbol variant must not be empty"};
    }
}
ComponentDefinition::ComponentDefinition(std::string name, std::vector<PinDefId> pins,
                                         PropertyMap properties,
                                         std::optional<DefinitionSource> source,
                                         std::vector<SchematicSymbolReference> schematic_symbols)
    : name_{std::move(name)}, pins_{std::move(pins)}, properties_{std::move(properties)},
      source_{std::move(source)}, schematic_symbols_{std::move(schematic_symbols)} {
    if (name_.empty()) {
        throw std::invalid_argument{"Component definition name must not be empty"};
    }
    if (pins_.empty()) {
        throw std::invalid_argument{"Component definition must contain at least one pin"};
    }
    require_unique_schematic_symbol_variants();
}
[[nodiscard]] const std::vector<SchematicSymbolReference> &
ComponentDefinition::schematic_symbols() const noexcept {
    return schematic_symbols_;
}
void ComponentDefinition::require_unique_schematic_symbol_variants() const {
    for (std::size_t index = 0; index < schematic_symbols_.size(); ++index) {
        const auto duplicate =
            std::any_of(schematic_symbols_.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                        schematic_symbols_.end(), [this, index](const auto &candidate) {
                            return candidate.variant() == schematic_symbols_[index].variant();
                        });
        if (duplicate) {
            throw std::invalid_argument{
                "Component definition schematic symbol variants must be unique"};
        }
    }
}

} // namespace volt
