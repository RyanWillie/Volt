#pragma once

#include <optional>

#include <volt/core/ids.hpp>

namespace volt {

/** Explicit assembly intent for one component instance. */
class ComponentAssemblyIntent {
  public:
    /** Construct component assembly intent. */
    ComponentAssemblyIntent(ComponentId component, std::optional<bool> dnp, bool selection_override)
        : component_{component}, dnp_{dnp}, selection_override_{selection_override} {}

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

} // namespace volt
