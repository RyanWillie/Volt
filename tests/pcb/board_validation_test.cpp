#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints.hpp>

namespace {

const volt::Diagnostic *find_diagnostic(const volt::DiagnosticReport &report,
                                        const std::string &code) {
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code().value() == code) {
            return &diagnostic;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Board validation diagnoses layer stack side order conflicts") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{10.0, 10.0}));
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{back, front}, 1.6});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *order = find_diagnostic(report, "PCB_LAYER_STACK_SIDE_ORDER_CONFLICT");
    REQUIRE(order != nullptr);
    CHECK(order->severity() == volt::Severity::Error);
    CHECK(order->entities() ==
          std::vector{volt::EntityRef::board_layer(back), volt::EntityRef::board_layer(front)});
}

TEST_CASE("Board validation checks mechanical opening extents against the outline") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{10.0, 10.0}));
    const auto clipped_hole = board.add_feature(
        volt::BoardFeature::hole("MH", volt::BoardPoint{0.5, 5.0}, 2.0, false, "mounting"));

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *outside_outline = find_diagnostic(report, "PCB_BOARD_FEATURE_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    CHECK(outside_outline->entities() == std::vector{volt::EntityRef::board_feature(clipped_hole)});
}

TEST_CASE("Board validation checks cutout edges against concave outlines") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(volt::BoardOutline{std::vector{
        volt::BoardPoint{0.0, 0.0},
        volt::BoardPoint{10.0, 0.0},
        volt::BoardPoint{10.0, 4.0},
        volt::BoardPoint{4.0, 4.0},
        volt::BoardPoint{4.0, 10.0},
        volt::BoardPoint{0.0, 10.0},
    }});
    const auto clipped_cutout = board.add_feature(volt::BoardFeature::cutout(
        "CUT",
        std::vector{volt::BoardPoint{3.0, 3.0}, volt::BoardPoint{9.0, 3.0},
                    volt::BoardPoint{3.0, 9.0}},
        "access"));

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *outside_outline = find_diagnostic(report, "PCB_BOARD_FEATURE_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    CHECK(outside_outline->entities() ==
          std::vector{volt::EntityRef::board_feature(clipped_cutout)});
}

TEST_CASE("Board validation rejects zero-clearance polygon edges crossing concave outlines") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(volt::BoardOutline{std::vector{
        volt::BoardPoint{0.0, 0.0},
        volt::BoardPoint{10.0, 0.0},
        volt::BoardPoint{10.0, 10.0},
        volt::BoardPoint{8.0, 10.0},
        volt::BoardPoint{8.0, 2.0},
        volt::BoardPoint{6.0, 2.0},
        volt::BoardPoint{6.0, 10.0},
        volt::BoardPoint{4.0, 10.0},
        volt::BoardPoint{4.0, 2.0},
        volt::BoardPoint{2.0, 2.0},
        volt::BoardPoint{2.0, 10.0},
        volt::BoardPoint{0.0, 10.0},
    }});
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front}, 1.6});
    const auto zone = board.add_zone(volt::BoardZone{
        std::vector{
            volt::BoardPoint{1.0, 5.0},
            volt::BoardPoint{9.0, 5.0},
            volt::BoardPoint{9.0, 6.0},
            volt::BoardPoint{1.0, 6.0},
        },
        std::vector{front},
    });

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *outside_outline = find_diagnostic(report, "PCB_COPPER_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    CHECK(outside_outline->entities() ==
          std::vector{volt::EntityRef::board_zone(zone), volt::EntityRef::board_layer(front)});
}
