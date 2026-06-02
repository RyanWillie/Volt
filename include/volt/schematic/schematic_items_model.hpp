#pragma once

#include <cstddef>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/schematic/schematic_items.hpp>

namespace volt {

class SchematicItemsModel {
  public:
    [[nodiscard]] SymbolInstanceId add_symbol_instance(SymbolInstance instance);

    [[nodiscard]] WireRunId add_wire_run(WireRun wire);

    [[nodiscard]] NetLabelId add_net_label(NetLabel label);

    [[nodiscard]] JunctionId add_junction(Junction junction);

    [[nodiscard]] PowerPortId add_power_port(PowerPort port);

    [[nodiscard]] NoConnectMarkerId add_no_connect_marker(NoConnectMarker marker);

    [[nodiscard]] SheetPortId add_sheet_port(SheetPort port);

    [[nodiscard]] SymbolFieldId add_symbol_field(SymbolField field);

    [[nodiscard]] const SymbolInstance &symbol_instance(SymbolInstanceId id) const;

    [[nodiscard]] const WireRun &wire_run(WireRunId id) const;

    [[nodiscard]] const NetLabel &net_label(NetLabelId id) const;

    void move_net_label_text(NetLabelId id, Point position);

    [[nodiscard]] const Junction &junction(JunctionId id) const;

    [[nodiscard]] const PowerPort &power_port(PowerPortId id) const;

    void move_power_port_label(PowerPortId id, Point position);

    [[nodiscard]] const NoConnectMarker &no_connect_marker(NoConnectMarkerId id) const;

    [[nodiscard]] const SheetPort &sheet_port(SheetPortId id) const;

    [[nodiscard]] const SymbolField &symbol_field(SymbolFieldId id) const;

    void move_symbol_field(SymbolFieldId id, Point position);

    [[nodiscard]] std::size_t symbol_instance_count() const noexcept;

    [[nodiscard]] std::size_t wire_run_count() const noexcept;

    [[nodiscard]] std::size_t net_label_count() const noexcept;

    [[nodiscard]] std::size_t junction_count() const noexcept;

    [[nodiscard]] std::size_t power_port_count() const noexcept;

    [[nodiscard]] std::size_t no_connect_marker_count() const noexcept;

    [[nodiscard]] std::size_t sheet_port_count() const noexcept;

    [[nodiscard]] std::size_t symbol_field_count() const noexcept;

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
