#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/schematic/schematic_sheet.hpp>

namespace volt {

class Schematic;

/**
 * Owns schematic sheets, authored regions, and sheet-local item membership lists.
 *
 * Responsibility: stores sheet structure (size, frame, regions) and which items belong to each
 *   sheet.
 * Invariants: membership entries reference existing items; sheet geometry is well-formed.
 * Collaborators: composed by Schematic; tracks membership of SchematicItemsModel items; acyclic.
 */
class SchematicSheetModel {
  public:
    /** Add a schematic sheet and return its schematic-local ID. */
    [[nodiscard]] SheetId add_sheet(Sheet sheet);

    /** Add a named authored region to an existing sheet. */
    [[nodiscard]] std::size_t add_sheet_region(SheetId sheet, SheetRegion region);

    /** Return the sheet with the requested name, if present. */
    [[nodiscard]] std::optional<SheetId> sheet_by_name(const std::string &name) const;

    /** Return a sheet-local authored region index by name, if present. */
    [[nodiscard]] std::optional<std::size_t> sheet_region_by_name(SheetId sheet,
                                                                  const std::string &name) const;

    /** Return a sheet by schematic-local ID. */
    [[nodiscard]] const Sheet &sheet(SheetId id) const;

    /** Return an authored region by sheet and sheet-local index. */
    [[nodiscard]] const SheetRegion &sheet_region(SheetId sheet, std::size_t region) const;

    /** Return the number of sheets. */
    [[nodiscard]] std::size_t sheet_count() const noexcept;

    /** Require that a sheet ID belongs to this model. */
    void require_sheet(SheetId sheet) const;

  private:
    friend class Schematic;

    /** Record that a symbol instance appears on a sheet. */
    void add_symbol_instance(SheetId sheet, SymbolInstanceId instance);

    /** Record that a wire run appears on a sheet. */
    void add_wire_run(SheetId sheet, WireRunId wire);

    /** Record that a net label appears on a sheet. */
    void add_net_label(SheetId sheet, NetLabelId label);

    /** Record that a junction appears on a sheet. */
    void add_junction(SheetId sheet, JunctionId junction);

    /** Record that a power or ground marker appears on a sheet. */
    void add_power_port(SheetId sheet, PowerPortId port);

    /** Record that a no-connect marker appears on a sheet. */
    void add_no_connect_marker(SheetId sheet, NoConnectMarkerId marker);

    /** Record that a sheet or off-page port appears on a sheet. */
    void add_sheet_port(SheetId sheet, SheetPortId port);

    /** Record that a symbol field appears on a sheet. */
    void add_symbol_field(SheetId sheet, SymbolFieldId field);

    [[nodiscard]] Sheet &mutable_sheet(SheetId id);

    EntityTable<Sheet, SheetId> sheets_;
};

} // namespace volt
