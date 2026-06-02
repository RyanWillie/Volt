#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/schematic/schematic_sheet.hpp>

namespace volt {

class SchematicSheetModel {
  public:
    [[nodiscard]] SheetId add_sheet(Sheet sheet);

    [[nodiscard]] std::size_t add_sheet_region(SheetId sheet, SheetRegion region);

    void add_symbol_instance(SheetId sheet, SymbolInstanceId instance);

    void add_wire_run(SheetId sheet, WireRunId wire);

    void add_net_label(SheetId sheet, NetLabelId label);

    void add_junction(SheetId sheet, JunctionId junction);

    void add_power_port(SheetId sheet, PowerPortId port);

    void add_no_connect_marker(SheetId sheet, NoConnectMarkerId marker);

    void add_sheet_port(SheetId sheet, SheetPortId port);

    void add_symbol_field(SheetId sheet, SymbolFieldId field);

    [[nodiscard]] std::optional<SheetId> sheet_by_name(const std::string &name) const;

    [[nodiscard]] std::optional<std::size_t> sheet_region_by_name(SheetId sheet,
                                                                  const std::string &name) const;

    [[nodiscard]] const Sheet &sheet(SheetId id) const;

    [[nodiscard]] const SheetRegion &sheet_region(SheetId sheet, std::size_t region) const;

    [[nodiscard]] std::size_t sheet_count() const noexcept;

    void require_sheet(SheetId sheet) const;

  private:
    [[nodiscard]] Sheet &mutable_sheet(SheetId id);

    EntityTable<Sheet, SheetId> sheets_;
};

} // namespace volt
