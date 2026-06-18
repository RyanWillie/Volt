#pragma once

#include <cstddef>
#include <memory>

#include <volt/core/ids.hpp>
#include <volt/schematic/schematic_items.hpp>

namespace volt {

class Schematic;

namespace detail {
struct SchematicItemsState;
}

/**
 * Owns schematic presentation items that reference existing logical kernel entities.
 *
 * Responsibility: stores symbol instances, wires, labels, junctions, and other presentation
 *   items placed on sheets.
 * Invariants: every item references existing logical entities; items never create connectivity.
 * Collaborators: composed by Schematic; membership tracked by SchematicSheetModel; acyclic.
 */
class SchematicItemsModel {
  public:
    /** Construct an empty schematic-items facade. */
    SchematicItemsModel();
    /** Copy schematic-items state. */
    SchematicItemsModel(const SchematicItemsModel &other);
    /** Move schematic-items state. */
    SchematicItemsModel(SchematicItemsModel &&other) noexcept;
    /** Copy schematic-items state. */
    SchematicItemsModel &operator=(const SchematicItemsModel &other);
    /** Move schematic-items state. */
    SchematicItemsModel &operator=(SchematicItemsModel &&other) noexcept;
    /** Destroy schematic-items state. */
    ~SchematicItemsModel();

    /** Return a symbol placement by schematic-local ID. */
    [[nodiscard]] const SymbolInstance &symbol_instance(SymbolInstanceId id) const;

    /** Return a wire run by schematic-local ID. */
    [[nodiscard]] const WireRun &wire_run(WireRunId id) const;

    /** Return a net label by schematic-local ID. */
    [[nodiscard]] const NetLabel &net_label(NetLabelId id) const;

    /** Return a junction by schematic-local ID. */
    [[nodiscard]] const Junction &junction(JunctionId id) const;

    /** Return a power or ground marker by schematic-local ID. */
    [[nodiscard]] const PowerPort &power_port(PowerPortId id) const;

    /** Return a no-connect marker by schematic-local ID. */
    [[nodiscard]] const NoConnectMarker &no_connect_marker(NoConnectMarkerId id) const;

    /** Return a sheet or off-page port by schematic-local ID. */
    [[nodiscard]] const SheetPort &sheet_port(SheetPortId id) const;

    /** Return a symbol field by schematic-local ID. */
    [[nodiscard]] const SymbolField &symbol_field(SymbolFieldId id) const;

    /** Return the number of symbol placements. */
    [[nodiscard]] std::size_t symbol_instance_count() const noexcept;

    /** Return the number of wire runs. */
    [[nodiscard]] std::size_t wire_run_count() const noexcept;

    /** Return the number of net labels. */
    [[nodiscard]] std::size_t net_label_count() const noexcept;

    /** Return the number of junctions. */
    [[nodiscard]] std::size_t junction_count() const noexcept;

    /** Return the number of power and ground markers. */
    [[nodiscard]] std::size_t power_port_count() const noexcept;

    /** Return the number of no-connect markers. */
    [[nodiscard]] std::size_t no_connect_marker_count() const noexcept;

    /** Return the number of sheet and off-page ports. */
    [[nodiscard]] std::size_t sheet_port_count() const noexcept;

    /** Return the number of symbol fields. */
    [[nodiscard]] std::size_t symbol_field_count() const noexcept;

    /** Require that a symbol instance ID belongs to this model. */
    void require_symbol_instance(SymbolInstanceId instance) const;

  protected:
    /** Construct a read-only facade over owner-private storage. */
    explicit SchematicItemsModel(std::shared_ptr<const detail::SchematicItemsState> state);

  private:
    [[nodiscard]] const detail::SchematicItemsState &state() const noexcept;

    std::shared_ptr<const detail::SchematicItemsState> state_;
};

} // namespace volt
