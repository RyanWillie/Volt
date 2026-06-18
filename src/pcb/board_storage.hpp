#pragma once

#include <optional>

#include <volt/core/entity_table.hpp>
#include <volt/pcb/board.hpp>

namespace volt::detail {

struct BoardStructureState {
    EntityTable<BoardLayer, BoardLayerId> layers;
    std::optional<LayerStack> layer_stack;
    std::optional<BoardOutline> outline;
    BoardDesignRules design_rules;
    std::optional<BoardCapabilityProfile> capability_profile;
    EntityTable<BoardFeature, BoardFeatureId> features;
};

struct BoardFootprintState {
    EntityTable<FootprintDefinition, FootprintDefId> footprint_definitions;
};

struct BoardPlacementState {
    EntityTable<ComponentPlacement, ComponentPlacementId> placements;
};

struct BoardCopperState {
    EntityTable<BoardTrack, BoardTrackId> tracks;
    EntityTable<BoardVia, BoardViaId> vias;
    EntityTable<BoardZone, BoardZoneId> zones;
    EntityTable<BoardKeepout, BoardKeepoutId> keepouts;
    EntityTable<BoardRoom, BoardRoomId> rooms;
    EntityTable<BoardText, BoardTextId> texts;
};

} // namespace volt::detail
