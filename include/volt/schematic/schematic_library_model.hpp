#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt {

class SchematicLibraryModel {
  public:
    [[nodiscard]] SymbolDefId add_symbol_definition(SymbolDefinition definition);

    [[nodiscard]] std::optional<SymbolDefId>
    symbol_definition_by_name(const std::string &name) const;

    [[nodiscard]] const SymbolDefinition &symbol_definition(SymbolDefId id) const;

    [[nodiscard]] std::size_t symbol_definition_count() const noexcept;

    void require_symbol_definition(SymbolDefId symbol_definition) const;

  private:
    EntityTable<SymbolDefinition, SymbolDefId> symbol_definitions_;
};

} // namespace volt
