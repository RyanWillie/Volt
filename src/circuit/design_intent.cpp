#include <volt/circuit/design_intent.hpp>

#include <algorithm>
#include <optional>
#include <utility>

namespace volt {

ComponentAssemblyIntent::ComponentAssemblyIntent(ComponentId component, std::optional<bool> dnp,
                                                 bool selection_override)
    : component_{component}, dnp_{std::move(dnp)}, selection_override_{selection_override} {}

bool DesignIntent::mark_intentional_stub_net(NetId net) {
    if (is_intentional_stub_net(net)) {
        return false;
    }

    intentional_stub_nets_.push_back(net);
    return true;
}

bool DesignIntent::mark_intentional_no_connect_pin(PinId pin) {
    if (is_intentional_no_connect_pin(pin)) {
        return false;
    }

    intentional_no_connect_pins_.push_back(pin);
    return true;
}

[[nodiscard]] bool DesignIntent::is_intentional_stub_net(NetId net) const {
    return std::find(intentional_stub_nets_.begin(), intentional_stub_nets_.end(), net) !=
           intentional_stub_nets_.end();
}

[[nodiscard]] bool DesignIntent::is_intentional_no_connect_pin(PinId pin) const {
    return std::find(intentional_no_connect_pins_.begin(), intentional_no_connect_pins_.end(),
                     pin) != intentional_no_connect_pins_.end();
}

[[nodiscard]] std::optional<bool> DesignIntent::component_dnp(ComponentId component) const {
    const auto existing =
        std::find_if(component_assembly_intents_.begin(), component_assembly_intents_.end(),
                     [component](const ComponentAssemblyIntent &intent) {
                         return intent.component() == component;
                     });
    if (existing == component_assembly_intents_.end()) {
        return std::nullopt;
    }
    return existing->dnp();
}

[[nodiscard]] bool DesignIntent::is_component_selection_override(ComponentId component) const {
    const auto existing =
        std::find_if(component_assembly_intents_.begin(), component_assembly_intents_.end(),
                     [component](const ComponentAssemblyIntent &intent) {
                         return intent.component() == component;
                     });
    return existing != component_assembly_intents_.end() && existing->selection_override();
}

[[nodiscard]] const std::vector<NetId> &DesignIntent::intentional_stub_nets() const noexcept {
    return intentional_stub_nets_;
}

[[nodiscard]] const std::vector<PinId> &DesignIntent::intentional_no_connect_pins() const noexcept {
    return intentional_no_connect_pins_;
}

[[nodiscard]] const std::vector<ComponentAssemblyIntent> &
DesignIntent::component_assembly_intents() const noexcept {
    return component_assembly_intents_;
}

void DesignIntent::set_component_dnp(ComponentId component, bool dnp) {
    const auto existing =
        std::find_if(component_assembly_intents_.begin(), component_assembly_intents_.end(),
                     [component](const ComponentAssemblyIntent &intent) {
                         return intent.component() == component;
                     });
    if (existing == component_assembly_intents_.end()) {
        component_assembly_intents_.emplace_back(component, dnp, false);
        return;
    }
    const auto selection_override = existing->selection_override();
    *existing = ComponentAssemblyIntent{component, dnp, selection_override};
}

void DesignIntent::set_component_selection_override(ComponentId component, bool override) {
    const auto existing =
        std::find_if(component_assembly_intents_.begin(), component_assembly_intents_.end(),
                     [component](const ComponentAssemblyIntent &intent) {
                         return intent.component() == component;
                     });
    if (existing == component_assembly_intents_.end()) {
        if (!override) {
            return;
        }
        component_assembly_intents_.emplace_back(component, std::nullopt, override);
        return;
    }
    const auto dnp = existing->dnp();
    if (!override && !dnp.has_value()) {
        component_assembly_intents_.erase(existing);
        return;
    }
    *existing = ComponentAssemblyIntent{component, dnp, override};
}

} // namespace volt
