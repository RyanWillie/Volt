#pragma once

#include <cstddef>

#include <volt/core/ids.hpp>

namespace volt {

/** Kind of entity referenced by a diagnostic. */
enum class EntityKind {
    Board,
    PartDefinition,
    ComponentDef,
    Component,
    PinDef,
    Pin,
    Net,
    NetClass,
    ModuleDef,
    TemplateNetDef,
    ModuleComponent,
    ModuleInstance,
    PortDef,
    SymbolDef,
    Sheet,
    SymbolInstance,
    WireRun,
    NetLabel,
    Junction,
    PowerPort,
    NoConnectMarker,
    SheetPort,
    SymbolField,
    BoardLayer,
    BoardFeature,
    BoardTrack,
    BoardVia,
    BoardZone,
    BoardKeepout,
    BoardRoom,
    BoardText,
    FootprintDef,
    FootprintPad,
    ComponentPlacement,
};

/**
 * Reference to an entity involved in a diagnostic.
 *
 * EntityRef is a reporting type, not the core storage model. It deliberately stores kind
 * plus index so diagnostics can refer to different entity families uniformly.
 */
class EntityRef {
  public:
    /** Create a reference to the board projection root. */
    [[nodiscard]] static EntityRef board() noexcept { return EntityRef{EntityKind::Board, 0U}; }

    /** Create a reference to a standalone part definition artifact. */
    [[nodiscard]] static EntityRef part_definition() noexcept {
        return EntityRef{EntityKind::PartDefinition, 0U};
    }

    /** Create a reference to a component definition. */
    [[nodiscard]] static EntityRef component_def(ComponentDefId id) noexcept {
        return EntityRef{EntityKind::ComponentDef, id.index()};
    }

    /** Create a reference to a component instance. */
    [[nodiscard]] static EntityRef component(ComponentId id) noexcept {
        return EntityRef{EntityKind::Component, id.index()};
    }

    /** Create a reference to a pin definition. */
    [[nodiscard]] static EntityRef pin_def(PinDefId id) noexcept {
        return EntityRef{EntityKind::PinDef, id.index()};
    }

    /** Create a reference to a pin instance. */
    [[nodiscard]] static EntityRef pin(PinId id) noexcept {
        return EntityRef{EntityKind::Pin, id.index()};
    }

    /** Create a reference to a net. */
    [[nodiscard]] static EntityRef net(NetId id) noexcept {
        return EntityRef{EntityKind::Net, id.index()};
    }

    /** Create a reference to a net class. */
    [[nodiscard]] static EntityRef net_class(NetClassId id) noexcept {
        return EntityRef{EntityKind::NetClass, id.index()};
    }

    /** Create a reference to a module definition. */
    [[nodiscard]] static EntityRef module_def(ModuleDefId id) noexcept {
        return EntityRef{EntityKind::ModuleDef, id.index()};
    }

    /** Create a reference to a template-local net definition. */
    [[nodiscard]] static EntityRef template_net_def(TemplateNetDefId id) noexcept {
        return EntityRef{EntityKind::TemplateNetDef, id.index()};
    }

    /** Create a reference to a component occurrence inside a module definition. */
    [[nodiscard]] static EntityRef module_component(ModuleComponentId id) noexcept {
        return EntityRef{EntityKind::ModuleComponent, id.index()};
    }

    /** Create a reference to a module instance. */
    [[nodiscard]] static EntityRef module_instance(ModuleInstanceId id) noexcept {
        return EntityRef{EntityKind::ModuleInstance, id.index()};
    }

    /** Create a reference to a module port definition. */
    [[nodiscard]] static EntityRef port_def(PortDefId id) noexcept {
        return EntityRef{EntityKind::PortDef, id.index()};
    }

    /** Create a reference to a schematic symbol definition. */
    [[nodiscard]] static EntityRef symbol_def(SymbolDefId id) noexcept {
        return EntityRef{EntityKind::SymbolDef, id.index()};
    }

    /** Create a reference to a schematic sheet. */
    [[nodiscard]] static EntityRef sheet(SheetId id) noexcept {
        return EntityRef{EntityKind::Sheet, id.index()};
    }

    /** Create a reference to a placed schematic symbol instance. */
    [[nodiscard]] static EntityRef symbol_instance(SymbolInstanceId id) noexcept {
        return EntityRef{EntityKind::SymbolInstance, id.index()};
    }

