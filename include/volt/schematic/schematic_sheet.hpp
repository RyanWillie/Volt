#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/schematic/schematic_sheet_metadata.hpp>

namespace volt {

namespace detail {
struct SheetState;
}

/** A schematic sheet that owns presentation objects for one drawing page. */
class Sheet {
  public:
    /** Construct a named schematic sheet. */
    explicit Sheet(std::string name);

    /** Construct a named schematic sheet with explicit metadata. */
    Sheet(std::string name, SheetMetadata metadata);
    /** Copy sheet state. */
    Sheet(const Sheet &other);
    /** Move sheet state. */
    Sheet(Sheet &&other) noexcept;
    /** Copy sheet state. */
    Sheet &operator=(const Sheet &other);
    /** Move sheet state. */
    Sheet &operator=(Sheet &&other) noexcept;
    /** Destroy sheet state. */
    ~Sheet();

    /** Return the sheet name. */
    [[nodiscard]] const std::string &name() const noexcept;

    /** Return sheet metadata. */
    [[nodiscard]] const SheetMetadata &metadata() const noexcept;

    /** Return placed symbol instances in insertion order. */
    [[nodiscard]] const std::vector<SymbolInstanceId> &symbol_instances() const noexcept;

    /** Return wire runs in insertion order. */
    [[nodiscard]] const std::vector<WireRunId> &wire_runs() const noexcept;

    /** Return net labels in insertion order. */
    [[nodiscard]] const std::vector<NetLabelId> &net_labels() const noexcept;

    /** Return explicit junctions in insertion order. */
    [[nodiscard]] const std::vector<JunctionId> &junctions() const noexcept;

    /** Return power and ground ports in insertion order. */
    [[nodiscard]] const std::vector<PowerPortId> &power_ports() const noexcept;

    /** Return no-connect markers in insertion order. */
    [[nodiscard]] const std::vector<NoConnectMarkerId> &no_connect_markers() const noexcept;

    /** Return sheet/off-page ports in insertion order. */
    [[nodiscard]] const std::vector<SheetPortId> &sheet_ports() const noexcept;

    /** Return placed symbol fields in insertion order. */
    [[nodiscard]] const std::vector<SymbolFieldId> &symbol_fields() const noexcept;

    /** Return named functional regions in insertion order. */
    [[nodiscard]] const std::vector<SheetRegion> &regions() const noexcept;

    /** Return the region with this name, if it exists on this sheet. */
    [[nodiscard]] std::optional<std::size_t> region_by_name(const std::string &name) const;

    /** Return a region by sheet-local region index. */
    [[nodiscard]] const SheetRegion &region(std::size_t index) const;

  protected:
    /** Construct a read-only facade over owner-private storage. */
    explicit Sheet(std::shared_ptr<const detail::SheetState> state);

  private:
    [[nodiscard]] const detail::SheetState &state() const noexcept;

    std::shared_ptr<const detail::SheetState> state_;
};

} // namespace volt
