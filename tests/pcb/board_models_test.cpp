#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/pcb/board_copper_model.hpp>
#include <volt/pcb/board_footprint_model.hpp>
#include <volt/pcb/board_placement_model.hpp>
#include <volt/pcb/board_structure_model.hpp>
#include <volt/pcb/footprints.hpp>

TEST_CASE("BoardStructureModel owns layers, stack, outline, rules, and features") {
    auto model = volt::BoardStructureModel{};
    const auto front = model.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = model.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    model.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    model.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{40.0, 20.0}));
    model.set_design_rules(volt::BoardDesignRules{0.20, 0.18, 0.30, 0.70, 0.10});
    const auto feature = model.add_feature(
        volt::BoardFeature::mounting_hole("MH1", volt::BoardPoint{3.0, 3.0}, 3.2));

    CHECK(model.layer_count() == 2);
    CHECK(model.layer(front).name() == "F.Cu");
    REQUIRE(model.layer_stack().has_value());
    CHECK(model.layer_stack()->layers() == std::vector{front, back});
    REQUIRE(model.outline().has_value());
    CHECK(model.design_rules().minimum_track_width_mm() == 0.18);
    CHECK(model.feature(feature).kind() == volt::BoardFeatureKind::MountingHole);

    CHECK_THROWS_AS(model.add_layer(volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper,
                                                     volt::BoardLayerSide::Top}),
                    std::logic_error);
    CHECK_THROWS_AS(model.set_layer_stack(volt::LayerStack{{front, volt::BoardLayerId{99}}, 1.6}),
                    std::out_of_range);
}

TEST_CASE("BoardFootprintModel dedupes identical cached definitions and rejects conflicts") {
    auto model = volt::BoardFootprintModel{};
    const auto first = model.cache_footprint_definition(volt::passive_0603_footprint());

    CHECK(model.cache_footprint_definition(volt::passive_0603_footprint()) == first);
    CHECK(model.footprint_definition(first) == volt::passive_0603_footprint());
    CHECK(model.footprint_definition_count() == 1);
    CHECK(model.footprint_definition_id(volt::FootprintRef{"passives", "R_0603_1608Metric"}) ==
          first);

    auto conflict = volt::FootprintDefinition{
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{0.5, 0.5}, volt::FootprintLayerSet::front_smd())},
    };
    CHECK_THROWS_AS(model.cache_footprint_definition(std::move(conflict)), std::logic_error);
}

TEST_CASE("BoardPlacementModel owns one placement per logical component") {
    auto model = volt::BoardPlacementModel{};
    const auto placement = model.place_component(volt::ComponentPlacement{
        volt::ComponentId{2}, volt::BoardPoint{10.0, 5.0}, volt::BoardRotation::degrees(90.0)});

    CHECK(model.placement_count() == 1);
    CHECK(model.placement(placement).component() == volt::ComponentId{2});
    CHECK(model.placement_for_component(volt::ComponentId{2}) == placement);
    CHECK_FALSE(model.placement_for_component(volt::ComponentId{3}).has_value());
    CHECK_THROWS_AS(
        model.place_component(volt::ComponentPlacement{
            volt::ComponentId{2}, volt::BoardPoint{12.0, 5.0}, volt::BoardRotation::degrees(0.0)}),
        std::logic_error);
}

TEST_CASE("BoardCopperModel owns routed and presentation board primitives") {
    auto model = volt::BoardCopperModel{};
    const auto front = volt::BoardLayerId{0};
    const auto back = volt::BoardLayerId{1};
    const auto net = volt::NetId{3};
    const auto track = model.add_track(volt::BoardTrack{
        net, front, std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{5.0, 1.0}}, 0.25});
    const auto via =
        model.add_via(volt::BoardVia{net, volt::BoardPoint{5.0, 1.0}, front, back, 0.30, 0.70});
    const auto zone = model.add_zone(
        volt::BoardZone{std::vector{volt::BoardPoint{0.0, 0.0}, volt::BoardPoint{4.0, 0.0},
                                    volt::BoardPoint{4.0, 4.0}, volt::BoardPoint{0.0, 4.0}},
                        std::vector{front}, net});
    const auto keepout = model.add_keepout(
        volt::BoardKeepout{std::vector{volt::BoardPoint{6.0, 0.0}, volt::BoardPoint{8.0, 0.0},
                                       volt::BoardPoint{8.0, 2.0}, volt::BoardPoint{6.0, 2.0}},
                           std::vector{front}, std::vector{volt::BoardKeepoutRestriction::Copper}});
    const auto text = model.add_text(volt::BoardText{"REV A", volt::BoardPoint{1.0, 6.0},
                                                     volt::BoardRotation::degrees(0.0), back, 1.0});

    CHECK(model.track(track).net() == net);
    CHECK(model.via(via).end_layer() == back);
    CHECK(model.zone(zone).layers() == std::vector{front});
    CHECK(model.keepout(keepout).restrictions() ==
          std::vector{volt::BoardKeepoutRestriction::Copper});
    CHECK(model.text(text).text() == "REV A");
    CHECK(model.track_count() == 1);
    CHECK(model.via_count() == 1);
    CHECK(model.zone_count() == 1);
    CHECK(model.keepout_count() == 1);
    CHECK(model.text_count() == 1);
}
