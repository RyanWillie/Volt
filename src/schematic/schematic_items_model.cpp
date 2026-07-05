#include <volt/schematic/schematic_items_model.hpp>

#include <volt/core/errors.hpp>

#include "schematic_storage.hpp"

#include <cstddef>
#include <memory>
#include <utility>

namespace volt {

SchematicItemsModel::SchematicItemsModel()
    : SchematicItemsModel{std::make_shared<detail::SchematicItemsState>()} {}

SchematicItemsModel::SchematicItemsModel(std::shared_ptr<const detail::SchematicItemsState> state)
    : state_{std::move(state)} {}

SchematicItemsModel::SchematicItemsModel(const SchematicItemsModel &other)
    : SchematicItemsModel{std::make_shared<detail::SchematicItemsState>(other.state())} {}

SchematicItemsModel::SchematicItemsModel(SchematicItemsModel &&other) noexcept = default;

SchematicItemsModel &SchematicItemsModel::operator=(const SchematicItemsModel &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::SchematicItemsState>(other.state());
    }
    return *this;
}

SchematicItemsModel &SchematicItemsModel::operator=(SchematicItemsModel &&other) noexcept = default;

SchematicItemsModel::~SchematicItemsModel() = default;

Schematic::ItemStorage::ItemStorage()
    : ItemStorage{std::make_shared<detail::SchematicItemsState>()} {}

Schematic::ItemStorage::ItemStorage(std::shared_ptr<detail::SchematicItemsState> state)
    : SchematicItemsModel{state}, state_{std::move(state)} {}

Schematic::ItemStorage::ItemStorage(const ItemStorage &other)
    : ItemStorage{std::make_shared<detail::SchematicItemsState>(other.state())} {}

Schematic::ItemStorage &Schematic::ItemStorage::operator=(const ItemStorage &other) {
    if (this != &other) {
        auto replacement =
            ItemStorage{std::make_shared<detail::SchematicItemsState>(other.state())};
        *this = std::move(replacement);
    }
    return *this;
}

[[nodiscard]] detail::SchematicItemsState &Schematic::ItemStorage::mutable_state() noexcept {
    return *state_;
}

[[nodiscard]] const detail::SchematicItemsState &Schematic::ItemStorage::state() const noexcept {
    return *state_;
}

[[nodiscard]] SymbolInstanceId
Schematic::ItemStorage::add_symbol_instance(SymbolInstance instance) {
    return mutable_state().symbol_instances.insert(instance);
}

[[nodiscard]] WireRunId Schematic::ItemStorage::add_wire_run(WireRun wire) {
    return mutable_state().wire_runs.insert(std::move(wire));
}

[[nodiscard]] NetLabelId Schematic::ItemStorage::add_net_label(NetLabel label) {
    return mutable_state().net_labels.insert(std::move(label));
}

[[nodiscard]] JunctionId Schematic::ItemStorage::add_junction(Junction junction) {
    return mutable_state().junctions.insert(junction);
}

[[nodiscard]] PowerPortId Schematic::ItemStorage::add_power_port(PowerPort port) {
    return mutable_state().power_ports.insert(std::move(port));
}

[[nodiscard]] NoConnectMarkerId
Schematic::ItemStorage::add_no_connect_marker(NoConnectMarker marker) {
    return mutable_state().no_connect_markers.insert(std::move(marker));
}

[[nodiscard]] SheetPortId Schematic::ItemStorage::add_sheet_port(SheetPort port) {
    return mutable_state().sheet_ports.insert(std::move(port));
}

[[nodiscard]] SymbolFieldId Schematic::ItemStorage::add_symbol_field(SymbolField field) {
    require_symbol_instance(field.symbol_instance());
    return mutable_state().symbol_fields.insert(std::move(field));
}

[[nodiscard]] const SymbolInstance &
SchematicItemsModel::symbol_instance(SymbolInstanceId id) const {
    return state().symbol_instances.get(id);
}

[[nodiscard]] const WireRun &SchematicItemsModel::wire_run(WireRunId id) const {
    return state().wire_runs.get(id);
}

[[nodiscard]] const NetLabel &SchematicItemsModel::net_label(NetLabelId id) const {
    return state().net_labels.get(id);
}

void Schematic::ItemStorage::move_net_label_text(NetLabelId id, Point position) {
    mutable_state().net_labels.get(id) =
        mutable_state().net_labels.get(id).with_text_position(position);
}

[[nodiscard]] const Junction &SchematicItemsModel::junction(JunctionId id) const {
    return state().junctions.get(id);
}

[[nodiscard]] const PowerPort &SchematicItemsModel::power_port(PowerPortId id) const {
    return state().power_ports.get(id);
}

void Schematic::ItemStorage::move_power_port_label(PowerPortId id, Point position) {
    mutable_state().power_ports.get(id) =
        mutable_state().power_ports.get(id).with_label_position(position);
}

[[nodiscard]] const NoConnectMarker &
SchematicItemsModel::no_connect_marker(NoConnectMarkerId id) const {
    return state().no_connect_markers.get(id);
}

[[nodiscard]] const SheetPort &SchematicItemsModel::sheet_port(SheetPortId id) const {
    return state().sheet_ports.get(id);
}

[[nodiscard]] const SymbolField &SchematicItemsModel::symbol_field(SymbolFieldId id) const {
    return state().symbol_fields.get(id);
}

void Schematic::ItemStorage::move_symbol_field(SymbolFieldId id, Point position) {
    mutable_state().symbol_fields.get(id) =
        mutable_state().symbol_fields.get(id).with_position(position);
}

[[nodiscard]] std::size_t SchematicItemsModel::symbol_instance_count() const noexcept {
    return state().symbol_instances.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::wire_run_count() const noexcept {
    return state().wire_runs.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::net_label_count() const noexcept {
    return state().net_labels.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::junction_count() const noexcept {
    return state().junctions.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::power_port_count() const noexcept {
    return state().power_ports.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::no_connect_marker_count() const noexcept {
    return state().no_connect_markers.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::sheet_port_count() const noexcept {
    return state().sheet_ports.size();
}

[[nodiscard]] std::size_t SchematicItemsModel::symbol_field_count() const noexcept {
    return state().symbol_fields.size();
}

void SchematicItemsModel::require_symbol_instance(SymbolInstanceId instance) const {
    if (!state().symbol_instances.contains(instance)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Symbol instance ID does not belong to this schematic",
                               EntityRef::symbol_instance(instance)};
    }
}

[[nodiscard]] const detail::SchematicItemsState &SchematicItemsModel::state() const noexcept {
    return *state_;
}

} // namespace volt
