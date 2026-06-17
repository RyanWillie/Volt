#include <volt/schematic/schematic_sheet_model.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include "../core/mutation_access.hpp"

namespace volt {

[[nodiscard]] SheetId SchematicSheetModel::add_sheet(Sheet sheet) {
    if (sheet_by_name(sheet.name()).has_value()) {
        throw std::logic_error{"Sheet name already exists"};
    }

    return sheets_.insert(std::move(sheet));
}

[[nodiscard]] std::size_t SchematicSheetModel::add_sheet_region(SheetId sheet_id,
                                                                SheetRegion region) {
    require_sheet(sheet_id);
    auto &sheet_ref = mutable_sheet(sheet_id);
    if (sheet_ref.region_by_name(region.name()).has_value()) {
        throw std::logic_error{"Sheet region name already exists"};
    }

    return sheet_ref.add_region(detail::kernel_mutation_access(), std::move(region));
}

void SchematicSheetModel::add_symbol_instance(detail::KernelMutationAccess access, SheetId sheet_id,
                                              SymbolInstanceId instance) {
    mutable_sheet(sheet_id).add_symbol_instance(access, instance);
}

void SchematicSheetModel::add_wire_run(detail::KernelMutationAccess access, SheetId sheet_id,
                                       WireRunId wire) {
    mutable_sheet(sheet_id).add_wire_run(access, wire);
}

void SchematicSheetModel::add_net_label(detail::KernelMutationAccess access, SheetId sheet_id,
                                        NetLabelId label) {
    mutable_sheet(sheet_id).add_net_label(access, label);
}

void SchematicSheetModel::add_junction(detail::KernelMutationAccess access, SheetId sheet_id,
                                       JunctionId junction) {
    mutable_sheet(sheet_id).add_junction(access, junction);
}

void SchematicSheetModel::add_power_port(detail::KernelMutationAccess access, SheetId sheet_id,
                                         PowerPortId port) {
    mutable_sheet(sheet_id).add_power_port(access, port);
}

void SchematicSheetModel::add_no_connect_marker(detail::KernelMutationAccess access,
                                                SheetId sheet_id, NoConnectMarkerId marker) {
    mutable_sheet(sheet_id).add_no_connect_marker(access, marker);
}

void SchematicSheetModel::add_sheet_port(detail::KernelMutationAccess access, SheetId sheet_id,
                                         SheetPortId port) {
    mutable_sheet(sheet_id).add_sheet_port(access, port);
}

void SchematicSheetModel::add_symbol_field(detail::KernelMutationAccess access, SheetId sheet_id,
                                           SymbolFieldId field) {
    mutable_sheet(sheet_id).add_symbol_field(access, field);
}

[[nodiscard]] std::optional<SheetId>
SchematicSheetModel::sheet_by_name(const std::string &name) const {
    for (std::size_t index = 0; index < sheets_.size(); ++index) {
        const auto id = SheetId{index};
        if (sheets_.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t>
SchematicSheetModel::sheet_region_by_name(SheetId sheet_id, const std::string &name) const {
    require_sheet(sheet_id);
    return sheets_.get(sheet_id).region_by_name(name);
}

[[nodiscard]] const Sheet &SchematicSheetModel::sheet(SheetId id) const { return sheets_.get(id); }

[[nodiscard]] const SheetRegion &SchematicSheetModel::sheet_region(SheetId sheet_id,
                                                                   std::size_t region) const {
    require_sheet(sheet_id);
    return sheets_.get(sheet_id).region(region);
}

[[nodiscard]] std::size_t SchematicSheetModel::sheet_count() const noexcept {
    return sheets_.size();
}

void SchematicSheetModel::require_sheet(SheetId sheet_id) const {
    if (!sheets_.contains(sheet_id)) {
        throw std::out_of_range{"Sheet ID does not belong to this schematic"};
    }
}

[[nodiscard]] Sheet &SchematicSheetModel::mutable_sheet(SheetId id) { return sheets_.get(id); }

} // namespace volt
