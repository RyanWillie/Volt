#include "schematic_test_helpers.hpp"

TEST_CASE("Schematic geometry transforms symbol points by orientation") {
    const auto local = volt::Point{2.0, 3.0};
    const auto origin = volt::Point{10.0, 20.0};

    CHECK(volt::transform_schematic_point(local, origin, volt::SchematicOrientation::Right) ==
          volt::Point{12.0, 23.0});
    CHECK(volt::transform_schematic_point(local, origin, volt::SchematicOrientation::Down) ==
          volt::Point{7.0, 22.0});
    CHECK(volt::transform_schematic_point(local, origin, volt::SchematicOrientation::Left) ==
          volt::Point{8.0, 17.0});
    CHECK(volt::transform_schematic_point(local, origin, volt::SchematicOrientation::Up) ==
          volt::Point{13.0, 18.0});
}

TEST_CASE("Schematic geometry classifies segment relationships and junction semantics") {
    const auto horizontal = volt::SchematicSegment{volt::Point{0.0, 0.0}, volt::Point{10.0, 0.0}};

    const auto crossing = volt::classify_segment_relationship(
        horizontal, volt::SchematicSegment{volt::Point{5.0, -5.0}, volt::Point{5.0, 5.0}});
    CHECK(crossing == volt::SchematicSegmentRelationship::Crossing);
    CHECK_FALSE(volt::same_net_segments_join(crossing, volt::SchematicJunction::Absent));
    CHECK(volt::same_net_segments_join(crossing, volt::SchematicJunction::Present));
    CHECK_FALSE(volt::different_net_segments_collide(crossing, volt::SchematicJunction::Absent));
    CHECK(volt::different_net_segments_collide(crossing, volt::SchematicJunction::Present));

    const auto endpoint_touch = volt::classify_segment_relationship(
        horizontal, volt::SchematicSegment{volt::Point{10.0, 0.0}, volt::Point{20.0, 0.0}});
    CHECK(endpoint_touch == volt::SchematicSegmentRelationship::EndpointTouch);
    CHECK(volt::same_net_segments_join(endpoint_touch, volt::SchematicJunction::Absent));
    CHECK(volt::different_net_segments_collide(endpoint_touch, volt::SchematicJunction::Absent));

    const auto overlap = volt::classify_segment_relationship(
        horizontal, volt::SchematicSegment{volt::Point{5.0, 0.0}, volt::Point{15.0, 0.0}});
    CHECK(overlap == volt::SchematicSegmentRelationship::Overlap);
    CHECK(volt::same_net_segments_join(overlap, volt::SchematicJunction::Absent));
    CHECK(volt::different_net_segments_collide(overlap, volt::SchematicJunction::Absent));

    const auto disjoint = volt::classify_segment_relationship(
        horizontal, volt::SchematicSegment{volt::Point{0.0, 2.0}, volt::Point{10.0, 2.0}});
    CHECK(disjoint == volt::SchematicSegmentRelationship::Disjoint);
    CHECK_FALSE(volt::same_net_segments_join(disjoint, volt::SchematicJunction::Present));
    CHECK_FALSE(volt::different_net_segments_collide(disjoint, volt::SchematicJunction::Present));

    const auto repeated_point_far_away = volt::classify_segment_relationship(
        volt::SchematicSegment{volt::Point{5.0, 10.0}, volt::Point{5.0, 10.0}}, horizontal);
    CHECK(repeated_point_far_away == volt::SchematicSegmentRelationship::Disjoint);

    const auto repeated_point_on_segment = volt::classify_segment_relationship(
        volt::SchematicSegment{volt::Point{5.0, 0.0}, volt::Point{5.0, 0.0}}, horizontal);
    CHECK(repeated_point_on_segment == volt::SchematicSegmentRelationship::EndpointTouch);
}

TEST_CASE("Schematic allows same-net joins but rejects different-net wire collisions") {
    volt::Circuit circuit;
    const auto vcc = circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});
    const auto gnd = circuit.add_net(volt::NetSpec{volt::NetName{"GND"}, volt::NetKind::Ground});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    [[maybe_unused]] const auto first = schematic.add_wire_run(
        sheet, volt::WireRun{vcc, std::vector{volt::Point{0.0, 0.0}, volt::Point{10.0, 0.0}}});

    CHECK_NOTHROW(schematic.add_wire_run(
        sheet, volt::WireRun{vcc, std::vector{volt::Point{10.0, 0.0}, volt::Point{20.0, 0.0}}}));
    CHECK_NOTHROW(schematic.add_wire_run(
        sheet, volt::WireRun{vcc, std::vector{volt::Point{5.0, 0.0}, volt::Point{15.0, 0.0}}}));
    CHECK_NOTHROW(schematic.add_wire_run(
        sheet, volt::WireRun{gnd, std::vector{volt::Point{2.0, -5.0}, volt::Point{2.0, 5.0}}}));
    CHECK_THROWS_AS(schematic.add_junction(sheet, volt::Junction{vcc, volt::Point{2.0, 0.0}}),
                    std::logic_error);
    CHECK_THROWS_AS(
        schematic.add_wire_run(
            sheet, volt::WireRun{gnd, std::vector{volt::Point{10.0, 0.0}, volt::Point{20.0, 0.0}}}),
        std::logic_error);
    CHECK_THROWS_AS(
        schematic.add_wire_run(
            sheet, volt::WireRun{gnd, std::vector{volt::Point{0.0, 0.0}, volt::Point{5.0, 0.0}}}),
        std::logic_error);
    check_kernel_error(
        [&] {
            static_cast<void>(schematic.add_wire_run(
                sheet,
                volt::WireRun{gnd, std::vector{volt::Point{0.0, 0.0}, volt::Point{5.0, 0.0}}}));
        },
        volt::ErrorCode::InvalidState, "Schematic wire run collides with a different logical net");
    CHECK(schematic.wire_run_count() == 4U);
}

TEST_CASE("Schematic geometry rejects non-finite coordinates") {
    CHECK_THROWS_AS(volt::Point(0.0, std::numeric_limits<double>::infinity()),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::SymbolCircle(volt::Point{0.0, 0.0}, -1.0), std::invalid_argument);
    CHECK_THROWS_AS(
        volt::SymbolArc(volt::Point{0.0, 0.0}, 1.0, std::numeric_limits<double>::infinity(), 90.0),
        std::invalid_argument);
    CHECK_THROWS_AS(volt::SymbolText("", volt::Point{0.0, 0.0}), std::invalid_argument);
    check_kernel_error(
        [] { static_cast<void>(volt::Point(0.0, std::numeric_limits<double>::infinity())); },
        volt::ErrorCode::InvalidArgument, "Schematic point coordinates must be finite");
}
