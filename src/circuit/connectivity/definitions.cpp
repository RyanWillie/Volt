#include <volt/circuit/connectivity/definitions.hpp>

#include <volt/core/errors.hpp>

#include <array>
#include <charconv>
#include <limits>
#include <sstream>
#include <type_traits>

namespace volt {

namespace {

void append_string(std::ostringstream &out, std::string_view value) {
    out << value.size() << ':' << value << '\n';
}

void append_number(std::ostringstream &out, double value) {
    auto buffer = std::array<char, 64>{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                      std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) {
        throw KernelLogicError{ErrorCode::InvalidState,
                               "Failed to encode canonical component-contract number"};
    }
    append_string(out, std::string_view{buffer.data(), result.ptr});
}

template <typename Enum> void append_enum(std::ostringstream &out, Enum value) {
    static_assert(std::is_enum_v<Enum>);
    append_string(out, std::to_string(static_cast<std::underlying_type_t<Enum>>(value)));
}

void append_quantity(std::ostringstream &out, const Quantity &quantity) {
    append_enum(out, quantity.dimension());
    append_number(out, quantity.value());
}

void append_attribute_value(std::ostringstream &out, const ElectricalAttributeValue &value) {
    append_enum(out, value.kind());
    switch (value.kind()) {
    case ElectricalAttributeValueKind::Quantity:
        append_quantity(out, value.as_quantity());
        return;
    case ElectricalAttributeValueKind::Tolerance:
        append_enum(out, value.as_tolerance().mode());
        append_quantity(out, value.as_tolerance().minus());
        append_quantity(out, value.as_tolerance().plus());
        return;
    case ElectricalAttributeValueKind::Range:
        append_enum(out, value.as_range().dimension());
        append_string(out, value.as_range().minimum().has_value() ? "minimum" : "no-minimum");
        if (value.as_range().minimum().has_value()) {
            append_quantity(out, *value.as_range().minimum());
        }
        append_string(out, value.as_range().maximum().has_value() ? "maximum" : "no-maximum");
        if (value.as_range().maximum().has_value()) {
            append_quantity(out, *value.as_range().maximum());
        }
        return;
    }
    throw KernelLogicError{ErrorCode::InvalidState,
                           "Unhandled component-contract electrical attribute value"};
}

void append_subject_ref(std::ostringstream &out, const ComponentSubjectRef &subject) {
    append_enum(out, subject.kind());
    switch (subject.kind()) {
    case ElectricalSubjectKind::FramedPin:
        append_string(out, subject.as_framed_pin().value());
        return;
    case ElectricalSubjectKind::DirectedRelation:
        append_string(out, subject.as_directed_relation().value());
        return;
    case ElectricalSubjectKind::SupplyDomain:
        append_string(out, subject.as_supply_domain().value());
        return;
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled component subject reference"};
}

[[nodiscard]] ContentHash
component_content_identity(std::span<const PinDefinition> pins,
                           const std::optional<DefinitionSource> &source,
                           const std::vector<SchematicSymbolReference> &schematic_symbols,
                           const ComponentContract &contract) {
    auto out = std::ostringstream{};
    append_string(out, "volt.component-contract");
    append_string(out, "1");
    append_string(out, contract.key().value());

    append_string(out, std::to_string(pins.size()));
    for (std::size_t index = 0; index < pins.size(); ++index) {
        const auto &pin = pins[index];
        append_string(out, contract.pin_keys()[index].value());
        append_string(out, pin.name());
        append_string(out, pin.number());
        append_enum(out, pin.connection_requirement());
        append_enum(out, pin.terminal_kind());
        append_enum(out, pin.direction());
        append_enum(out, pin.signal_domain());
        append_enum(out, pin.drive_kind());
        append_enum(out, pin.polarity());
        append_string(out, std::to_string(pin.electrical_attributes().size()));
        for (const auto &[attribute_name, value] : pin.electrical_attributes().entries()) {
            append_string(out, attribute_name.value());
            append_attribute_value(out, value);
        }
    }

    append_string(out, source.has_value() ? "source" : "no-source");
    if (source.has_value()) {
        append_string(out, source->namespace_name());
        append_string(out, source->name());
        append_string(out, source->version());
    }

    append_string(out, std::to_string(schematic_symbols.size()));
    for (const auto &symbol : schematic_symbols) {
        append_string(out, symbol.name());
        append_string(out, symbol.variant());
    }

    append_string(out, std::to_string(contract.framed_pins().size()));
    for (const auto &subject : contract.framed_pins()) {
        append_string(out, subject.key().value());
        append_string(out, subject.pin().value());
        append_string(out, subject.reference().value());
    }
    append_string(out, std::to_string(contract.relations().size()));
    for (const auto &subject : contract.relations()) {
        append_string(out, subject.key().value());
        append_string(out, subject.from().value());
        append_string(out, subject.to().value());
    }
    append_string(out, std::to_string(contract.supply_domains().size()));
    for (const auto &subject : contract.supply_domains()) {
        append_string(out, subject.key().value());
        append_string(out, std::to_string(subject.positive_pins().size()));
        for (const auto &pin : subject.positive_pins()) {
            append_string(out, pin.value());
        }
        append_string(out, std::to_string(subject.return_pins().size()));
        for (const auto &pin : subject.return_pins()) {
            append_string(out, pin.value());
        }
    }
    append_string(out, std::to_string(contract.feature_schemas().size()));
    for (const auto &schema : contract.feature_schemas()) {
        append_string(out, schema.key().value());
        append_enum(out, schema.subject_kind());
        append_string(out, std::to_string(schema.roles().size()));
        for (const auto &role : schema.roles()) {
            append_string(out, role.key().value());
            append_enum(out, role.cardinality());
        }
        append_string(out, std::to_string(schema.required_records().size()));
        for (const auto &requirement : schema.required_records()) {
            append_enum(out, requirement.observable);
            append_enum(out, requirement.meaning);
        }
    }
    append_string(out, std::to_string(contract.feature_bindings().size()));
    for (const auto &binding : contract.feature_bindings()) {
        append_string(out, binding.key().value());
        append_string(out, binding.schema().value());
        append_subject_ref(out, binding.subject());
        append_string(out, std::to_string(binding.roles().size()));
        for (const auto &role : binding.roles()) {
            append_string(out, role.role().value());
            append_string(out, std::to_string(role.pins().size()));
            for (const auto &pin : role.pins()) {
                append_string(out, pin.value());
            }
        }
    }
    return sha256_content_hash(out.str());
}

} // namespace

PinDefinition::PinDefinition(std::string name, std::string number,
                             ConnectionRequirement connection_requirement,
                             ElectricalTerminalKind terminal_kind, ElectricalDirection direction,
                             ElectricalSignalDomain signal_domain, ElectricalDriveKind drive_kind,
                             ElectricalPolarity polarity,
                             ElectricalAttributeMap electrical_attributes)
    : name_{std::move(name)}, number_{std::move(number)},
      connection_requirement_{connection_requirement}, terminal_kind_{terminal_kind},
      direction_{direction}, signal_domain_{signal_domain}, drive_kind_{drive_kind},
      polarity_{polarity}, electrical_attributes_{std::move(electrical_attributes)} {
    if (name_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Pin definition name must not be empty"};
    }
    if (number_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Pin definition number must not be empty"};
    }
}

[[nodiscard]] ConnectionRequirement PinDefinition::connection_requirement() const noexcept {
    return connection_requirement_;
}

DefinitionSource::DefinitionSource(std::string namespace_name, std::string name,
                                   std::string version)
    : namespace_{std::move(namespace_name)}, name_{std::move(name)}, version_{std::move(version)} {
    if (namespace_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Definition source namespace must not be empty"};
    }
    if (name_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Definition source name must not be empty"};
    }
    if (version_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Definition source version must not be empty"};
    }
}

SchematicSymbolReference::SchematicSymbolReference(std::string name, std::string variant)
    : name_{std::move(name)}, variant_{std::move(variant)} {
    if (name_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Schematic symbol name must not be empty"};
    }
    if (variant_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Schematic symbol variant must not be empty"};
    }
}

ComponentDefinition
ComponentDefinition::make(std::string name, std::span<const PinDefinition> pin_definitions,
                          std::vector<PinDefId> pins, PropertyMap properties,
                          std::optional<DefinitionSource> source,
                          std::vector<SchematicSymbolReference> schematic_symbols,
                          std::optional<ComponentContractSpec> contract) {
    if (pins.size() != pin_definitions.size()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Component definition pin IDs and content must have equal size"};
    }
    for (std::size_t index = 0; index < pins.size(); ++index) {
        if (std::find(pins.begin(), pins.begin() + static_cast<std::ptrdiff_t>(index),
                      pins[index]) != pins.begin() + static_cast<std::ptrdiff_t>(index)) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Component definition contains duplicate pin IDs"};
        }
    }
    auto normalized_contract =
        contract.has_value()
            ? ComponentContract{std::move(*contract)}
            : ComponentContract::standard(ComponentKey{name}, [&pin_definitions] {
                  auto keys = std::vector<PinKey>{};
                  keys.reserve(pin_definitions.size());
                  for (std::size_t index = 0; index < pin_definitions.size(); ++index) {
                      keys.emplace_back("pin/" + std::to_string(index));
                  }
                  return keys;
              }());
    if (normalized_contract.pin_keys().size() != pin_definitions.size()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Component contract PinKeys must match ordered pin content"};
    }
    const auto identity =
        component_content_identity(pin_definitions, source, schematic_symbols, normalized_contract);
    return ComponentDefinition{std::move(name),
                               std::move(pins),
                               std::move(properties),
                               std::move(source),
                               std::move(schematic_symbols),
                               std::move(normalized_contract),
                               identity};
}

ComponentDefinition::ComponentDefinition(std::string name, std::vector<PinDefId> pins,
                                         PropertyMap properties,
                                         std::optional<DefinitionSource> source,
                                         std::vector<SchematicSymbolReference> schematic_symbols,
                                         ComponentContract contract, ContentHash content_identity)
    : name_{std::move(name)}, pins_{std::move(pins)}, properties_{std::move(properties)},
      source_{std::move(source)}, schematic_symbols_{std::move(schematic_symbols)},
      contract_{std::move(contract)}, content_identity_{std::move(content_identity)} {
    if (name_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Component definition name must not be empty"};
    }
    if (pins_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Component definition must contain at least one pin"};
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
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Component definition schematic symbol variants must be unique"};
        }
    }
}

} // namespace volt
