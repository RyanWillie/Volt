#pragma once

#include <memory>
#include <utility>

#include <volt/core/entity_table.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt::detail {

struct SheetState {
    SheetState(std::string sheet_name, SheetMetadata sheet_metadata)
        : name{std::move(sheet_name)}, metadata{std::move(sheet_metadata)} {}

    std::string name;
    SheetMetadata metadata;
    std::vector<SymbolInstanceId> symbol_instances;
    std::vector<WireRunId> wire_runs;
    std::vector<NetLabelId> net_labels;
    std::vector<JunctionId> junctions;
    std::vector<PowerPortId> power_ports;
    std::vector<NoConnectMarkerId> no_connect_markers;
    std::vector<SheetPortId> sheet_ports;
    std::vector<SymbolFieldId> symbol_fields;
    std::vector<SheetRegion> regions;
};

class SheetStorage : public Sheet {
  public:
    explicit SheetStorage(std::string name);
    SheetStorage(std::string name, SheetMetadata metadata);
    SheetStorage(const SheetStorage &other);
    SheetStorage(SheetStorage &&other) noexcept = default;
    SheetStorage &operator=(const SheetStorage &other);
    SheetStorage &operator=(SheetStorage &&other) noexcept = default;

    explicit SheetStorage(Sheet sheet);

    std::size_t add_region(SheetRegion region) {
        state_->regions.push_back(std::move(region));
        return state_->regions.size() - 1U;
    }

    void add_symbol_instance(SymbolInstanceId instance) {
        state_->symbol_instances.push_back(instance);
    }

    void add_wire_run(WireRunId wire) { state_->wire_runs.push_back(wire); }

    void add_net_label(NetLabelId label) { state_->net_labels.push_back(label); }

    void add_junction(JunctionId junction) { state_->junctions.push_back(junction); }

    void add_power_port(PowerPortId port) { state_->power_ports.push_back(port); }

    void add_no_connect_marker(NoConnectMarkerId marker) {
        state_->no_connect_markers.push_back(marker);
    }

    void add_sheet_port(SheetPortId port) { state_->sheet_ports.push_back(port); }

    void add_symbol_field(SymbolFieldId field) { state_->symbol_fields.push_back(field); }

  private:
    explicit SheetStorage(std::shared_ptr<SheetState> state);

    [[nodiscard]] const SheetState &state() const noexcept { return *state_; }

    std::shared_ptr<SheetState> state_;
};

struct SchematicLibraryState {
    EntityTable<SymbolDefinition, SymbolDefId> symbol_definitions;
};

struct SheetRegionLocation {
    SheetId sheet;
    std::size_t index;
};

struct SchematicSheetState {
    EntityTable<SheetStorage, SheetId> sheets;
    EntityTable<SheetRegionLocation, SheetRegionId> regions;
};

struct SchematicItemsState {
    EntityTable<SymbolInstance, SymbolInstanceId> symbol_instances;
    EntityTable<WireRun, WireRunId> wire_runs;
    EntityTable<NetLabel, NetLabelId> net_labels;
    EntityTable<Junction, JunctionId> junctions;
    EntityTable<PowerPort, PowerPortId> power_ports;
    EntityTable<NoConnectMarker, NoConnectMarkerId> no_connect_markers;
    EntityTable<SheetPort, SheetPortId> sheet_ports;
    EntityTable<SymbolField, SymbolFieldId> symbol_fields;
};

} // namespace volt::detail
