#include <volt/schematic/schematic.hpp>

#include <volt/core/errors.hpp>

#include "schematic_storage.hpp"

#include <cstddef>
#include <memory>
#include <utility>

namespace volt {

Schematic::SheetStorage::SheetStorage()
    : SheetStorage{std::make_shared<detail::SchematicSheetState>()} {}

Schematic::SheetStorage::SheetStorage(std::shared_ptr<detail::SchematicSheetState> state)
    : state_{std::move(state)} {}

Schematic::SheetStorage::SheetStorage(const SheetStorage &other)
    : SheetStorage{std::make_shared<detail::SchematicSheetState>(other.state())} {}

Schematic::SheetStorage &Schematic::SheetStorage::operator=(const SheetStorage &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::SchematicSheetState>(other.state());
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
    for (std::size_t index = 0; index < state().sheets.size(); ++index) {
        const auto id = SheetId{index};
        if (state().sheets.get(id).name() == sheet.name()) {
            throw KernelLogicError{ErrorCode::DuplicateName, "Sheet name already exists",
                                   EntityRef::sheet(id)};
        }
    }

    const auto region_count = sheet.regions().size();
    const auto id = mutable_state().sheets.insert(detail::SheetStorage{std::move(sheet)});
    for (std::size_t index = 0; index < region_count; ++index) {
        static_cast<void>(mutable_state().regions.insert(detail::SheetRegionLocation{id, index}));
    }
    return id;
}

[[nodiscard]] SheetRegionId Schematic::SheetStorage::add_sheet_region(SheetId sheet_id,
                                                                      SheetRegion region) {
    require(sheet_id);
    auto &sheet = mutable_state().sheets.get(sheet_id);
    if (sheet.region_by_name(region.name()).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Sheet region name already exists"};
    }

    const auto index = sheet.add_region(std::move(region));
    return mutable_state().regions.insert(detail::SheetRegionLocation{sheet_id, index});
}

void Schematic::SheetStorage::add_symbol_instance(SheetId sheet, SymbolInstanceId instance) {
    mutable_state().sheets.get(sheet).add_symbol_instance(instance);
}

void Schematic::SheetStorage::add_wire_run(SheetId sheet, WireRunId wire) {
    mutable_state().sheets.get(sheet).add_wire_run(wire);
}

void Schematic::SheetStorage::add_net_label(SheetId sheet, NetLabelId label) {
    mutable_state().sheets.get(sheet).add_net_label(label);
}

void Schematic::SheetStorage::add_junction(SheetId sheet, JunctionId junction) {
    mutable_state().sheets.get(sheet).add_junction(junction);
}

void Schematic::SheetStorage::add_power_port(SheetId sheet, PowerPortId port) {
    mutable_state().sheets.get(sheet).add_power_port(port);
}

void Schematic::SheetStorage::add_no_connect_marker(SheetId sheet, NoConnectMarkerId marker) {
    mutable_state().sheets.get(sheet).add_no_connect_marker(marker);
}

void Schematic::SheetStorage::add_sheet_port(SheetId sheet, SheetPortId port) {
    mutable_state().sheets.get(sheet).add_sheet_port(port);
}

void Schematic::SheetStorage::add_symbol_field(SheetId sheet, SymbolFieldId field) {
    mutable_state().sheets.get(sheet).add_symbol_field(field);
}

[[nodiscard]] const Sheet &Schematic::SheetStorage::get(SheetId id) const {
    return state().sheets.get(id);
}

[[nodiscard]] const SheetRegion &Schematic::SheetStorage::get(SheetRegionId id) const {
    const auto &location = state().regions.get(id);
    return state().sheets.get(location.sheet).region(location.index);
}

[[nodiscard]] std::size_t Schematic::SheetStorage::size(SheetId) const noexcept {
    return state().sheets.size();
}

[[nodiscard]] std::size_t Schematic::SheetStorage::size(SheetRegionId) const noexcept {
    return state().regions.size();
}

void Schematic::SheetStorage::require(SheetId id) const {
    if (!state().sheets.contains(id)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Sheet ID does not belong to this schematic", EntityRef::sheet(id)};
    }
}

} // namespace volt
