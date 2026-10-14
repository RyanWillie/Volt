#include "board_router_storage.hpp"

#include <volt/pcb/queries/board_queries.hpp>

namespace volt::detail {

BoardRoutingInputs::BoardRoutingInputs(const Board &board)
    : geometry{board.all<BoardLayerId>()},
      definitions{board.circuit().all<ComponentDefId>().size()},
      net_classes{board.circuit().all<NetClassId>().size()},
      port_bindings{board.circuit().all<PortBindingId>().size()},
      module_instances{board.circuit().all<ModuleInstanceId>().size()} {
    const auto &circuit = board.circuit();
    for (const auto &component : circuit.all<ComponentId>())
        selected.push_back(component.selected_library_part_ref());
    for (const auto &net : circuit.all<NetId>())
        classes.push_back(net.net_class());
    for (std::size_t i = 0; i < circuit.all<PinId>().size(); ++i)
        memberships.push_back(circuit.net_of(PinId{i}));
}

bool BoardRoutingInputs::current(const Board &board) const {
    const auto &circuit = board.circuit();
    if (!geometry.is_current() || definitions != circuit.all<ComponentDefId>().size() ||
        net_classes != circuit.all<NetClassId>().size() ||
        port_bindings != circuit.all<PortBindingId>().size() ||
        module_instances != circuit.all<ModuleInstanceId>().size() ||
        selected.size() != circuit.all<ComponentId>().size() ||
        classes.size() != circuit.all<NetId>().size() ||
        memberships.size() != circuit.all<PinId>().size())
        return false;
    for (std::size_t i = 0; i < selected.size(); ++i)
        if (selected[i] != circuit.get(ComponentId{i}).selected_library_part_ref())
            return false;
    for (std::size_t i = 0; i < classes.size(); ++i)
        if (classes[i] != circuit.get(NetId{i}).net_class())
            return false;
    for (std::size_t i = 0; i < memberships.size(); ++i)
        if (memberships[i] != circuit.net_of(PinId{i}))
            return false;
    return true;
}

} // namespace volt::detail

namespace volt {

BoardRouter::~BoardRouter() = default;

std::vector<PadResolution> BoardRouter::resolve_pads() const {
    if (state_ && state_->inputs.current(*board_))
        return state_->pads;
    auto inputs = detail::BoardRoutingInputs{*board_};
    auto pads = queries::resolve_pads(resolved_);
    auto prepared = std::make_unique<detail::BoardRoutingState>(
        detail::BoardRoutingState{std::move(inputs), std::move(pads), std::nullopt});
    auto result = prepared->pads;
    state_ = std::move(prepared);
    ++explicit_pad_resolutions_;
    return result;
}

std::unique_ptr<detail::BoardRoutingState> BoardRouter::prepare_state() const {
    auto prepared = std::unique_ptr<detail::BoardRoutingState>{};
    if (state_ && state_->inputs.current(*board_)) {
        prepared = std::make_unique<detail::BoardRoutingState>(*state_);
    } else {
        auto inputs = detail::BoardRoutingInputs{*board_};
        auto pads = queries::resolve_pads(resolved_);
        prepared = std::make_unique<detail::BoardRoutingState>(
            detail::BoardRoutingState{std::move(inputs), std::move(pads), std::nullopt});
        ++explicit_pad_resolutions_;
    }
    return prepared;
}

} // namespace volt
