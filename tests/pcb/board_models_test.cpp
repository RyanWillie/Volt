#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <stdexcept>
#include <vector>

#include <volt/pcb/board_copper_model.hpp>
#include <volt/pcb/board_footprint_model.hpp>
#include <volt/pcb/board_placement_model.hpp>
#include <volt/pcb/board_structure_model.hpp>
#include <volt/pcb/footprints.hpp>

namespace {

template <typename Model>
concept CanPlaceComponent =
    requires(Model model, volt::ComponentPlacement placement) { model.place_component(placement); };

template <typename Model>
concept CanAddTrack = requires(Model model, volt::BoardTrack track) { model.add_track(track); };

template <typename Model>
concept CanAddVia = requires(Model model, volt::BoardVia via) { model.add_via(via); };

template <typename Model>
concept CanAddZone = requires(Model model, volt::BoardZone zone) { model.add_zone(zone); };

template <typename Model>
concept CanAddKeepout =
    requires(Model model, volt::BoardKeepout keepout) { model.add_keepout(keepout); };

template <typename Model>
concept CanAddText = requires(Model model, volt::BoardText text) { model.add_text(text); };

static_assert(!CanPlaceComponent<volt::BoardPlacementModel>);
static_assert(!CanAddTrack<volt::BoardCopperModel>);
static_assert(!CanAddVia<volt::BoardCopperModel>);
static_assert(!CanAddZone<volt::BoardCopperModel>);
static_assert(!CanAddKeepout<volt::BoardCopperModel>);
static_assert(!CanAddText<volt::BoardCopperModel>);

} // namespace

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
