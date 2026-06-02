#include <volt/schematic/schematic_library_model.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace volt {

[[nodiscard]] SymbolDefId
SchematicLibraryModel::add_symbol_definition(SymbolDefinition definition) {
    if (symbol_definition_by_name(definition.name()).has_value()) {
        throw std::logic_error{"Symbol definition name already exists"};
    }

    return symbol_definitions_.insert(std::move(definition));
}
[[nodiscard]] std::optional<SymbolDefId>
SchematicLibraryModel::symbol_definition_by_name(const std::string &name) const {
    for (std::size_t index = 0; index < symbol_definitions_.size(); ++index) {
        const auto id = SymbolDefId{index};
        if (symbol_definitions_.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] const SymbolDefinition &
SchematicLibraryModel::symbol_definition(SymbolDefId id) const {
    return symbol_definitions_.get(id);
}
[[nodiscard]] std::size_t SchematicLibraryModel::symbol_definition_count() const noexcept {
    return symbol_definitions_.size();
}
void SchematicLibraryModel::require_symbol_definition(SymbolDefId symbol_definition) const {
    if (!symbol_definitions_.contains(symbol_definition)) {
        throw std::out_of_range{"Symbol definition ID does not belong to this schematic"};
    }
}

} // namespace volt
