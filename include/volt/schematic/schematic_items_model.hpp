#pragma once

#include <cstddef>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/schematic/schematic_items.hpp>

namespace volt {

/** Owns schematic presentation items that reference existing logical kernel entities. */
class SchematicItemsModel {
  public:
    /** Add a symbol placement over an existing logical component. */
    [[nodiscard]] SymbolInstanceId add_symbol_instance(SymbolInstance instance);

    /** Add a drawn wire run over an existing logical net. */
    [[nodiscard]] WireRunId add_wire_run(WireRun wire);

    /** Add a net label over an existing logical net. */
    [[nodiscard]] NetLabelId add_net_label(NetLabel label);

    /** Add a junction over an existing logical net. */
    [[nodiscard]] JunctionId add_junction(Junction junction);

    /** Add a power or ground marker over an existing logical net. */
    [[nodiscard]] PowerPortId add_power_port(PowerPort port);

    /** Add a no-connect marker over an existing logical pin. */
    [[nodiscard]] NoConnectMarkerId add_no_connect_marker(NoConnectMarker marker);

    /** Add a sheet or off-page port over an existing logical net. */
    [[nodiscard]] SheetPortId add_sheet_port(SheetPort port);

    /** Add a field owned by an existing symbol instance. */
    [[nodiscard]] SymbolFieldId add_symbol_field(SymbolField field);

    /** Return a symbol placement by schematic-local ID. */
    [[nodiscard]] const SymbolInstance &symbol_instance(SymbolInstanceId id) const;

    /** Return a wire run by schematic-local ID. */
    [[nodiscard]] const WireRun &wire_run(WireRunId id) const;

    /** Return a net label by schematic-local ID. */
    [[nodiscard]] const NetLabel &net_label(NetLabelId id) const;

    /** Move rendered net-label text without changing logical connectivity. */
    void move_net_label_text(NetLabelId id, Point position);

    /** Return a junction by schematic-local ID. */
    [[nodiscard]] const Junction &junction(JunctionId id) const;

    /** Return a power or ground marker by schematic-local ID. */
    [[nodiscard]] const PowerPort &power_port(PowerPortId id) const;

    /** Move rendered power-port text without changing logical connectivity. */
    void move_power_port_label(PowerPortId id, Point position);

    /** Return a no-connect marker by schematic-local ID. */
    [[nodiscard]] const NoConnectMarker &no_connect_marker(NoConnectMarkerId id) const;

    /** Return a sheet or off-page port by schematic-local ID. */
    [[nodiscard]] const SheetPort &sheet_port(SheetPortId id) const;

    /** Return a symbol field by schematic-local ID. */
    [[nodiscard]] const SymbolField &symbol_field(SymbolFieldId id) const;

    /** Move a symbol field without changing its owning logical symbol placement. */
    void move_symbol_field(SymbolFieldId id, Point position);

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

  private:
    EntityTable<SymbolInstance, SymbolInstanceId> symbol_instances_;
    EntityTable<WireRun, WireRunId> wire_runs_;
    EntityTable<NetLabel, NetLabelId> net_labels_;
    EntityTable<Junction, JunctionId> junctions_;
    EntityTable<PowerPort, PowerPortId> power_ports_;
    EntityTable<NoConnectMarker, NoConnectMarkerId> no_connect_markers_;
    EntityTable<SheetPort, SheetPortId> sheet_ports_;
    EntityTable<SymbolField, SymbolFieldId> symbol_fields_;
};

} // namespace volt
