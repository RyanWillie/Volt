#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/errors.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/copper/board_copper_model.hpp>
#include <volt/pcb/footprints/board_footprint_model.hpp>
#include <volt/pcb/footprints/footprints.hpp>
#include <volt/pcb/placement/board_placement_model.hpp>
#include <volt/pcb/queries/board_queries.hpp>
#include <volt/pcb/routing/board_spatial_index.hpp>
#include <volt/pcb/structure/board_structure_model.hpp>

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
concept CanAddRoom = requires(Model model, volt::BoardRoom room) { model.add_room(room); };

template <typename Model>
concept CanAddText = requires(Model model, volt::BoardText text) { model.add_text(text); };

template <typename Model>
concept CanInsertAfterBoardMutation =
    requires(Model &model, volt::BoardSpatialQueryShape shape, std::size_t mutation_count) {
        model.insert_after_board_mutation(shape, mutation_count);
    };

static_assert(!CanPlaceComponent<volt::BoardPlacementModel>);
static_assert(!CanAddTrack<volt::BoardCopperModel>);
static_assert(!CanAddVia<volt::BoardCopperModel>);
static_assert(!CanAddZone<volt::BoardCopperModel>);
static_assert(!CanAddKeepout<volt::BoardCopperModel>);
static_assert(!CanAddRoom<volt::BoardCopperModel>);
static_assert(!CanAddText<volt::BoardCopperModel>);
static_assert(!CanInsertAfterBoardMutation<volt::BoardSpatialIndex>);

} // namespace

TEST_CASE("BoardStructureModel owns layers, stack, outline, rules, and features") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{40.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.20, 0.18, 0.30, 0.70, 0.10});
    const auto feature = board.add_feature(
        volt::BoardFeature::hole("MH1", volt::BoardPoint{3.0, 3.0}, 3.2, false, "mounting"));

    CHECK(board.layer_count() == 2);
    CHECK(board.layer(front).name() == "F.Cu");
    REQUIRE(board.layer_stack().has_value());
    CHECK(board.layer_stack()->layers() == std::vector{front, back});
    REQUIRE(board.outline().has_value());
    CHECK(board.design_rules().minimum_track_width_mm() == 0.18);
    CHECK(board.feature(feature).kind() == volt::BoardFeatureKind::Hole);
    CHECK(board.feature(feature).role() == "mounting");

    CHECK_THROWS_AS(board.add_layer(volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper,
                                                     volt::BoardLayerSide::Top}),
                    std::logic_error);
    CHECK_THROWS_AS(board.set_layer_stack(volt::LayerStack{{front, volt::BoardLayerId{99}}, 1.6}),
                    std::out_of_range);
}

TEST_CASE("PCB board model structural rejections carry machine-readable error codes") {
    auto circuit = volt::Circuit{};
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"N1"}, volt::NetKind::Signal});
    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto silk = board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});

    try {
        [[maybe_unused]] const auto duplicate = board.add_layer(
            volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
        FAIL("Duplicate board layer names must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::DuplicateName);
        CHECK(std::string{error.what()} == "Board layer name already exists");
    }

    try {
        board.set_layer_stack(volt::LayerStack{{front, volt::BoardLayerId{99}}, 1.6});
        FAIL("Unknown board layer IDs must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::UnknownEntity);
        CHECK(std::string{error.what()} == "Board layer ID does not belong to this board");
        REQUIRE(error.entity().has_value());
        CHECK(error.entity()->kind() == volt::EntityKind::BoardLayer);
        CHECK(error.entity()->index() == 99);
    }

    try {
        [[maybe_unused]] const auto track = board.add_track(volt::BoardTrack{
            net, silk, std::vector{volt::BoardPoint{0.0, 0.0}, volt::BoardPoint{1.0, 0.0}}, 0.20});
        FAIL("Non-copper board layers must not accept copper primitives");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::CrossReferenceViolation);
        CHECK(std::string{error.what()} == "Board copper primitives require copper layers");
        REQUIRE(error.entity().has_value());
        CHECK(error.entity()->kind() == volt::EntityKind::BoardLayer);
        CHECK(error.entity()->index() == silk.index());
    }
}

TEST_CASE("BoardFootprintModel dedupes identical cached definitions and rejects conflicts") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    const auto first = board.cache_footprint_definition(volt::passive_0603_footprint());

    CHECK(board.cache_footprint_definition(volt::passive_0603_footprint()) == first);
    CHECK(board.footprint_definition(first) == volt::passive_0603_footprint());
    CHECK(board.footprint_definition_count() == 1);
    CHECK(volt::queries::footprint_definition_id(
              board, volt::FootprintRef{"passives", "R_0603_1608Metric"}) == first);

    auto conflict = volt::FootprintDefinition{
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{0.5, 0.5}, volt::FootprintLayerSet::front_smd())},
    };
    CHECK_THROWS_AS(board.cache_footprint_definition(std::move(conflict)), std::logic_error);
}

