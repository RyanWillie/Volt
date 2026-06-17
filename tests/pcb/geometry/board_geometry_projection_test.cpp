#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

#include <volt/pcb/board.hpp>
#include <volt/pcb/projection/board_geometry_projection.hpp>

TEST_CASE("Board geometry projection derives bare-board 3D geometry from board state") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    static_cast<void>(board.add_feature(
        volt::BoardFeature::hole("MH", volt::BoardPoint{3.0, 3.0}, 3.2, false, "mounting")));
    static_cast<void>(board.add_feature(volt::BoardFeature::slot(
        "SLOT", volt::BoardPoint{8.0, 4.0}, volt::BoardPoint{16.0, 4.0}, 1.5, true, "mounting")));
    static_cast<void>(board.add_feature(volt::BoardFeature::cutout(
        "CUT",
        std::vector{volt::BoardPoint{20.0, 4.0}, volt::BoardPoint{25.0, 4.0},
                    volt::BoardPoint{25.0, 9.0}, volt::BoardPoint{20.0, 9.0}},
        "access")));
    static_cast<void>(board.add_feature(volt::BoardFeature::circle(
        "FID", volt::BoardPoint{12.0, 12.0}, 1.0, volt::BoardSide::Top, "fiducial")));

    const auto geometry = volt::project_board_geometry(board);

    CHECK(geometry.units == volt::BoardUnits::Millimeters);
    REQUIRE(geometry.thickness_mm.has_value());
    CHECK(geometry.thickness_mm.value() == 1.6);
    REQUIRE(geometry.outline.has_value());
    CHECK(geometry.outline->at(2) == volt::BoardPoint{30.0, 20.0});

    REQUIRE(geometry.stackup.size() == 2);
    CHECK(geometry.stackup[0].layer == front);
    CHECK(geometry.stackup[0].name == "F.Cu");
    CHECK(geometry.stackup[0].z_mm == 0.8);
    CHECK(geometry.stackup[1].layer == back);
    CHECK(geometry.stackup[1].z_mm == -0.8);

    REQUIRE(geometry.openings.size() == 2);
    REQUIRE(std::holds_alternative<volt::BoardGeometryHoleOpening>(geometry.openings[0].shape));
    CHECK(std::get<volt::BoardGeometryHoleOpening>(geometry.openings[0].shape).drill_diameter_mm ==
          3.2);
    REQUIRE(std::holds_alternative<volt::BoardGeometrySlotOpening>(geometry.openings[1].shape));
    CHECK(std::get<volt::BoardGeometrySlotOpening>(geometry.openings[1].shape).plated);

    REQUIRE(geometry.cutouts.size() == 1);
    CHECK(geometry.cutouts[0].outline[2] == volt::BoardPoint{25.0, 9.0});

    REQUIRE(geometry.surface_features.size() == 1);
    CHECK(geometry.surface_features[0].kind == volt::BoardFeatureKind::Circle);
    CHECK(geometry.surface_features[0].side == volt::BoardSide::Top);
}
