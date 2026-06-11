#pragma once

#include <cstddef>

namespace volt {

/**
 * Strongly typed index used for internal kernel entity identity.
 *
 * The tag parameter makes different ID families non-interchangeable at compile time
 * while keeping each ID as a small table index.
 */
template <typename Tag> class EntityId {
  public:
    /** Integer type used by the backing entity table. */
    using index_type = std::size_t;

    /** Construct an ID for the given table index. */
    explicit constexpr EntityId(index_type index) noexcept : index_{index} {}

    /** Return the table index represented by this ID. */
    [[nodiscard]] constexpr index_type index() const noexcept { return index_; }

    /** Return whether two IDs of the same entity family have the same index. */
    [[nodiscard]] friend constexpr bool operator==(EntityId lhs, EntityId rhs) noexcept = default;

  private:
    index_type index_;
};

namespace detail {

struct ComponentDefIdTag;
struct ComponentIdTag;
struct PinDefIdTag;
struct PinIdTag;
struct NetIdTag;
struct ModuleDefIdTag;
struct ModuleInstanceIdTag;
struct TemplateNetDefIdTag;
struct ModuleComponentIdTag;
struct PortDefIdTag;
struct PortBindingIdTag;
struct NetClassIdTag;
struct SymbolDefIdTag;
struct SheetIdTag;
struct SymbolInstanceIdTag;
struct WireRunIdTag;
struct NetLabelIdTag;
struct JunctionIdTag;
struct PowerPortIdTag;
struct NoConnectMarkerIdTag;
struct SheetPortIdTag;
struct SymbolFieldIdTag;
struct BoardLayerIdTag;
struct BoardFeatureIdTag;
struct BoardTrackIdTag;
struct BoardViaIdTag;
struct BoardZoneIdTag;
struct BoardKeepoutIdTag;
struct BoardRoomIdTag;
struct BoardTextIdTag;
struct FootprintDefIdTag;
struct FootprintPadIdTag;
struct ComponentPlacementIdTag;

} // namespace detail

/** ID for a reusable component definition. */
using ComponentDefId = EntityId<detail::ComponentDefIdTag>;
/** ID for a component instance in the design. */
using ComponentId = EntityId<detail::ComponentIdTag>;
/** ID for a reusable pin definition. */
using PinDefId = EntityId<detail::PinDefIdTag>;
/** ID for a concrete pin instance in the design. */
using PinId = EntityId<detail::PinIdTag>;
/** ID for a canonical logical net in the design. */
using NetId = EntityId<detail::NetIdTag>;
/** ID for a reusable logical module definition. */
using ModuleDefId = EntityId<detail::ModuleDefIdTag>;
/** ID for a root-level module instance in the design. */
using ModuleInstanceId = EntityId<detail::ModuleInstanceIdTag>;
/** ID for a template-local net declared by a module definition. */
using TemplateNetDefId = EntityId<detail::TemplateNetDefIdTag>;
/** ID for a component occurrence declared inside a reusable module definition. */
using ModuleComponentId = EntityId<detail::ModuleComponentIdTag>;
/** ID for a module port definition. */
using PortDefId = EntityId<detail::PortDefIdTag>;
/** ID for an explicit module port binding edge. */
using PortBindingId = EntityId<detail::PortBindingIdTag>;
/** ID for a reusable net class intent definition. */
using NetClassId = EntityId<detail::NetClassIdTag>;
/** ID for a reusable schematic symbol definition. */
using SymbolDefId = EntityId<detail::SymbolDefIdTag>;
/** ID for a schematic sheet projection. */
using SheetId = EntityId<detail::SheetIdTag>;
/** ID for a placed schematic symbol instance. */
using SymbolInstanceId = EntityId<detail::SymbolInstanceIdTag>;
/** ID for a schematic wire run projection over a canonical net. */
using WireRunId = EntityId<detail::WireRunIdTag>;
/** ID for a schematic net label projection over a canonical net. */
using NetLabelId = EntityId<detail::NetLabelIdTag>;
/** ID for an explicit schematic junction projection over a canonical net. */
using JunctionId = EntityId<detail::JunctionIdTag>;
/** ID for a schematic power or ground port projection over a canonical net. */
using PowerPortId = EntityId<detail::PowerPortIdTag>;
/** ID for a schematic no-connect marker projection over a concrete pin. */
using NoConnectMarkerId = EntityId<detail::NoConnectMarkerIdTag>;
/** ID for a schematic sheet/off-page port projection over a canonical net. */
using SheetPortId = EntityId<detail::SheetPortIdTag>;
/** ID for a placed symbol field projection owned by a symbol instance. */
using SymbolFieldId = EntityId<detail::SymbolFieldIdTag>;
/** ID for a PCB board layer owned by a board projection. */
using BoardLayerId = EntityId<detail::BoardLayerIdTag>;
/** ID for a physical board feature owned by a board projection. */
using BoardFeatureId = EntityId<detail::BoardFeatureIdTag>;
/** ID for a routed PCB track owned by a board projection. */
using BoardTrackId = EntityId<detail::BoardTrackIdTag>;
/** ID for a routed PCB via owned by a board projection. */
using BoardViaId = EntityId<detail::BoardViaIdTag>;
/** ID for a PCB copper zone owned by a board projection. */
using BoardZoneId = EntityId<detail::BoardZoneIdTag>;
/** ID for a PCB keepout owned by a board projection. */
using BoardKeepoutId = EntityId<detail::BoardKeepoutIdTag>;
/** ID for a PCB room owned by a board projection. */
using BoardRoomId = EntityId<detail::BoardRoomIdTag>;
/** ID for PCB board text owned by a board projection. */
using BoardTextId = EntityId<detail::BoardTextIdTag>;
/** ID for a cached PCB footprint definition owned by a board projection. */
using FootprintDefId = EntityId<detail::FootprintDefIdTag>;
/** ID for a pad inside a PCB footprint definition. */
using FootprintPadId = EntityId<detail::FootprintPadIdTag>;
/** ID for a component placement owned by a board projection. */
using ComponentPlacementId = EntityId<detail::ComponentPlacementIdTag>;

} // namespace volt
