#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt {

/** Owns reusable schematic symbol definitions for one schematic projection. */
class SchematicLibraryModel {
  public:
    /** Add a reusable symbol definition and return its schematic-local ID. */
    [[nodiscard]] SymbolDefId add_symbol_definition(SymbolDefinition definition);

    /** Return the symbol definition with the requested name, if present. */
    [[nodiscard]] std::optional<SymbolDefId>
    symbol_definition_by_name(const std::string &name) const;

    /** Return a symbol definition by schematic-local ID. */
    [[nodiscard]] const SymbolDefinition &symbol_definition(SymbolDefId id) const;

    /** Return the number of reusable symbol definitions. */
    [[nodiscard]] std::size_t symbol_definition_count() const noexcept;

    /** Require that a symbol definition ID belongs to this model. */
    void require_symbol_definition(SymbolDefId symbol_definition) const;

  private:
    EntityTable<SymbolDefinition, SymbolDefId> symbol_definitions_;
};

} // namespace volt
