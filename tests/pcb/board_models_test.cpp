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
        volt::BoardFeature::hole("MH1", volt::BoardPoint{3.0, 3.0}, 3.2, false, "mounting"));

    CHECK(model.layer_count() == 2);
    CHECK(model.layer(front).name() == "F.Cu");
    REQUIRE(model.layer_stack().has_value());
    CHECK(model.layer_stack()->layers() == std::vector{front, back});
    REQUIRE(model.outline().has_value());
    CHECK(model.design_rules().minimum_track_width_mm() == 0.18);
    CHECK(model.feature(feature).kind() == volt::BoardFeatureKind::Hole);
    CHECK(model.feature(feature).role() == "mounting");

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

TEST_CASE("Board stackup stores copper weight and dielectric properties") {
    auto front = volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top};
    front.set_copper_weight_oz(1.0);
    CHECK(front.copper_weight_oz() == 1.0);
    CHECK_THROWS_AS(front.set_copper_weight_oz(0.0), std::invalid_argument);
    CHECK_THROWS_AS(front.set_copper_weight_oz(-1.0), std::invalid_argument);

    auto silk =
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top};
    CHECK_THROWS_AS(silk.set_copper_weight_oz(1.0), std::invalid_argument);

    CHECK_THROWS_AS(volt::BoardDielectric(0.0, 4.5), std::invalid_argument);
    CHECK_THROWS_AS(volt::BoardDielectric(1.0, 0.9), std::invalid_argument);
    const auto core = volt::BoardDielectric{1.51, 4.6};
    CHECK(core.thickness_mm() == 1.51);
    CHECK(core.relative_permittivity() == 4.6);
}

TEST_CASE("Board stackup requires dielectrics to match copper layer pairs") {
    auto model = volt::BoardStructureModel{};
    const auto front = model.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = model.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    const auto silk = model.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});

    model.set_layer_stack(
        volt::LayerStack{{front, silk, back}, 1.6, std::vector{volt::BoardDielectric{1.51, 4.6}}});
    REQUIRE(model.layer_stack().has_value());
    REQUIRE(model.layer_stack()->dielectrics().size() == 1);
    CHECK(model.layer_stack()->dielectrics().front().relative_permittivity() == 4.6);

    CHECK_THROWS_AS(
        model.set_layer_stack(volt::LayerStack{
            {front, back},
            1.6,
            std::vector{volt::BoardDielectric{0.7, 4.6}, volt::BoardDielectric{0.7, 4.6}}}),
        std::invalid_argument);
    CHECK_THROWS_AS(model.set_layer_stack(volt::LayerStack{
                        {front, silk}, 1.6, std::vector{volt::BoardDielectric{1.51, 4.6}}}),
                    std::invalid_argument);
}

TEST_CASE("Board design rules store a canonical clearance matrix") {
    auto rules = volt::BoardDesignRules{0.15, 0.15, 0.20, 0.45, 0.10};

    CHECK(rules.clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Pad) ==
          0.15);
    CHECK(rules.clearance_mm(volt::BoardClearanceKind::Via, volt::BoardClearanceKind::BoardEdge) ==
          0.10);

    rules.set_clearance_mm(volt::BoardClearanceKind::Pad, volt::BoardClearanceKind::Track, 0.30);
    rules.set_clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Track, 0.20);
    rules.set_clearance_mm(volt::BoardClearanceKind::Zone, volt::BoardClearanceKind::BoardEdge,
                           0.50);

    CHECK(rules.clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Pad) ==
          0.30);
    CHECK(rules.clearance_mm(volt::BoardClearanceKind::Pad, volt::BoardClearanceKind::Track) ==
          0.30);
    CHECK(rules.clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Track) ==
          0.20);
    CHECK(rules.clearance_mm(volt::BoardClearanceKind::BoardEdge, volt::BoardClearanceKind::Zone) ==
          0.50);
    CHECK(rules.clearance_mm(volt::BoardClearanceKind::Via, volt::BoardClearanceKind::Zone) ==
          0.15);

    rules.set_clearance_mm(volt::BoardClearanceKind::Pad, volt::BoardClearanceKind::Track, 0.35);
    REQUIRE(rules.clearance_matrix().size() == 3);
    CHECK(rules.clearance_matrix()[0].first == volt::BoardClearanceKind::Track);
    CHECK(rules.clearance_matrix()[0].second == volt::BoardClearanceKind::Track);
    CHECK(rules.clearance_matrix()[1].first == volt::BoardClearanceKind::Track);
    CHECK(rules.clearance_matrix()[1].second == volt::BoardClearanceKind::Pad);
    CHECK(rules.clearance_matrix()[1].clearance_mm == 0.35);
    CHECK(rules.clearance_matrix()[2].second == volt::BoardClearanceKind::BoardEdge);

    CHECK_THROWS_AS(rules.set_clearance_mm(volt::BoardClearanceKind::BoardEdge,
                                           volt::BoardClearanceKind::BoardEdge, 0.2),
                    std::invalid_argument);
    CHECK_THROWS_AS(rules.set_clearance_mm(volt::BoardClearanceKind::Track,
                                           volt::BoardClearanceKind::Pad, -0.1),
                    std::invalid_argument);
}
