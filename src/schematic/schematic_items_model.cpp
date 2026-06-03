#include <volt/schematic/schematic_items_model.hpp>

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace volt {

[[nodiscard]] SymbolInstanceId SchematicItemsModel::add_symbol_instance(SymbolInstance instance) {
    return symbol_instances_.insert(instance);
}

[[nodiscard]] WireRunId SchematicItemsModel::add_wire_run(WireRun wire) {
    return wire_runs_.insert(std::move(wire));
}

[[nodiscard]] NetLabelId SchematicItemsModel::add_net_label(NetLabel label) {
    return net_labels_.insert(std::move(label));
}

[[nodiscard]] JunctionId SchematicItemsModel::add_junction(Junction junction) {
    return junctions_.insert(junction);
}

[[nodiscard]] PowerPortId SchematicItemsModel::add_power_port(PowerPort port) {
    return power_ports_.insert(std::move(port));
}

[[nodiscard]] NoConnectMarkerId SchematicItemsModel::add_no_connect_marker(NoConnectMarker marker) {
    return no_connect_markers_.insert(std::move(marker));
}

[[nodiscard]] SheetPortId SchematicItemsModel::add_sheet_port(SheetPort port) {
    return sheet_ports_.insert(std::move(port));
}

[[nodiscard]] SymbolFieldId SchematicItemsModel::add_symbol_field(SymbolField field) {
    require_symbol_instance(field.symbol_instance());
    return symbol_fields_.insert(std::move(field));
}

[[nodiscard]] const SymbolInstance &
SchematicItemsModel::symbol_instance(SymbolInstanceId id) const {
    return symbol_instances_.get(id);
}

[[nodiscard]] const WireRun &SchematicItemsModel::wire_run(WireRunId id) const {
    return wire_runs_.get(id);
}

[[nodiscard]] const NetLabel &SchematicItemsModel::net_label(NetLabelId id) const {
    return net_labels_.get(id);
}

void SchematicItemsModel::move_net_label_text(NetLabelId id, Point position) {
    net_labels_.get(id).move_text_to(position);
}

[[nodiscard]] const Junction &SchematicItemsModel::junction(JunctionId id) const {
    return junctions_.get(id);
}

[[nodiscard]] const PowerPort &SchematicItemsModel::power_port(PowerPortId id) const {
    return power_ports_.get(id);
}

void SchematicItemsModel::move_power_port_label(PowerPortId id, Point position) {
    power_ports_.get(id).move_label_to(position);
}

[[nodiscard]] const NoConnectMarker &
SchematicItemsModel::no_connect_marker(NoConnectMarkerId id) const {
    return no_connect_markers_.get(id);
}

[[nodiscard]] const SheetPort &SchematicItemsModel::sheet_port(SheetPortId id) const {
    return sheet_ports_.get(id);
}

[[nodiscard]] const SymbolField &SchematicItemsModel::symbol_field(SymbolFieldId id) const {
    return symbol_fields_.get(id);
}

void SchematicItemsModel::move_symbol_field(SymbolFieldId id, Point position) {
    symbol_fields_.get(id).move_to(position);
}

[[nodiscard]] std::size_t SchematicItemsModel::symbol_instance_count() const noexcept {
    return symbol_instances_.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::wire_run_count() const noexcept {
    return wire_runs_.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::net_label_count() const noexcept {
    return net_labels_.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::junction_count() const noexcept {
    return junctions_.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::power_port_count() const noexcept {
    return power_ports_.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::no_connect_marker_count() const noexcept {
    return no_connect_markers_.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::sheet_port_count() const noexcept {
    return sheet_ports_.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::symbol_field_count() const noexcept {
    return symbol_fields_.size();
}

void SchematicItemsModel::require_symbol_instance(SymbolInstanceId instance) const {
    if (!symbol_instances_.contains(instance)) {
        throw std::out_of_range{"Symbol instance ID does not belong to this schematic"};
    }
}

} // namespace volt
