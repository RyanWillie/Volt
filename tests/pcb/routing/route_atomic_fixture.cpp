#include <volt/io/pcb/pcb_writer.hpp>
#include <volt/pcb/routing/board_router.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace {
thread_local std::size_t remaining = std::numeric_limits<std::size_t>::max();
thread_local bool recording = false;
thread_local std::size_t allocated_bytes = 0;

void *allocate(std::size_t size) {
    if (recording)
        allocated_bytes += size;
    if (remaining != std::numeric_limits<std::size_t>::max()) {
        if (remaining == 0)
            throw std::bad_alloc{};
        --remaining;
    }
    if (auto *memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc{};
}

void disable() { remaining = std::numeric_limits<std::size_t>::max(); }

struct Fixture {
    volt::Circuit circuit;
    volt::NetId net = circuit.add_net(volt::NetSpec{volt::NetName{"SIG"}, volt::NetKind::Signal});
    volt::Board board{circuit};
    volt::BoardLayerId front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    volt::BoardLayerId back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    volt::FootprintLibrary footprints;

    Fixture() {
        board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
        static_cast<void>(board.add_track(volt::BoardTrack{net, front, {{1, 1}, {2, 1}}, 0.2}));
        static_cast<void>(board.add_via(volt::BoardVia{net, {3, 3}, front, back, 0.3, 0.7}));
    }

    volt::ResolvedBoardView view() const {
        return volt::ResolvedBoardView::test_only(board, footprints, {});
    }
};

int exercise(bool batch) {
    std::size_t failures = 0;
    for (std::size_t budget = 0; budget < 10000; ++budget) {
        Fixture f;
        const auto bytes = volt::io::write_pcb_board(f.view());
        const auto tracks = f.board.all<volt::BoardTrackId>();
        const auto vias = f.board.all<volt::BoardViaId>();
        const auto *old_track = &f.board.get(volt::BoardTrackId{0});
        const auto *old_via = &f.board.get(volt::BoardViaId{0});
        const auto append =
            volt::CopperAppendSpec{{volt::BoardTrack{f.net, f.front, {{10, 10}, {15, 10}}, 0.2},
                                    volt::BoardVia{f.net, {15, 10}, f.front, f.back, 0.3, 0.7},
                                    volt::BoardTrack{f.net, f.back, {{15, 10}, {20, 10}}, 0.2}}};
        const auto spec = volt::RouteSpec{&f.board,
                                          volt::RouteNetRef{&f.circuit, f.net},
                                          volt::RoutePoint{{10, 10}},
                                          {&f.board, f.front},
                                          {volt::RouteThrough{volt::RoutePoint{{15, 10}}},
                                           volt::RouteVia{{&f.board, f.back}},
                                           volt::RouteTo{volt::RoutePoint{{20, 10}}}}};
        auto router = volt::BoardRouter{f.board, f.view()};
        try {
            remaining = budget;
            if (batch) {
                const auto result = f.board.append_copper(append);
                disable();
                if (result.order.size() != 3)
                    return 2;
            } else {
                const auto result = router.route(spec);
                disable();
                if (!result.committed || result.committed_order.size() != 3)
                    return 3;
            }
            if (!tracks.advanced_once() || f.board.all<volt::BoardTrackId>().size() != 3 ||
                f.board.all<volt::BoardViaId>().size() != 2)
                return 4;
            if (failures < (batch ? 8U : 30U))
                return 5;
            std::cout << (batch ? "batch" : "route")
                      << " preparation allocation failures checked: " << failures << '\n';
            return 0;
        } catch (const std::bad_alloc &) {
            disable();
            ++failures;
            if (volt::io::write_pcb_board(f.view()) != bytes || !tracks.is_current() ||
                !vias.is_current() || &f.board.get(volt::BoardTrackId{0}) != old_track ||
                &f.board.get(volt::BoardViaId{0}) != old_via)
                return 6;
        }
    }
    return 7;
}

int record_copy_cost() {
    std::size_t previous = 0;
    for (const std::size_t count : {0U, 100U, 1000U}) {
        Fixture f;
        for (std::size_t i = 0; i < count; ++i) {
            static_cast<void>(
                f.board.add_track(volt::BoardTrack{f.net, f.front, {{5, 5}, {6, 5}}, 0.2}));
            if (i % 10U == 0)
                static_cast<void>(
                    f.board.add_via(volt::BoardVia{f.net, {7, 7}, f.front, f.back, 0.3, 0.7}));
        }
        const auto spec =
            volt::CopperAppendSpec{{volt::BoardTrack{f.net, f.front, {{10, 10}, {11, 10}}, 0.2}}};
        allocated_bytes = 0;
        recording = true;
        const auto started = std::chrono::steady_clock::now();
        const auto result = f.board.append_copper(spec);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        recording = false;
        if (result.order.size() != 1 || (count != 0 && allocated_bytes <= previous))
            return 8;
        previous = allocated_bytes;
        std::cout << "staged append existing_extra_tracks=" << count
                  << " allocated_bytes=" << allocated_bytes << " elapsed_us="
                  << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << '\n';
    }
    return 0;
}
} // namespace

void *operator new(std::size_t size) { return allocate(size); }

void *operator new[](std::size_t size) { return allocate(size); }

void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
    try {
        return allocate(size);
    } catch (const std::bad_alloc &) {
        return nullptr;
    }
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    return ::operator new(size, std::nothrow);
}

void operator delete(void *pointer) noexcept { std::free(pointer); }

void operator delete[](void *pointer) noexcept { std::free(pointer); }

void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }

void operator delete[](void *pointer, std::size_t) noexcept { std::free(pointer); }

void operator delete(void *pointer, const std::nothrow_t &) noexcept { std::free(pointer); }

void operator delete[](void *pointer, const std::nothrow_t &) noexcept { std::free(pointer); }

int main() {
    if (const auto result = exercise(true))
        return result;
    if (const auto result = exercise(false))
        return result;
    return record_copy_cost();
}
