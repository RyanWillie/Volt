#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <volt/circuit/parts/parts.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>

namespace volt {

namespace detail {
struct ElectricalState;
}

/**
 * Owns typed electrical metadata and selected physical implementation state.
 *
 * Responsibility: stores typed electrical attributes (on pins, nets, components) and the
 *   selected physical part per component, keyed by entity ID.
 * Invariants: an attribute's value matches its spec's dimension; attributes attach only to
 *   entities the root has confirmed exist.
 * Collaborators: composed by Circuit; read by ERC rules; never references Circuit back (acyclic).
 */
class ElectricalModel {
  public:
    /** Construct an empty electrical metadata facade. */
    ElectricalModel();
    /** Copy electrical metadata state. */
    ElectricalModel(const ElectricalModel &other);
    /** Move electrical metadata state. */
    ElectricalModel(ElectricalModel &&other) noexcept;
    /** Copy electrical metadata state. */
    ElectricalModel &operator=(const ElectricalModel &other);
    /** Move electrical metadata state. */
    ElectricalModel &operator=(ElectricalModel &&other) noexcept;
    /** Destroy electrical metadata state. */
    ~ElectricalModel();

    /** Return typed electrical metadata for a concrete component. */
    [[nodiscard]] const ElectricalAttributeMap &
    component_attributes(ComponentId component) const noexcept;

    /** Return typed electrical metadata for a reusable pin definition. */
    [[nodiscard]] const ElectricalAttributeMap &
    pin_definition_attributes(PinDefId pin_definition) const noexcept;

    /** Return typed electrical metadata for a logical net. */
    [[nodiscard]] const ElectricalAttributeMap &net_attributes(NetId net) const noexcept;

    /** Return the selected physical part for a concrete component, if present. */
    [[nodiscard]] const std::optional<PhysicalPart> &
    selected_physical_part(ComponentId component) const noexcept;

  protected:
    /** Construct a read-only facade over owner-private storage. */
    explicit ElectricalModel(std::shared_ptr<const detail::ElectricalState> state);

  private:
    [[nodiscard]] const detail::ElectricalState &state() const noexcept;

    template <typename Id>
    [[nodiscard]] static const ElectricalAttributeMap &
    attributes(const std::vector<std::pair<Id, ElectricalAttributeMap>> &entries,
               Id owner) noexcept;

    std::shared_ptr<const detail::ElectricalState> state_;
};

} // namespace volt
