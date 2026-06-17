#pragma once

#include <optional>
#include <vector>

#include <volt/core/ids.hpp>

namespace volt {

/** Explicit assembly intent for one component instance. */
class ComponentAssemblyIntent {
  public:
    /** Construct component assembly intent. */
    ComponentAssemblyIntent(ComponentId component, std::optional<bool> dnp,
                            bool selection_override);

    /** Return the component instance this intent applies to. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return explicit do-not-populate intent, when authored. */
    [[nodiscard]] const std::optional<bool> &dnp() const noexcept { return dnp_; }

    /** Return whether the selected part was intentionally overridden for this instance. */
    [[nodiscard]] bool selection_override() const noexcept { return selection_override_; }

  private:
    ComponentId component_;
    std::optional<bool> dnp_;
    bool selection_override_;
};

/**
 * Owns explicit design-intent assertions for otherwise suspicious connectivity shapes.
 *
 * Responsibility: records deliberate intent (intentional stub nets, intentional no-connect
 *   pins) so validation can distinguish a choice from a mistake.
 * Invariants: assertions reference existing nets/pins (cross-reference confirmed by the root).
 * Collaborators: composed by Circuit; read by ERC rules to suppress would-be diagnostics;
 *   never references Circuit back (acyclic).
 */
class DesignIntent {
  public:
    /** Return whether a net is intentionally left as a stub. */
    [[nodiscard]] bool is_intentional_stub_net(NetId net) const;

    /** Return whether a concrete pin is intentionally left unconnected. */
    [[nodiscard]] bool is_intentional_no_connect_pin(PinId pin) const;

    /** Return explicit component DNP intent, if one has been authored. */
    [[nodiscard]] std::optional<bool> component_dnp(ComponentId component) const;

    /** Return whether this component has a selected-part override intent marker. */
    [[nodiscard]] bool is_component_selection_override(ComponentId component) const;

    /** Return intentional stub nets in deterministic order. */
    [[nodiscard]] const std::vector<NetId> &intentional_stub_nets() const noexcept;

    /** Return intentional no-connect pins in deterministic order. */
    [[nodiscard]] const std::vector<PinId> &intentional_no_connect_pins() const noexcept;

    /** Return component assembly intent records in deterministic order. */
    [[nodiscard]] const std::vector<ComponentAssemblyIntent> &
    component_assembly_intents() const noexcept;

    /** Mark a single-pin net as intentional design intent. */
    bool mark_intentional_stub_net(NetId net);

    /** Mark an unconnected concrete pin as intentional design intent. */
    bool mark_intentional_no_connect_pin(PinId pin);

    /** Set explicit DNP intent for a component. */
    void set_component_dnp(ComponentId component, bool dnp);

    /** Set or clear selected-part override intent for a component. */
    void set_component_selection_override(ComponentId component, bool override);

  private:
    std::vector<NetId> intentional_stub_nets_;
    std::vector<PinId> intentional_no_connect_pins_;
    std::vector<ComponentAssemblyIntent> component_assembly_intents_;
};

} // namespace volt
