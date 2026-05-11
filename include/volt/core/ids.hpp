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
struct SymbolDefIdTag;
struct SheetIdTag;
struct SymbolInstanceIdTag;
struct WireRunIdTag;
struct NetLabelIdTag;

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

} // namespace volt
