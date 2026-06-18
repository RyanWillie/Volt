#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <volt/core/ids.hpp>

namespace volt {

namespace detail {
struct DesignIntentState;
}

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
    /** Construct empty design-intent storage. */
    DesignIntent();
    /** Copy design-intent state. */
    DesignIntent(const DesignIntent &other);
    /** Move design-intent state. */
    DesignIntent(DesignIntent &&other) noexcept;
    /** Copy design-intent state. */
    DesignIntent &operator=(const DesignIntent &other);
    /** Move design-intent state. */
    DesignIntent &operator=(DesignIntent &&other) noexcept;
    /** Destroy design-intent state. */
    ~DesignIntent();

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

  protected:
    /** Construct a read-only facade over owner-private storage. */
    explicit DesignIntent(std::shared_ptr<const detail::DesignIntentState> state);

  private:
    [[nodiscard]] const detail::DesignIntentState &state() const noexcept;

    std::shared_ptr<const detail::DesignIntentState> state_;
};

} // namespace volt
