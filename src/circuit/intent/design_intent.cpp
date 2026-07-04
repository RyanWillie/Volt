#include <volt/circuit/intent/design_intent.hpp>

#include "../circuit_storage.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace volt {

ComponentAssemblyIntent::ComponentAssemblyIntent(ComponentId component, std::optional<bool> dnp,
                                                 bool selection_override)
    : component_{component}, dnp_{dnp}, selection_override_{selection_override} {}

DesignIntent::DesignIntent() : DesignIntent{std::make_shared<detail::DesignIntentState>()} {}

DesignIntent::DesignIntent(std::shared_ptr<const detail::DesignIntentState> state)
    : state_{std::move(state)} {}

DesignIntent::DesignIntent(const DesignIntent &other)
    : DesignIntent{std::make_shared<detail::DesignIntentState>(other.state())} {}

DesignIntent::DesignIntent(DesignIntent &&other) noexcept = default;

DesignIntent &DesignIntent::operator=(const DesignIntent &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::DesignIntentState>(other.state());
    }
    return *this;
}

DesignIntent &DesignIntent::operator=(DesignIntent &&other) noexcept = default;

DesignIntent::~DesignIntent() = default;

bool Circuit::DesignIntentStorage::mark_intentional_stub_net(NetId net) {
    if (is_intentional_stub_net(net)) {
        return false;
    }

    mutable_state().intentional_stub_nets.push_back(net);
    return true;
}

bool Circuit::DesignIntentStorage::mark_intentional_no_connect_pin(PinId pin) {
    if (is_intentional_no_connect_pin(pin)) {
        return false;
    }

    mutable_state().intentional_no_connect_pins.push_back(pin);
    return true;
}

[[nodiscard]] bool DesignIntent::is_intentional_stub_net(NetId net) const {
    return std::find(state().intentional_stub_nets.begin(), state().intentional_stub_nets.end(),
                     net) != state().intentional_stub_nets.end();
}

[[nodiscard]] bool DesignIntent::is_intentional_no_connect_pin(PinId pin) const {
    return std::find(state().intentional_no_connect_pins.begin(),
                     state().intentional_no_connect_pins.end(),
                     pin) != state().intentional_no_connect_pins.end();
}

[[nodiscard]] std::optional<bool> DesignIntent::component_dnp(ComponentId component) const {
    const auto existing = std::find_if(state().component_assembly_intents.begin(),
                                       state().component_assembly_intents.end(),
                                       [component](const ComponentAssemblyIntent &intent) {
                                           return intent.component() == component;
                                       });
    if (existing == state().component_assembly_intents.end()) {
        return std::nullopt;
    }
    return existing->dnp();
}

[[nodiscard]] bool DesignIntent::is_component_selection_override(ComponentId component) const {
    const auto existing = std::find_if(state().component_assembly_intents.begin(),
                                       state().component_assembly_intents.end(),
                                       [component](const ComponentAssemblyIntent &intent) {
                                           return intent.component() == component;
                                       });
    return existing != state().component_assembly_intents.end() && existing->selection_override();
}

[[nodiscard]] const std::vector<NetId> &DesignIntent::intentional_stub_nets() const noexcept {
    return state().intentional_stub_nets;
}

[[nodiscard]] const std::vector<PinId> &DesignIntent::intentional_no_connect_pins() const noexcept {
    return state().intentional_no_connect_pins;
}

[[nodiscard]] const std::vector<ComponentAssemblyIntent> &
DesignIntent::component_assembly_intents() const noexcept {
    return state().component_assembly_intents;
}

void Circuit::DesignIntentStorage::set_component_dnp(ComponentId component, bool dnp) {
    const auto existing = std::find_if(mutable_state().component_assembly_intents.begin(),
                                       mutable_state().component_assembly_intents.end(),
                                       [component](const ComponentAssemblyIntent &intent) {
                                           return intent.component() == component;
                                       });
    if (existing == mutable_state().component_assembly_intents.end()) {
        mutable_state().component_assembly_intents.emplace_back(component, dnp, false);
        return;
    }
    const auto selection_override = existing->selection_override();
    *existing = ComponentAssemblyIntent{component, dnp, selection_override};
}

void Circuit::DesignIntentStorage::set_component_selection_override(ComponentId component,
                                                                    bool override) {
    const auto existing = std::find_if(mutable_state().component_assembly_intents.begin(),
                                       mutable_state().component_assembly_intents.end(),
                                       [component](const ComponentAssemblyIntent &intent) {
                                           return intent.component() == component;
                                       });
    if (existing == mutable_state().component_assembly_intents.end()) {
        if (!override) {
            return;
        }
        mutable_state().component_assembly_intents.emplace_back(component, std::nullopt, override);
        return;
    }
    const auto dnp = existing->dnp();
    if (!override && !dnp.has_value()) {
        mutable_state().component_assembly_intents.erase(existing);
        return;
    }
    *existing = ComponentAssemblyIntent{component, dnp, override};
}

[[nodiscard]] const detail::DesignIntentState &DesignIntent::state() const noexcept {
    return *state_;
}

} // namespace volt
