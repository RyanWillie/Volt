#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <volt/core/ids.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt {

namespace detail {
struct SchematicLibraryState;
}

/**
 * Owns reusable schematic symbol definitions for one schematic projection.
 *
 * Responsibility: stores the symbol vocabulary (pins, graphics, anchors) that item instances
 *   reference.
 * Invariants: symbol names/IDs are stable and unique within the projection.
 * Collaborators: composed by Schematic; referenced by SchematicItemsModel symbol instances;
 *   acyclic.
 */
class SchematicLibraryModel {
  public:
    /** Construct an empty schematic-library facade. */
    SchematicLibraryModel();
    /** Copy schematic-library state. */
    SchematicLibraryModel(const SchematicLibraryModel &other);
    /** Move schematic-library state. */
    SchematicLibraryModel(SchematicLibraryModel &&other) noexcept;
    /** Copy schematic-library state. */
    SchematicLibraryModel &operator=(const SchematicLibraryModel &other);
    /** Move schematic-library state. */
    SchematicLibraryModel &operator=(SchematicLibraryModel &&other) noexcept;
    /** Destroy schematic-library state. */
    ~SchematicLibraryModel();

    /** Return the symbol definition with the requested name, if present. */
    [[nodiscard]] std::optional<SymbolDefId>
    symbol_definition_by_name(const std::string &name) const;

    /** Return a symbol definition by schematic-local ID. */
    [[nodiscard]] const SymbolDefinition &symbol_definition(SymbolDefId id) const;

    /** Return the number of reusable symbol definitions. */
    [[nodiscard]] std::size_t symbol_definition_count() const noexcept;

    /** Require that a symbol definition ID belongs to this model. */
    void require_symbol_definition(SymbolDefId symbol_definition) const;

  protected:
    /** Construct a read-only facade over owner-private storage. */
    explicit SchematicLibraryModel(std::shared_ptr<const detail::SchematicLibraryState> state);

  private:
    [[nodiscard]] const detail::SchematicLibraryState &state() const noexcept;

    std::shared_ptr<const detail::SchematicLibraryState> state_;
};

} // namespace volt