TEST_CASE("BoardFootprintModel treats footprint courtyard and body geometry as cache identity") {
    const auto pad = volt::FootprintPad::surface_mount(
        "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
        volt::FootprintSize{0.5, 0.5}, volt::FootprintLayerSet::front_smd());
    const auto body = volt::FootprintPolygon{std::vector{
        volt::FootprintPoint{-0.4, -0.3},
        volt::FootprintPoint{0.4, -0.3},
        volt::FootprintPoint{0.4, 0.3},
        volt::FootprintPoint{-0.4, 0.3},
    }};
    const auto larger_body = volt::FootprintPolygon{std::vector{
        volt::FootprintPoint{-0.5, -0.3},
        volt::FootprintPoint{0.5, -0.3},
        volt::FootprintPoint{0.5, 0.3},
        volt::FootprintPoint{-0.5, 0.3},
    }};

    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    const auto footprint = volt::FootprintDefinition{volt::FootprintRef{"test", "GeometryIdentity"},
                                                     std::vector{pad}, std::nullopt, body};
    const auto id = board.cache_footprint_definition(footprint);

    CHECK(board.cache_footprint_definition(footprint) == id);

    auto conflict = volt::FootprintDefinition{volt::FootprintRef{"test", "GeometryIdentity"},
                                              std::vector{pad}, std::nullopt, larger_body};
    CHECK_THROWS_AS(board.cache_footprint_definition(std::move(conflict)), std::logic_error);
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
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    const auto silk = board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});

    board.set_layer_stack(
        volt::LayerStack{{front, silk, back}, 1.6, std::vector{volt::BoardDielectric{1.51, 4.6}}});
    REQUIRE(board.layer_stack().has_value());
    REQUIRE(board.layer_stack()->dielectrics().size() == 1);
    CHECK(board.layer_stack()->dielectrics().front().relative_permittivity() == 4.6);

    CHECK_THROWS_AS(
        board.set_layer_stack(volt::LayerStack{
            {front, back},
            1.6,
            std::vector{volt::BoardDielectric{0.7, 4.6}, volt::BoardDielectric{0.7, 4.6}}}),
        std::invalid_argument);
    CHECK_THROWS_AS(board.set_layer_stack(volt::LayerStack{
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

TEST_CASE("BoardCapabilityProfile validates ingestible manufacturing limits") {
    const auto provenance =
        volt::BoardCapabilityProvenance{"Example fab published capability table", "2026-06-11"};
    const auto profile = volt::BoardCapabilityProfile{
        "Example Fab 2-layer",
        provenance,
        0.20,
        0.30,
        0.60,
        std::vector{
            volt::BoardClearancePair{volt::BoardClearanceKind::Pad, volt::BoardClearanceKind::Track,
                                     0.20},
            volt::BoardClearancePair{volt::BoardClearanceKind::Track,
                                     volt::BoardClearanceKind::BoardEdge, 0.30},
        },
        std::vector{
            volt::BoardCapabilityCopperWeightRefinement{1.0, 0.20, 0.20},
            volt::BoardCapabilityCopperWeightRefinement{2.0, 0.30, 0.28},
        },
        std::vector{2},
        volt::BoardCapabilityRange{0.4, 2.0},
        std::vector{1.0, 2.0, 2.5},
        volt::BoardCapabilityRange{0.15, 6.3}};

    CHECK(profile.name() == "Example Fab 2-layer");
    CHECK(profile.provenance().source == "Example fab published capability table");
    CHECK(profile.provenance().as_of == "2026-06-11");
    CHECK(profile.minimum_track_width_mm() == 0.20);
    CHECK(profile.minimum_via_drill_mm() == 0.30);
    CHECK(profile.minimum_via_annular_mm() == 0.60);
    CHECK(profile.minimum_clearance_mm(volt::BoardClearanceKind::Track,
                                       volt::BoardClearanceKind::Pad) == 0.20);
    CHECK(profile.minimum_clearance_mm(volt::BoardClearanceKind::Pad,
                                       volt::BoardClearanceKind::Track) == 0.20);
    REQUIRE(profile.minimum_clearances().size() == 2);
    CHECK(profile.minimum_clearances()[0].first == volt::BoardClearanceKind::Track);
    CHECK(profile.minimum_clearances()[0].second == volt::BoardClearanceKind::Pad);
    REQUIRE(profile.copper_weight_refinements().size() == 2);
    CHECK(profile.copper_weight_refinements()[1].copper_weight_oz == 2.0);
    CHECK(profile.supported_copper_layer_counts() == std::vector<int>{2});
    REQUIRE(profile.board_thickness_range_mm().has_value());
    CHECK(profile.board_thickness_range_mm()->minimum_mm == 0.4);
    CHECK(profile.board_thickness_range_mm()->maximum_mm == 2.0);
    CHECK(profile.available_copper_weights_oz() == std::vector<double>{1.0, 2.0, 2.5});
    REQUIRE(profile.drill_diameter_range_mm().has_value());
    CHECK(profile.drill_diameter_range_mm()->minimum_mm == 0.15);
    CHECK(profile.drill_diameter_range_mm()->maximum_mm == 6.3);

    CHECK_THROWS_AS((volt::BoardCapabilityProfile{"", provenance, 0.20, 0.30, 0.60, {}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        (volt::BoardCapabilityProfile{
            "Example", volt::BoardCapabilityProvenance{"", "2026-06-11"}, 0.20, 0.30, 0.60, {}}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (volt::BoardCapabilityProfile{"Example",
                                      volt::BoardCapabilityProvenance{"Example source", ""},
                                      0.20,
                                      0.30,
                                      0.60,
                                      {}}),
        std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardCapabilityProfile{"Example", provenance, 0.0, 0.30, 0.60, {}}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardCapabilityProfile{"Example", provenance, 0.20, 0.30, 0.30, {}}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardCapabilityProfile{
                        "Example", provenance, 0.20, 0.30, 0.60,
                        std::vector{
                            volt::BoardClearancePair{volt::BoardClearanceKind::Track,
                                                     volt::BoardClearanceKind::Pad, 0.20},
                            volt::BoardClearancePair{volt::BoardClearanceKind::Pad,
                                                     volt::BoardClearanceKind::Track, 0.25},
                        }}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardCapabilityProfile{
                        "Example",
                        provenance,
                        0.20,
                        0.30,
                        0.60,
                        {},
                        std::vector{volt::BoardCapabilityCopperWeightRefinement{2.0, 0.30, 0.28},
                                    volt::BoardCapabilityCopperWeightRefinement{1.0, 0.20, 0.20}}}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardCapabilityProfile{
                        "Example",
                        provenance,
                        0.20,
                        0.30,
                        0.60,
                        {},
                        std::vector{volt::BoardCapabilityCopperWeightRefinement{1.0, 0.20, 0.20},
                                    volt::BoardCapabilityCopperWeightRefinement{1.0, 0.25, 0.25}}}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardCapabilityProfile{
                        "Example", provenance, 0.20, 0.30, 0.60, {}, {}, std::vector{2, 2}}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardCapabilityProfile{"Example",
                                                  provenance,
                                                  0.20,
                                                  0.30,
                                                  0.60,
                                                  {},
                                                  {},
                                                  {},
                                                  volt::BoardCapabilityRange{2.0, 0.4}}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardCapabilityProfile{"Example",
                                                  provenance,
                                                  0.20,
                                                  0.30,
                                                  0.60,
                                                  {},
                                                  {},
                                                  {},
                                                  std::nullopt,
                                                  std::vector{2.0, 1.0}}),
                    std::invalid_argument);
}

TEST_CASE("BoardCapabilityProfile exposes a conservative explicit fallback") {
    const auto profile = volt::BoardCapabilityProfile::conservative_default();

    CHECK(profile.name() == "volt.conservative");
    CHECK(profile.provenance().source == "Volt built-in conservative fallback");
    CHECK(profile.provenance().as_of == "2026-06-11");
    CHECK(profile.minimum_track_width_mm() == 0.20);
    CHECK(profile.minimum_via_drill_mm() == 0.30);
    CHECK(profile.minimum_via_annular_mm() == 0.60);
    CHECK(profile.minimum_clearance_mm(volt::BoardClearanceKind::Track,
                                       volt::BoardClearanceKind::Track) == 0.20);
    CHECK(profile.minimum_clearance_mm(volt::BoardClearanceKind::Pad,
                                       volt::BoardClearanceKind::BoardEdge) == 0.30);
    CHECK(profile.copper_weight_refinements().empty());
    CHECK(profile.supported_copper_layer_counts().empty());
    CHECK_FALSE(profile.board_thickness_range_mm().has_value());
    CHECK(profile.available_copper_weights_oz().empty());
    CHECK_FALSE(profile.drill_diameter_range_mm().has_value());
}

TEST_CASE("Board stores capability profiles without mutating design rules") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_design_rules(volt::BoardDesignRules{0.11, 0.12, 0.21, 0.46, 0.07, 0.31});

    CHECK_FALSE(board.capability_profile().has_value());

    board.set_capability_profile(volt::BoardCapabilityProfile::conservative_default());

    REQUIRE(board.capability_profile().has_value());
    CHECK(board.capability_profile()->name() == "volt.conservative");
    CHECK(board.design_rules().copper_clearance_mm() == 0.11);
    CHECK(board.design_rules().minimum_track_width_mm() == 0.12);
    CHECK(board.design_rules().minimum_via_drill_diameter_mm() == 0.21);
    CHECK(board.design_rules().minimum_via_annular_diameter_mm() == 0.46);
    CHECK(board.design_rules().board_outline_clearance_mm() == 0.07);
    CHECK(board.design_rules().package_assembly_clearance_mm() == 0.31);
}

TEST_CASE("BoardRoom validates scope and optional rule overrides") {
    const auto front = volt::BoardLayerId{0};
    const auto back = volt::BoardLayerId{1};
    const auto outline =
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{5.0, 5.0});

    auto room = volt::BoardRoom{"BGA escape", outline, std::vector{front, back}, 7};
    CHECK(room.name() == "BGA escape");
    CHECK(room.outline().vertices() == outline.vertices());
    CHECK(room.layers() == std::vector{front, back});
    CHECK(room.priority() == 7);
    CHECK_FALSE(room.copper_clearance_mm().has_value());
    CHECK_FALSE(room.track_width_mm().has_value());

    room.set_copper_clearance_mm(0.075);
    room.set_track_width_mm(0.10);
    REQUIRE(room.copper_clearance_mm().has_value());
    REQUIRE(room.track_width_mm().has_value());
    CHECK(room.copper_clearance_mm().value() == 0.075);
    CHECK(room.track_width_mm().value() == 0.10);

    CHECK_THROWS_AS((volt::BoardRoom{"", outline, std::vector{front}}), std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardRoom{"Empty", outline, {}}), std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardRoom{"Duplicate", outline, std::vector{front, front}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(room.set_copper_clearance_mm(-0.1), std::invalid_argument);
    CHECK_THROWS_AS(room.set_copper_clearance_mm(std::numeric_limits<double>::infinity()),
                    std::invalid_argument);
    CHECK_THROWS_AS(room.set_track_width_mm(0.0), std::invalid_argument);
    CHECK_THROWS_AS(room.set_track_width_mm(std::numeric_limits<double>::infinity()),
                    std::invalid_argument);
}

TEST_CASE("Board rejects duplicate room names and missing room layers") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto outline =
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{5.0, 5.0});

    const auto room = board.add_room(volt::BoardRoom{"BGA escape", outline, std::vector{front}});
    CHECK(room == volt::BoardRoomId{0});
    CHECK(board.room_count() == 1);
    CHECK(board.room(room).name() == "BGA escape");

    CHECK_THROWS_AS(board.add_room(volt::BoardRoom{"BGA escape", outline, std::vector{front}}),
                    std::logic_error);
    CHECK_THROWS_AS(
        board.add_room(volt::BoardRoom{"Missing", outline, std::vector{volt::BoardLayerId{99}}}),
        std::out_of_range);
}
