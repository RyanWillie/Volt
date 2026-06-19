#include "entity_ref_format.hpp"

#include <stdexcept>

#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/pcb/board.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt::io::detail {

[[nodiscard]] std::string entity_ref_serialized_id(EntityRef entity) {
    switch (entity.kind()) {
    case EntityKind::Board:
        return "board:0";
    case EntityKind::PartDefinition:
        return "part_definition:0";
    case EntityKind::ComponentDef:
        return encode_local_id(ComponentDefId{entity.index()});
    case EntityKind::Component:
        return encode_local_id(ComponentId{entity.index()});
    case EntityKind::PinDef:
        return encode_local_id(PinDefId{entity.index()});
    case EntityKind::Pin:
        return encode_local_id(PinId{entity.index()});
    case EntityKind::Net:
        return encode_local_id(NetId{entity.index()});
    case EntityKind::ModuleDef:
        return encode_local_id(ModuleDefId{entity.index()});
    case EntityKind::ModuleInstance:
        return encode_local_id(ModuleInstanceId{entity.index()});
    case EntityKind::PortDef:
        return encode_local_id(PortDefId{entity.index()});
    case EntityKind::SymbolDef:
        return encode_local_id(SymbolDefId{entity.index()});
    case EntityKind::Sheet:
        return encode_local_id(SheetId{entity.index()});
    case EntityKind::SymbolInstance:
        return encode_local_id(SymbolInstanceId{entity.index()});
    case EntityKind::WireRun:
        return encode_local_id(WireRunId{entity.index()});
    case EntityKind::NetLabel:
        return encode_local_id(NetLabelId{entity.index()});
    case EntityKind::Junction:
        return encode_local_id(JunctionId{entity.index()});
    case EntityKind::PowerPort:
        return encode_local_id(PowerPortId{entity.index()});
    case EntityKind::NoConnectMarker:
        return encode_local_id(NoConnectMarkerId{entity.index()});
    case EntityKind::SheetPort:
        return encode_local_id(SheetPortId{entity.index()});
    case EntityKind::SymbolField:
        return encode_local_id(SymbolFieldId{entity.index()});
    case EntityKind::BoardLayer:
        return encode_local_id(BoardLayerId{entity.index()});
    case EntityKind::BoardFeature:
        return encode_local_id(BoardFeatureId{entity.index()});
    case EntityKind::BoardTrack:
        return encode_local_id(BoardTrackId{entity.index()});
    case EntityKind::BoardVia:
        return encode_local_id(BoardViaId{entity.index()});
    case EntityKind::BoardZone:
        return encode_local_id(BoardZoneId{entity.index()});
    case EntityKind::BoardKeepout:
        return encode_local_id(BoardKeepoutId{entity.index()});
    case EntityKind::BoardRoom:
        return encode_local_id(BoardRoomId{entity.index()});
    case EntityKind::BoardText:
        return encode_local_id(BoardTextId{entity.index()});
    case EntityKind::FootprintDef:
        return encode_local_id(FootprintDefId{entity.index()});
    case EntityKind::FootprintPad:
        return encode_local_id(FootprintPadId{entity.index()});
    case EntityKind::ComponentPlacement:
        return encode_local_id(ComponentPlacementId{entity.index()});
    }
    throw std::logic_error{"Unhandled diagnostic entity kind"};
}

} // namespace volt::io::detail
