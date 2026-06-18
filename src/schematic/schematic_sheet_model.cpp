#include <volt/schematic/schematic_sheet_model.hpp>

#include "schematic_storage.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace volt {

SchematicSheetModel::SchematicSheetModel()
    : SchematicSheetModel{std::make_shared<detail::SchematicSheetState>()} {}

SchematicSheetModel::SchematicSheetModel(std::shared_ptr<const detail::SchematicSheetState> state)
    : state_{std::move(state)} {}

SchematicSheetModel::SchematicSheetModel(const SchematicSheetModel &other)
    : SchematicSheetModel{std::make_shared<detail::SchematicSheetState>(other.state())} {}

SchematicSheetModel::SchematicSheetModel(SchematicSheetModel &&other) noexcept = default;

SchematicSheetModel &SchematicSheetModel::operator=(const SchematicSheetModel &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::SchematicSheetState>(other.state());
    }
    return *this;
}

SchematicSheetModel &SchematicSheetModel::operator=(SchematicSheetModel &&other) noexcept = default;

SchematicSheetModel::~SchematicSheetModel() = default;

Schematic::SheetStorage::SheetStorage()
    : SheetStorage{std::make_shared<detail::SchematicSheetState>()} {}

Schematic::SheetStorage::SheetStorage(std::shared_ptr<detail::SchematicSheetState> state)
    : SchematicSheetModel{state}, state_{std::move(state)} {}

Schematic::SheetStorage::SheetStorage(const SheetStorage &other)
    : SheetStorage{std::make_shared<detail::SchematicSheetState>(other.state())} {}

Schematic::SheetStorage &Schematic::SheetStorage::operator=(const SheetStorage &other) {
    if (this != &other) {
        auto replacement =
            SheetStorage{std::make_shared<detail::SchematicSheetState>(other.state())};
        *this = std::move(replacement);
    }
    return *this;
}

[[nodiscard]] detail::SchematicSheetState &Schematic::SheetStorage::mutable_state() noexcept {
    return *state_;
}

[[nodiscard]] const detail::SchematicSheetState &Schematic::SheetStorage::state() const noexcept {
    return *state_;
}

[[nodiscard]] SheetId Schematic::SheetStorage::add_sheet(Sheet sheet) {
    if (sheet_by_name(sheet.name()).has_value()) {
        throw std::logic_error{"Sheet name already exists"};
    }

    return mutable_state().sheets.insert(detail::SheetStorage{std::move(sheet)});
}

[[nodiscard]] std::size_t Schematic::SheetStorage::add_sheet_region(SheetId sheet_id,
                                                                    SheetRegion region) {
    require_sheet(sheet_id);
    auto &sheet_ref = mutable_state().sheets.get(sheet_id);
    if (sheet_ref.region_by_name(region.name()).has_value()) {
        throw std::logic_error{"Sheet region name already exists"};
    }

    return sheet_ref.add_region(std::move(region));
}

void Schematic::SheetStorage::add_symbol_instance(SheetId sheet_id, SymbolInstanceId instance) {
    mutable_state().sheets.get(sheet_id).add_symbol_instance(instance);
}

void Schematic::SheetStorage::add_wire_run(SheetId sheet_id, WireRunId wire) {
    mutable_state().sheets.get(sheet_id).add_wire_run(wire);
}

void Schematic::SheetStorage::add_net_label(SheetId sheet_id, NetLabelId label) {
    mutable_state().sheets.get(sheet_id).add_net_label(label);
}

void Schematic::SheetStorage::add_junction(SheetId sheet_id, JunctionId junction) {
    mutable_state().sheets.get(sheet_id).add_junction(junction);
}

void Schematic::SheetStorage::add_power_port(SheetId sheet_id, PowerPortId port) {
    mutable_state().sheets.get(sheet_id).add_power_port(port);
}

void Schematic::SheetStorage::add_no_connect_marker(SheetId sheet_id, NoConnectMarkerId marker) {
    mutable_state().sheets.get(sheet_id).add_no_connect_marker(marker);
}

void Schematic::SheetStorage::add_sheet_port(SheetId sheet_id, SheetPortId port) {
    mutable_state().sheets.get(sheet_id).add_sheet_port(port);
}

void Schematic::SheetStorage::add_symbol_field(SheetId sheet_id, SymbolFieldId field) {
    mutable_state().sheets.get(sheet_id).add_symbol_field(field);
}

[[nodiscard]] std::optional<SheetId>
SchematicSheetModel::sheet_by_name(const std::string &name) const {
    for (std::size_t index = 0; index < state().sheets.size(); ++index) {
        const auto id = SheetId{index};
        if (state().sheets.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t>
SchematicSheetModel::sheet_region_by_name(SheetId sheet_id, const std::string &name) const {
    require_sheet(sheet_id);
    return state().sheets.get(sheet_id).region_by_name(name);
}

[[nodiscard]] const Sheet &SchematicSheetModel::sheet(SheetId id) const {
    return state().sheets.get(id);
}

[[nodiscard]] const SheetRegion &SchematicSheetModel::sheet_region(SheetId sheet_id,
                                                                   std::size_t region) const {
    require_sheet(sheet_id);
    return state().sheets.get(sheet_id).region(region);
}

[[nodiscard]] std::size_t SchematicSheetModel::sheet_count() const noexcept {
    return state().sheets.size();
}

void SchematicSheetModel::require_sheet(SheetId sheet_id) const {
    if (!state().sheets.contains(sheet_id)) {
        throw std::out_of_range{"Sheet ID does not belong to this schematic"};
    }
}

[[nodiscard]] const detail::SchematicSheetState &SchematicSheetModel::state() const noexcept {
    return *state_;
}

} // namespace volt
