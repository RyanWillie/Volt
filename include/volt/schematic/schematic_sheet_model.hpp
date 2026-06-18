#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <volt/core/ids.hpp>
#include <volt/schematic/schematic_sheet.hpp>

namespace volt {

namespace detail {
struct SchematicSheetState;
}

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
    /** Construct an empty schematic-sheet facade. */
    SchematicSheetModel();
    /** Copy schematic-sheet state. */
    SchematicSheetModel(const SchematicSheetModel &other);
    /** Move schematic-sheet state. */
    SchematicSheetModel(SchematicSheetModel &&other) noexcept;
    /** Copy schematic-sheet state. */
    SchematicSheetModel &operator=(const SchematicSheetModel &other);
    /** Move schematic-sheet state. */
    SchematicSheetModel &operator=(SchematicSheetModel &&other) noexcept;
    /** Destroy schematic-sheet state. */
    ~SchematicSheetModel();

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

  protected:
    /** Construct a read-only facade over owner-private storage. */
    explicit SchematicSheetModel(std::shared_ptr<const detail::SchematicSheetState> state);

  private:
    [[nodiscard]] const detail::SchematicSheetState &state() const noexcept;

    std::shared_ptr<const detail::SchematicSheetState> state_;
};

} // namespace volt
