#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/schematic/schematic_sheet_metadata.hpp>

namespace volt {

/** A schematic sheet that owns presentation objects for one drawing page. */
class Sheet {
  public:
    /** Construct a named schematic sheet. */
    explicit Sheet(std::string name);

    /** Construct a named schematic sheet with explicit metadata. */
    Sheet(std::string name, SheetMetadata metadata);

    /** Return the sheet name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return sheet metadata. */
    [[nodiscard]] const SheetMetadata &metadata() const noexcept { return metadata_; }

    /** Return placed symbol instances in insertion order. */
    [[nodiscard]] const std::vector<SymbolInstanceId> &symbol_instances() const noexcept;

    /** Return wire runs in insertion order. */
    [[nodiscard]] const std::vector<WireRunId> &wire_runs() const noexcept { return wire_runs_; }

    /** Return net labels in insertion order. */
    [[nodiscard]] const std::vector<NetLabelId> &net_labels() const noexcept { return net_labels_; }

    /** Return explicit junctions in insertion order. */
    [[nodiscard]] const std::vector<JunctionId> &junctions() const noexcept { return junctions_; }

    /** Return power and ground ports in insertion order. */
    [[nodiscard]] const std::vector<PowerPortId> &power_ports() const noexcept;

    /** Return no-connect markers in insertion order. */
    [[nodiscard]] const std::vector<NoConnectMarkerId> &no_connect_markers() const noexcept;

    /** Return sheet/off-page ports in insertion order. */
    [[nodiscard]] const std::vector<SheetPortId> &sheet_ports() const noexcept;

    /** Return placed symbol fields in insertion order. */
    [[nodiscard]] const std::vector<SymbolFieldId> &symbol_fields() const noexcept;

    /** Return named functional regions in insertion order. */
    [[nodiscard]] const std::vector<SheetRegion> &regions() const noexcept { return regions_; }

    /** Return the region with this name, if it exists on this sheet. */
    [[nodiscard]] std::optional<std::size_t> region_by_name(const std::string &name) const;

    /** Return a region by sheet-local region index. */
    [[nodiscard]] const SheetRegion &region(std::size_t index) const;

  private:
    friend class Schematic;

    std::size_t add_region(SheetRegion region);

    void add_symbol_instance(SymbolInstanceId instance);

    void add_wire_run(WireRunId wire);

    void add_net_label(NetLabelId label);

    void add_junction(JunctionId junction);

    void add_power_port(PowerPortId port);

    void add_no_connect_marker(NoConnectMarkerId marker);

    void add_sheet_port(SheetPortId port);

    void add_symbol_field(SymbolFieldId field);

    std::string name_;
    SheetMetadata metadata_;
    std::vector<SymbolInstanceId> symbol_instances_;
    std::vector<WireRunId> wire_runs_;
    std::vector<NetLabelId> net_labels_;
    std::vector<JunctionId> junctions_;
    std::vector<PowerPortId> power_ports_;
    std::vector<NoConnectMarkerId> no_connect_markers_;
    std::vector<SheetPortId> sheet_ports_;
    std::vector<SymbolFieldId> symbol_fields_;
    std::vector<SheetRegion> regions_;
};

} // namespace volt