    /** Create a reference to a schematic wire run. */
    [[nodiscard]] static EntityRef wire_run(WireRunId id) noexcept {
        return EntityRef{EntityKind::WireRun, id.index()};
    }

    /** Create a reference to a schematic net label. */
    [[nodiscard]] static EntityRef net_label(NetLabelId id) noexcept {
        return EntityRef{EntityKind::NetLabel, id.index()};
    }

    /** Create a reference to a schematic junction. */
    [[nodiscard]] static EntityRef junction(JunctionId id) noexcept {
        return EntityRef{EntityKind::Junction, id.index()};
    }

    /** Create a reference to a schematic power or ground port. */
    [[nodiscard]] static EntityRef power_port(PowerPortId id) noexcept {
        return EntityRef{EntityKind::PowerPort, id.index()};
    }

    /** Create a reference to a schematic no-connect marker. */
    [[nodiscard]] static EntityRef no_connect_marker(NoConnectMarkerId id) noexcept {
        return EntityRef{EntityKind::NoConnectMarker, id.index()};
    }

    /** Create a reference to a schematic sheet/off-page port. */
    [[nodiscard]] static EntityRef sheet_port(SheetPortId id) noexcept {
        return EntityRef{EntityKind::SheetPort, id.index()};
    }

    /** Create a reference to a schematic symbol field. */
    [[nodiscard]] static EntityRef symbol_field(SymbolFieldId id) noexcept {
        return EntityRef{EntityKind::SymbolField, id.index()};
    }

    /** Create a reference to a PCB board layer. */
    [[nodiscard]] static EntityRef board_layer(BoardLayerId id) noexcept {
        return EntityRef{EntityKind::BoardLayer, id.index()};
    }

    /** Create a reference to a PCB board feature. */
    [[nodiscard]] static EntityRef board_feature(BoardFeatureId id) noexcept {
        return EntityRef{EntityKind::BoardFeature, id.index()};
    }

    /** Create a reference to a PCB track. */
    [[nodiscard]] static EntityRef board_track(BoardTrackId id) noexcept {
        return EntityRef{EntityKind::BoardTrack, id.index()};
    }

    /** Create a reference to a PCB via. */
    [[nodiscard]] static EntityRef board_via(BoardViaId id) noexcept {
        return EntityRef{EntityKind::BoardVia, id.index()};
    }

    /** Create a reference to a PCB zone. */
    [[nodiscard]] static EntityRef board_zone(BoardZoneId id) noexcept {
        return EntityRef{EntityKind::BoardZone, id.index()};
    }

    /** Create a reference to a PCB keepout. */
    [[nodiscard]] static EntityRef board_keepout(BoardKeepoutId id) noexcept {
        return EntityRef{EntityKind::BoardKeepout, id.index()};
    }

    /** Create a reference to a PCB room. */
    [[nodiscard]] static EntityRef board_room(BoardRoomId id) noexcept {
        return EntityRef{EntityKind::BoardRoom, id.index()};
    }

    /** Create a reference to PCB board text. */
    [[nodiscard]] static EntityRef board_text(BoardTextId id) noexcept {
        return EntityRef{EntityKind::BoardText, id.index()};
    }

    /** Create a reference to a cached PCB footprint definition. */
    [[nodiscard]] static EntityRef footprint_def(FootprintDefId id) noexcept {
        return EntityRef{EntityKind::FootprintDef, id.index()};
    }

    /** Create a reference to a PCB footprint pad. */
    [[nodiscard]] static EntityRef footprint_pad(FootprintPadId id) noexcept {
        return EntityRef{EntityKind::FootprintPad, id.index()};
    }

    /** Create a reference to a PCB component placement. */
    [[nodiscard]] static EntityRef component_placement(ComponentPlacementId id) noexcept {
        return EntityRef{EntityKind::ComponentPlacement, id.index()};
    }

    /** Return the kind of entity referenced. */
    [[nodiscard]] EntityKind kind() const noexcept { return kind_; }

    /** Return the entity table index referenced. */
    [[nodiscard]] std::size_t index() const noexcept { return index_; }

    /** Return whether two entity references point at the same entity kind and index. */
    [[nodiscard]] friend bool operator==(EntityRef lhs, EntityRef rhs) noexcept = default;

  private:
    constexpr EntityRef(EntityKind kind, std::size_t index) noexcept : kind_{kind}, index_{index} {}

    EntityKind kind_;
    std::size_t index_;
};

} // namespace volt
