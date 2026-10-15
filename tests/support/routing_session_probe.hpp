#pragma once
#include "../../src/pcb/routing/board_router_storage.hpp"

namespace volt::detail {
struct RoutingSessionProbe {
    static std::size_t builds(const BoardRouter &router) { return router.explicit_index_builds_; }

    static std::size_t pads(const BoardRouter &router) { return router.explicit_pad_resolutions_; }

    static const void *state(const BoardRouter &router) { return router.state_.get(); }

    static bool indexed(const BoardRouter &router) { return router.state_ && router.state_->index; }
};
} // namespace volt::detail
