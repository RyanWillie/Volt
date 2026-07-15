#include <volt/schematic/schematic.hpp>

#include <volt/core/errors.hpp>

#include "schematic_storage.hpp"

#include <cstddef>
#include <memory>
#include <utility>

namespace volt {

Schematic::ItemStorage::ItemStorage()
    : ItemStorage{std::make_shared<detail::SchematicItemsState>()} {}

Schematic::ItemStorage::ItemStorage(std::shared_ptr<detail::SchematicItemsState> state)
    : state_{std::move(state)} {}

Schematic::ItemStorage::ItemStorage(const ItemStorage &other)
    : ItemStorage{std::make_shared<detail::SchematicItemsState>(other.state())} {}

Schematic::ItemStorage &Schematic::ItemStorage::operator=(const ItemStorage &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::SchematicItemsState>(other.state());
    }
    return *this;
}

[[nodiscard]] detail::SchematicItemsState &Schematic::ItemStorage::mutable_state() noexcept {
    return *state_;
}

[[nodiscard]] const detail::SchematicItemsState &Schematic::ItemStorage::state() const noexcept {
    return *state_;
}

[[nodiscard]] SymbolInstanceId Schematic::ItemStorage::add(SymbolInstance instance) {
    return mutable_state().symbol_instances.insert(instance);
}

[[nodiscard]] WireRunId Schematic::ItemStorage::add(WireRun wire) {
    return mutable_state().wire_runs.insert(std::move(wire));
}

[[nodiscard]] NetLabelId Schematic::ItemStorage::add(NetLabel label) {
    return mutable_state().net_labels.insert(std::move(label));
}

[[nodiscard]] JunctionId Schematic::ItemStorage::add(Junction junction) {
    return mutable_state().junctions.insert(junction);
}

[[nodiscard]] PowerPortId Schematic::ItemStorage::add(PowerPort port) {
    return mutable_state().power_ports.insert(std::move(port));
}

[[nodiscard]] NoConnectMarkerId Schematic::ItemStorage::add(NoConnectMarker marker) {
    return mutable_state().no_connect_markers.insert(std::move(marker));
}

[[nodiscard]] SheetPortId Schematic::ItemStorage::add(SheetPort port) {
    return mutable_state().sheet_ports.insert(std::move(port));
}

[[nodiscard]] SymbolFieldId Schematic::ItemStorage::add(SymbolField field) {
    require(field.symbol_instance());
    return mutable_state().symbol_fields.insert(std::move(field));
}

[[nodiscard]] const SymbolInstance &Schematic::ItemStorage::get(SymbolInstanceId id) const {
    return state().symbol_instances.get(id);
}

[[nodiscard]] const WireRun &Schematic::ItemStorage::get(WireRunId id) const {
    return state().wire_runs.get(id);
}

[[nodiscard]] const NetLabel &Schematic::ItemStorage::get(NetLabelId id) const {
    return state().net_labels.get(id);
}

[[nodiscard]] const Junction &Schematic::ItemStorage::get(JunctionId id) const {
    return state().junctions.get(id);
}

[[nodiscard]] const PowerPort &Schematic::ItemStorage::get(PowerPortId id) const {
    return state().power_ports.get(id);
}

[[nodiscard]] const NoConnectMarker &Schematic::ItemStorage::get(NoConnectMarkerId id) const {
    return state().no_connect_markers.get(id);
}

[[nodiscard]] const SheetPort &Schematic::ItemStorage::get(SheetPortId id) const {
    return state().sheet_ports.get(id);
}

[[nodiscard]] const SymbolField &Schematic::ItemStorage::get(SymbolFieldId id) const {
    return state().symbol_fields.get(id);
}

[[nodiscard]] std::size_t Schematic::ItemStorage::size(SymbolInstanceId) const noexcept {
    return state().symbol_instances.size();
}

[[nodiscard]] std::size_t Schematic::ItemStorage::size(WireRunId) const noexcept {
    return state().wire_runs.size();
}

[[nodiscard]] std::size_t Schematic::ItemStorage::size(NetLabelId) const noexcept {
    return state().net_labels.size();
}

[[nodiscard]] std::size_t Schematic::ItemStorage::size(JunctionId) const noexcept {
    return state().junctions.size();
}

[[nodiscard]] std::size_t Schematic::ItemStorage::size(PowerPortId) const noexcept {
    return state().power_ports.size();
}

[[nodiscard]] std::size_t Schematic::ItemStorage::size(NoConnectMarkerId) const noexcept {
    return state().no_connect_markers.size();
}

[[nodiscard]] std::size_t Schematic::ItemStorage::size(SheetPortId) const noexcept {
    return state().sheet_ports.size();
}

[[nodiscard]] std::size_t Schematic::ItemStorage::size(SymbolFieldId) const noexcept {
    return state().symbol_fields.size();
}

void Schematic::ItemStorage::move(MoveNetLabelText change) {
    auto &label = mutable_state().net_labels.get(change.label);
    label = label.with_text_position(change.position);
}

void Schematic::ItemStorage::move(MovePowerPortLabel change) {
    auto &port = mutable_state().power_ports.get(change.port);
    port = port.with_label_position(change.position);
}

void Schematic::ItemStorage::move(MoveSymbolField change) {
    auto &field = mutable_state().symbol_fields.get(change.field);
    field = field.with_position(change.position);
}

void Schematic::ItemStorage::require(SymbolInstanceId id) const {
    if (!state().symbol_instances.contains(id)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Symbol instance ID does not belong to this schematic",
                               EntityRef::symbol_instance(id)};
    }
}

} // namespace volt
