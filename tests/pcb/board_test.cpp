#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/queries.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints.hpp>

namespace {

struct ResistorCircuit {
    volt::Circuit circuit;
    volt::ComponentId component;
    volt::PinDefId first_pin_definition;
    volt::PinDefId second_pin_definition;
    volt::PinId first_pin;
    volt::PinId second_pin;
    volt::NetId first_net;
    volt::NetId second_net;
};

struct MultiComponentNetCircuit {
    volt::Circuit circuit;
    std::vector<volt::ComponentId> components;
    volt::PinDefId first_pin_definition;
    volt::PinDefId second_pin_definition;
    volt::NetId shared_net;
};

struct TwoResistorCircuit {
    volt::Circuit circuit;
    volt::ComponentId first_component;
    volt::ComponentId second_component;
};

[[nodiscard]] ResistorCircuit make_resistor_circuit(bool select_physical_part = true) {
    auto circuit = volt::Circuit{};
    const auto first_pin_definition = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin_definition = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", {first_pin_definition, second_pin_definition}});
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"R1"});
    const auto first_pin =
        volt::queries::pin_by_definition(circuit, component, first_pin_definition).value();
    const auto second_pin =
        volt::queries::pin_by_definition(circuit, component, second_pin_definition).value();
    const auto first_net = circuit.add_net(volt::Net{volt::NetName{"LEFT"}, volt::NetKind::Signal});
    const auto second_net =
        circuit.add_net(volt::Net{volt::NetName{"RIGHT"}, volt::NetKind::Signal});

    circuit.connect(first_net, first_pin);
    circuit.connect(second_net, second_pin);

    if (select_physical_part) {
        circuit.select_physical_part(
            component, volt::PhysicalPart{
                           volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                           volt::PackageRef{"0603"},
                           volt::FootprintRef{"passives", "R_0603_1608Metric"},
                           std::vector{volt::PinPadMapping{first_pin_definition, "1"},
                                       volt::PinPadMapping{second_pin_definition, "2"}},
                       });
    }

    return ResistorCircuit{std::move(circuit),
                           component,
                           first_pin_definition,
                           second_pin_definition,
                           first_pin,
                           second_pin,
                           first_net,
                           second_net};
}

[[nodiscard]] MultiComponentNetCircuit make_multi_component_net(std::size_t component_count) {
    auto circuit = volt::Circuit{};
    const auto first_pin_definition = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin_definition = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", {first_pin_definition, second_pin_definition}});
    const auto shared_net =
        circuit.add_net(volt::Net{volt::NetName{"SHARED"}, volt::NetKind::Signal});

    auto components = std::vector<volt::ComponentId>{};
    components.reserve(component_count);
    for (std::size_t index = 0; index < component_count; ++index) {
        const auto component = circuit.instantiate_component(
            component_definition, volt::ReferenceDesignator{"R" + std::to_string(index + 1U)});
        circuit.select_physical_part(
            component, volt::PhysicalPart{
                           volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                           volt::PackageRef{"0603"},
                           volt::FootprintRef{"passives", "R_0603_1608Metric"},
                           std::vector{volt::PinPadMapping{first_pin_definition, "1"},
                                       volt::PinPadMapping{second_pin_definition, "2"}},
                       });
        const auto connected_pin_definition =
            index == 0U ? second_pin_definition : first_pin_definition;
        circuit.connect(
            shared_net,
            volt::queries::pin_by_definition(circuit, component, connected_pin_definition).value());
        components.push_back(component);
    }

    return MultiComponentNetCircuit{std::move(circuit), std::move(components), first_pin_definition,
                                    second_pin_definition, shared_net};
}

[[nodiscard]] TwoResistorCircuit make_two_resistor_circuit(volt::FootprintRef footprint) {
    auto circuit = volt::Circuit{};
    const auto first_pin_definition = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin_definition = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", {first_pin_definition, second_pin_definition}});
    const auto first_component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"R1"});
    const auto second_component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"R2"});

    for (const auto component : std::vector{first_component, second_component}) {
        circuit.select_physical_part(
            component, volt::PhysicalPart{
                           volt::ManufacturerPart{"Example", "DENSE"},
                           volt::PackageRef{"DENSE"},
                           footprint,
                           std::vector{volt::PinPadMapping{first_pin_definition, "1"},
                                       volt::PinPadMapping{second_pin_definition, "2"}},
                       });
        const auto first_pin =
            volt::queries::pin_by_definition(circuit, component, first_pin_definition).value();
        const auto second_pin =
            volt::queries::pin_by_definition(circuit, component, second_pin_definition).value();
        const auto first_net = circuit.add_net(volt::Net{
            volt::NetName{"N" + std::to_string(first_pin.index())}, volt::NetKind::Signal});
        const auto second_net = circuit.add_net(volt::Net{
            volt::NetName{"N" + std::to_string(second_pin.index())}, volt::NetKind::Signal});
        circuit.connect(first_net, first_pin);
        circuit.connect(second_net, second_pin);
    }

    return TwoResistorCircuit{std::move(circuit), first_component, second_component};
}

[[nodiscard]] const volt::Diagnostic *find_diagnostic(const volt::DiagnosticReport &report,
                                                      const std::string &code) {
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code() == volt::DiagnosticCode{code}) {
            return &diagnostic;
        }
    }
    return nullptr;
}

[[nodiscard]] std::vector<const volt::Diagnostic *>
find_diagnostics(const volt::DiagnosticReport &report, const std::string &code) {
    auto matches = std::vector<const volt::Diagnostic *>{};
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code() == volt::DiagnosticCode{code}) {
            matches.push_back(&diagnostic);
        }
    }
    return matches;
}

[[nodiscard]] volt::FootprintDefinition conflicting_passive_0603_footprint() {
    return volt::FootprintDefinition{
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::RoundedRectangle, volt::FootprintPoint{-0.80, 0.0},
                volt::FootprintSize{0.90, 0.95}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::RoundedRectangle, volt::FootprintPoint{0.80, 0.0},
                volt::FootprintSize{0.90, 0.95}, volt::FootprintLayerSet::front_smd()),
        }};
}

[[nodiscard]] volt::FootprintDefinition dense_overlap_footprint(bool include_geometry) {
    auto pads = std::vector{
        volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-3.0, 0.0},
            volt::FootprintSize{0.50, 0.50}, volt::FootprintLayerSet::front_smd()),
        volt::FootprintPad::surface_mount(
            "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{3.0, 0.0},
            volt::FootprintSize{0.50, 0.50}, volt::FootprintLayerSet::front_smd()),
    };
    if (!include_geometry) {
        return volt::FootprintDefinition{volt::FootprintRef{"test", "DenseOverlap"},
                                         std::move(pads)};
    }

    return volt::FootprintDefinition{
        volt::FootprintRef{"test", "DenseOverlap"},
        std::move(pads),
        volt::FootprintPolygon{std::vector{
            volt::FootprintPoint{-2.0, -1.25},
            volt::FootprintPoint{2.0, -1.25},
            volt::FootprintPoint{2.0, 1.25},
            volt::FootprintPoint{-2.0, 1.25},
        }},
        volt::FootprintPolygon{std::vector{
            volt::FootprintPoint{-1.5, -1.0},
            volt::FootprintPoint{1.5, -1.0},
            volt::FootprintPoint{1.5, 1.0},
            volt::FootprintPoint{-1.5, 1.0},
        }},
    };
}

[[nodiscard]] std::vector<volt::BoardClearancePair>
capability_clearances(double copper_clearance_mm = 0.20, double board_edge_clearance_mm = 0.30) {
    return std::vector{
        volt::BoardClearancePair{volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Track,
                                 copper_clearance_mm},
        volt::BoardClearancePair{volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Pad,
                                 copper_clearance_mm},
        volt::BoardClearancePair{volt::BoardClearanceKind::Track,
                                 volt::BoardClearanceKind::BoardEdge, board_edge_clearance_mm},
        volt::BoardClearancePair{volt::BoardClearanceKind::Pad, volt::BoardClearanceKind::BoardEdge,
                                 board_edge_clearance_mm},
    };
}

[[nodiscard]] volt::BoardCapabilityProfile
make_capability_profile(std::vector<volt::BoardClearancePair> clearances = capability_clearances(),
                        std::vector<volt::BoardCapabilityCopperWeightRefinement> refinements = {}) {
    return volt::BoardCapabilityProfile{
        "Example Fab 2-layer",
        volt::BoardCapabilityProvenance{"Example fab published capability table", "2026-06-11"},
        0.20,
        0.30,
        0.60,
        std::move(clearances),
        std::move(refinements),
    };
}

[[nodiscard]] volt::BoardCapabilityProfile make_physical_capability_profile() {
    return volt::BoardCapabilityProfile{
        "Physical Fab",
        volt::BoardCapabilityProvenance{"Physical fab published capability table", "2026-06-13"},
        0.10,
        0.15,
        0.25,
        capability_clearances(0.10, 0.20),
        {},
        std::vector{2},
        volt::BoardCapabilityRange{0.4, 2.0},
        std::vector{1.0, 2.0},
        volt::BoardCapabilityRange{0.15, 6.3},
    };
}

} // namespace

TEST_CASE("Board stores metadata, layers, outline, features, and placements") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};

    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    const auto feature = board.add_feature(
        volt::BoardFeature::hole("MH1", volt::BoardPoint{3.0, 3.0}, 3.2, false, "mounting"));
    const auto slot = board.add_feature(volt::BoardFeature::slot(
        "SLOT1", volt::BoardPoint{8.0, 4.0}, volt::BoardPoint{14.0, 4.0}, 1.5, false, "mounting"));
    const auto cutout = board.add_feature(volt::BoardFeature::cutout(
        "CUT1",
        std::vector{volt::BoardPoint{18.0, 4.0}, volt::BoardPoint{22.0, 4.0},
                    volt::BoardPoint{22.0, 8.0}, volt::BoardPoint{18.0, 8.0}},
        "access"));
    const auto circle = board.add_feature(volt::BoardFeature::circle(
        "FID1", volt::BoardPoint{45.0, 25.0}, 1.0, volt::BoardSide::Top, "fiducial"));
    const auto placement = board.place_component(
        volt::ComponentPlacement{fixture.component, volt::BoardPoint{25.0, 15.0},
                                 volt::BoardRotation::degrees(90.0), volt::BoardSide::Top, true});

    CHECK(board.name() == volt::BoardName{"Control"});
    CHECK(board.units() == volt::BoardUnits::Millimeters);
    REQUIRE(board.layer_stack().has_value());
    CHECK(board.layer_stack()->layers() == std::vector{front, back});
    REQUIRE(board.outline().has_value());
    CHECK(board.feature(feature).kind() == volt::BoardFeatureKind::Hole);
    CHECK(board.feature(feature).hole().drill_diameter_mm() == 3.2);
    CHECK(board.feature(feature).role() == "mounting");
    CHECK(board.feature(slot).slot().width_mm() == 1.5);
    CHECK(board.feature(slot).role() == "mounting");
    CHECK(board.feature(cutout).cutout().outline().size() == 4);
    CHECK(board.feature(cutout).role() == "access");
    CHECK(board.feature(circle).circle().side() == volt::BoardSide::Top);
    CHECK(board.feature(circle).role() == "fiducial");
    CHECK(board.placement(placement).component() == fixture.component);
    CHECK(board.placement(placement).position() == volt::BoardPoint{25.0, 15.0});
    CHECK(board.placement(placement).rotation() == volt::BoardRotation::degrees(90.0));
    CHECK(board.placement(placement).side() == volt::BoardSide::Top);
    CHECK(board.placement(placement).locked());
}

TEST_CASE("Board stores kernel-owned copper tracks and vias over existing nets and layers") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};

    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});

    const auto track = board.add_track(volt::BoardTrack{
        fixture.first_net,
        front,
        std::vector{
            volt::BoardPoint{5.0, 5.0},
            volt::BoardPoint{12.0, 5.0},
            volt::BoardPoint{12.0, 8.0},
        },
        0.25,
    });
    const auto via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{12.0, 8.0}, front, back, 0.30, 0.70});

    REQUIRE(board.track_count() == 1);
    CHECK(track == volt::BoardTrackId{0});
    CHECK(board.track(track).net() == fixture.first_net);
    CHECK(board.track(track).layer() == front);
    CHECK(board.track(track).points() == std::vector{volt::BoardPoint{5.0, 5.0},
                                                     volt::BoardPoint{12.0, 5.0},
                                                     volt::BoardPoint{12.0, 8.0}});
    CHECK(board.track(track).width_mm() == 0.25);

    REQUIRE(board.via_count() == 1);
    CHECK(via == volt::BoardViaId{0});
    CHECK(board.via(via).net() == fixture.first_net);
    CHECK(board.via(via).position() == volt::BoardPoint{12.0, 8.0});
    CHECK(board.via(via).start_layer() == front);
    CHECK(board.via(via).end_layer() == back);
    CHECK(board.via(via).drill_diameter_mm() == 0.30);
    CHECK(board.via(via).annular_diameter_mm() == 0.70);
}

TEST_CASE("Board stores kernel-owned zones, keepouts, and board text") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};

    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto silk = board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});

    const auto zone = board.add_zone(volt::BoardZone{
        std::vector{
            volt::BoardPoint{1.0, 1.0},
            volt::BoardPoint{8.0, 1.0},
            volt::BoardPoint{8.0, 6.0},
            volt::BoardPoint{1.0, 6.0},
        },
        std::vector{front},
        fixture.first_net,
        volt::BoardZoneFill::Solid,
        10,
    });
    const auto keepout = board.add_keepout(volt::BoardKeepout{
        std::vector{
            volt::BoardPoint{10.0, 1.0},
            volt::BoardPoint{14.0, 1.0},
            volt::BoardPoint{14.0, 4.0},
            volt::BoardPoint{10.0, 4.0},
        },
        std::vector{front},
        std::vector{volt::BoardKeepoutRestriction::Copper, volt::BoardKeepoutRestriction::Via},
    });
    const auto text = board.add_text(volt::BoardText{
        "REV A", volt::BoardPoint{3.0, 9.0}, volt::BoardRotation::degrees(0.0), silk, 1.2, true});

    REQUIRE(board.zone_count() == 1);
    CHECK(zone == volt::BoardZoneId{0});
    CHECK(board.zone(zone).outline() ==
          std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0},
                      volt::BoardPoint{8.0, 6.0}, volt::BoardPoint{1.0, 6.0}});
    CHECK(board.zone(zone).layers() == std::vector{front});
    CHECK(board.zone(zone).net() == fixture.first_net);
    CHECK(board.zone(zone).fill() == volt::BoardZoneFill::Solid);
    CHECK(board.zone(zone).priority() == 10);

    REQUIRE(board.keepout_count() == 1);
    CHECK(keepout == volt::BoardKeepoutId{0});
    CHECK(board.keepout(keepout).layers() == std::vector{front});
    CHECK(board.keepout(keepout).restrictions() ==
          std::vector{volt::BoardKeepoutRestriction::Copper, volt::BoardKeepoutRestriction::Via});

    REQUIRE(board.text_count() == 1);
    CHECK(text == volt::BoardTextId{0});
    CHECK(board.text(text).text() == "REV A");
    CHECK(board.text(text).position() == volt::BoardPoint{3.0, 9.0});
    CHECK(board.text(text).rotation() == volt::BoardRotation::degrees(0.0));
    CHECK(board.text(text).layer() == silk);
    CHECK(board.text(text).size_mm() == 1.2);
    CHECK(board.text(text).locked());
}

TEST_CASE("Board stores kernel-owned design rules") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};

    board.set_design_rules(volt::BoardDesignRules{0.20, 0.18, 0.30, 0.70, 0.10});

    CHECK(board.design_rules().copper_clearance_mm() == 0.20);
    CHECK(board.design_rules().minimum_track_width_mm() == 0.18);
    CHECK(board.design_rules().minimum_via_drill_diameter_mm() == 0.30);
    CHECK(board.design_rules().minimum_via_annular_diameter_mm() == 0.70);
    CHECK(board.design_rules().board_outline_clearance_mm() == 0.10);
}

TEST_CASE("Board outline accepts an explicit closing vertex") {
    auto outline = volt::BoardOutline{std::vector{
        volt::BoardPoint{0.0, 0.0},
        volt::BoardPoint{10.0, 0.0},
        volt::BoardPoint{10.0, 5.0},
        volt::BoardPoint{0.0, 5.0},
        volt::BoardPoint{0.0, 0.0},
    }};

    CHECK(outline.vertices() == std::vector{
                                    volt::BoardPoint{0.0, 0.0},
                                    volt::BoardPoint{10.0, 0.0},
                                    volt::BoardPoint{10.0, 5.0},
                                    volt::BoardPoint{0.0, 5.0},
                                });
    CHECK(outline.contains(volt::BoardPoint{5.0, 2.5}));
}

TEST_CASE("Board rejects structurally invalid board and placement mutations") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});

    CHECK_THROWS_AS(volt::BoardPoint(std::numeric_limits<double>::quiet_NaN(), 0.0),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::BoardRotation::degrees(std::numeric_limits<double>::infinity()),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        volt::BoardOutline(std::vector{volt::BoardPoint{0.0, 0.0}, volt::BoardPoint{1.0, 0.0}}),
        std::invalid_argument);
    CHECK_THROWS_AS(volt::BoardOutline(std::vector{
                        volt::BoardPoint{0.0, 3.0},
                        volt::BoardPoint{1.0, 0.0},
                        volt::BoardPoint{2.0, 3.0},
                        volt::BoardPoint{0.0, 1.0},
                        volt::BoardPoint{2.0, 1.0},
                    }),
                    std::invalid_argument);
    CHECK_THROWS_AS(board.set_layer_stack(volt::LayerStack{{front, volt::BoardLayerId{99}}, 1.6}),
                    std::out_of_range);
    CHECK_THROWS_AS(board.add_layer(volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper,
                                                     volt::BoardLayerSide::Top}),
                    std::logic_error);
    CHECK_THROWS_AS(
        board.place_component(volt::ComponentPlacement{
            volt::ComponentId{99}, volt::BoardPoint{1.0, 1.0}, volt::BoardRotation::degrees(0.0)}),
        std::out_of_range);

    [[maybe_unused]] const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{1.0, 1.0}, volt::BoardRotation::degrees(0.0)});
    CHECK_THROWS_AS(
        board.place_component(volt::ComponentPlacement{
            fixture.component, volt::BoardPoint{2.0, 2.0}, volt::BoardRotation::degrees(0.0)}),
        std::logic_error);

    [[maybe_unused]] const auto footprint =
        board.cache_footprint_definition(volt::passive_0603_footprint());
    CHECK(board.cache_footprint_definition(volt::passive_0603_footprint()) == footprint);
    CHECK_THROWS_AS(board.cache_footprint_definition(conflicting_passive_0603_footprint()),
                    std::logic_error);
    CHECK_THROWS_AS(board.footprint_definition(volt::FootprintDefId{99}), std::out_of_range);
    CHECK_THROWS_AS(
        board.add_feature(volt::BoardFeature::hole("BAD", volt::BoardPoint{1.0, 1.0}, -1.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(board.add_feature(volt::BoardFeature::slot("BAD", volt::BoardPoint{1.0, 1.0},
                                                               volt::BoardPoint{1.0, 1.0}, 1.0)),
                    std::invalid_argument);
    CHECK_THROWS_AS(board.add_feature(volt::BoardFeature::cutout("BAD", {})),
                    std::invalid_argument);
}

TEST_CASE("Board rejects cached footprint definitions that conflict with resolution libraries") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    [[maybe_unused]] const auto footprint =
        board.cache_footprint_definition(conflicting_passive_0603_footprint());

    CHECK_THROWS_AS(board.resolve_pads(volt::builtin_footprint_library()), std::logic_error);
}

TEST_CASE("Board rejects structurally invalid copper mutations") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    const auto mask = board.add_layer(
        volt::BoardLayer{"F.Mask", volt::BoardLayerRole::SolderMask, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});

    CHECK_THROWS_AS(
        volt::BoardTrack(fixture.first_net, front, std::vector{volt::BoardPoint{1.0, 1.0}}, 0.25),
        std::invalid_argument);
    CHECK_THROWS_AS(
        volt::BoardTrack(fixture.first_net, front,
                         std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{1.0, 1.0}}, 0.25),
        std::invalid_argument);
    CHECK_THROWS_AS(
        volt::BoardTrack(fixture.first_net, front,
                         std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{2.0, 1.0}}, 0.0),
        std::invalid_argument);
    CHECK_THROWS_AS(
        volt::BoardVia(fixture.first_net, volt::BoardPoint{1.0, 1.0}, front, front, 0.30, 0.70),
        std::invalid_argument);
    CHECK_THROWS_AS(
        volt::BoardVia(fixture.first_net, volt::BoardPoint{1.0, 1.0}, front, back, 0.70, 0.30),
        std::invalid_argument);

    CHECK_THROWS_AS(board.add_track(volt::BoardTrack{
                        volt::NetId{99}, front,
                        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{2.0, 1.0}}, 0.25}),
                    std::out_of_range);
    CHECK_THROWS_AS(board.add_track(volt::BoardTrack{
                        fixture.first_net, volt::BoardLayerId{99},
                        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{2.0, 1.0}}, 0.25}),
                    std::out_of_range);
    CHECK_THROWS_AS(board.add_track(volt::BoardTrack{
                        fixture.first_net, mask,
                        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{2.0, 1.0}}, 0.25}),
                    std::logic_error);
    CHECK_THROWS_AS(board.add_via(volt::BoardVia{fixture.first_net, volt::BoardPoint{2.0, 2.0},
                                                 front, mask, 0.30, 0.70}),
                    std::logic_error);
}

TEST_CASE("Board rejects structurally invalid zone, keepout, and text mutations") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto silk = board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});

    const auto valid_outline = std::vector{
        volt::BoardPoint{1.0, 1.0},
        volt::BoardPoint{4.0, 1.0},
        volt::BoardPoint{4.0, 4.0},
        volt::BoardPoint{1.0, 4.0},
    };

    CHECK_THROWS_AS(
        volt::BoardZone(std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{2.0, 1.0}},
                        std::vector{front}, fixture.first_net),
        std::invalid_argument);
    CHECK_THROWS_AS(volt::BoardZone(valid_outline, {}, fixture.first_net), std::invalid_argument);
    CHECK_THROWS_AS(volt::BoardKeepout(valid_outline, std::vector{front}, {}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardText{"", volt::BoardPoint{1.0, 1.0},
                                     volt::BoardRotation::degrees(0.0), silk, 1.0}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::BoardText{"REV A", volt::BoardPoint{1.0, 1.0},
                                     volt::BoardRotation::degrees(0.0), silk, 0.0}),
                    std::invalid_argument);

    CHECK_THROWS_AS(
        board.add_zone(volt::BoardZone(valid_outline, std::vector{front}, volt::NetId{99})),
        std::out_of_range);
    CHECK_THROWS_AS(
        board.add_zone(volt::BoardZone(valid_outline, std::vector{silk}, fixture.first_net)),
        std::logic_error);
    CHECK_THROWS_AS(
        board.add_keepout(volt::BoardKeepout{valid_outline, std::vector{volt::BoardLayerId{99}},
                                             std::vector{volt::BoardKeepoutRestriction::Copper}}),
        std::out_of_range);
    CHECK_THROWS_AS(board.add_text(volt::BoardText{"REV A", volt::BoardPoint{1.0, 1.0},
                                                   volt::BoardRotation::degrees(0.0),
                                                   volt::BoardLayerId{99}, 1.0}),
                    std::out_of_range);
}

TEST_CASE("Board resolves placed pads to logical pins and existing nets") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{10.0, 8.0}, volt::BoardRotation::degrees(0.0)});

    const auto resolutions = board.resolve_pads(volt::builtin_footprint_library());

    REQUIRE(resolutions.size() == 2);
    CHECK(resolutions[0].placement() == placement);
    CHECK(resolutions[0].component() == fixture.component);
    CHECK(resolutions[0].pad() == volt::FootprintPadId{0});
    CHECK(resolutions[0].pad_label() == "1");
    CHECK(resolutions[0].pin() == fixture.first_pin);
    CHECK(resolutions[0].net() == fixture.first_net);
    CHECK(resolutions[0].status() == volt::PadResolutionStatus::Connected);
    CHECK(resolutions[1].pad() == volt::FootprintPadId{1});
    CHECK(resolutions[1].pin() == fixture.second_pin);
    CHECK(resolutions[1].net() == fixture.second_net);
}

TEST_CASE("Board derives a ratsnest edge for a simple two-component net") {
    auto fixture = make_multi_component_net(2);
    auto board = volt::Board{fixture.circuit};
    const auto first_placement = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto second_placement = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{20.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto edges = board.ratsnest_edges(volt::builtin_footprint_library());

    REQUIRE(edges.size() == 1);
    CHECK(edges[0].net() == fixture.shared_net);
    CHECK(edges[0].from().placement() == first_placement);
    CHECK(edges[0].from().component() == fixture.components[0]);
    CHECK(edges[0].from().pad() == volt::FootprintPadId{1});
    CHECK(edges[0].from().position() == volt::BoardPoint{10.75, 10.0});
    CHECK(edges[0].to().placement() == second_placement);
    CHECK(edges[0].to().component() == fixture.components[1]);
    CHECK(edges[0].to().pad() == volt::FootprintPadId{0});
    CHECK(edges[0].to().position() == volt::BoardPoint{19.25, 10.0});
}

TEST_CASE("Board derives deterministic nearest ratsnest edges for a multi-pad net") {
    auto fixture = make_multi_component_net(3);
    auto board = volt::Board{fixture.circuit};
    const auto first_placement = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto second_placement = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{20.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto third_placement = board.place_component(volt::ComponentPlacement{
        fixture.components[2], volt::BoardPoint{15.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto first = board.ratsnest_edges(volt::builtin_footprint_library());
    const auto second = board.ratsnest_edges(volt::builtin_footprint_library());

    REQUIRE(first.size() == 2);
    REQUIRE(second.size() == first.size());
    CHECK(first[0].net() == fixture.shared_net);
    CHECK(first[0].from().placement() == first_placement);
    CHECK(first[0].from().pad() == volt::FootprintPadId{1});
    CHECK(first[0].to().placement() == third_placement);
    CHECK(first[0].to().pad() == volt::FootprintPadId{0});
    CHECK(first[1].net() == fixture.shared_net);
    CHECK(first[1].from().placement() == second_placement);
    CHECK(first[1].from().pad() == volt::FootprintPadId{0});
    CHECK(first[1].to().placement() == third_placement);
    CHECK(first[1].to().pad() == volt::FootprintPadId{0});
    CHECK(second[0].from().placement() == first[0].from().placement());
    CHECK(second[0].to().placement() == first[0].to().placement());
    CHECK(second[1].from().placement() == first[1].from().placement());
    CHECK(second[1].to().placement() == first[1].to().placement());
}

TEST_CASE("Board pad resolution and validation use cached footprint definitions") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    [[maybe_unused]] const auto footprint =
        board.cache_footprint_definition(volt::passive_0603_footprint());
    [[maybe_unused]] const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{10.0, 8.0}, volt::BoardRotation::degrees(0.0)});

    const auto empty_library = volt::FootprintLibrary{};
    const auto resolutions = board.resolve_pads(empty_library);
    const auto report = volt::validate_board(board, empty_library);

    REQUIRE(resolutions.size() == 2);
    CHECK(resolutions[0].net() == fixture.first_net);
    CHECK(resolutions[1].net() == fixture.second_net);
    CHECK(find_diagnostic(report, "PCB_FOOTPRINT_UNRESOLVED") == nullptr);
}

TEST_CASE("Board projects footprint courtyard and body geometry through placement transforms") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    const auto footprint = volt::FootprintDefinition{
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
                volt::FootprintSize{0.5, 0.5}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{1.0, 0.0},
                volt::FootprintSize{0.5, 0.5}, volt::FootprintLayerSet::front_smd()),
        },
        volt::FootprintPolygon{std::vector{
            volt::FootprintPoint{1.0, 2.0},
            volt::FootprintPoint{3.0, 2.0},
            volt::FootprintPoint{3.0, 4.0},
            volt::FootprintPoint{1.0, 4.0},
        }},
        volt::FootprintPolygon{std::vector{
            volt::FootprintPoint{0.0, 0.0},
            volt::FootprintPoint{1.0, 0.0},
            volt::FootprintPoint{1.0, 1.0},
            volt::FootprintPoint{0.0, 1.0},
        }},
    };
    [[maybe_unused]] const auto cached = board.cache_footprint_definition(footprint);
    const auto placement = board.place_component(
        volt::ComponentPlacement{fixture.component, volt::BoardPoint{10.0, 20.0},
                                 volt::BoardRotation::degrees(90.0), volt::BoardSide::Bottom});

    const auto geometries = board.project_footprint_geometries(volt::FootprintLibrary{});

    REQUIRE(geometries.size() == 1);
    CHECK(geometries[0].placement() == placement);
    CHECK(geometries[0].component() == fixture.component);
    CHECK(geometries[0].side() == volt::BoardSide::Bottom);
    REQUIRE(geometries[0].courtyard().has_value());
    CHECK(geometries[0].courtyard().value() ==
          std::vector{volt::BoardPoint{8.0, 19.0}, volt::BoardPoint{8.0, 17.0},
                      volt::BoardPoint{6.0, 17.0}, volt::BoardPoint{6.0, 19.0}});
    REQUIRE(geometries[0].body().has_value());
    CHECK(geometries[0].body().value() ==
          std::vector{volt::BoardPoint{10.0, 20.0}, volt::BoardPoint{10.0, 19.0},
                      volt::BoardPoint{9.0, 19.0}, volt::BoardPoint{9.0, 20.0}});
}

TEST_CASE("Board validation reports design issues without owning connectivity") {
    auto fixture = make_resistor_circuit(false);
    auto board = volt::Board{fixture.circuit};

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *missing_outline = find_diagnostic(report, "PCB_BOARD_OUTLINE_MISSING");
    REQUIRE(missing_outline != nullptr);
    CHECK(missing_outline->severity() == volt::Severity::Error);
    CHECK(missing_outline->entities().empty());

    const auto *unplaced = find_diagnostic(report, "PCB_COMPONENT_NOT_PLACED");
    REQUIRE(unplaced != nullptr);
    CHECK(unplaced->severity() == volt::Severity::Error);
    REQUIRE(unplaced->entities().size() == 1);
    CHECK(unplaced->entities()[0] == volt::EntityRef::component(fixture.component));

    const auto *missing_part = find_diagnostic(report, "PCB_COMPONENT_MISSING_SELECTED_PART");
    REQUIRE(missing_part != nullptr);
    CHECK(missing_part->severity() == volt::Severity::Error);
    REQUIRE(missing_part->entities().size() == 1);
    CHECK(missing_part->entities()[0] == volt::EntityRef::component(fixture.component));
}

TEST_CASE("Board validation reports suspicious board primitive intent") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{10.0, 10.0}));
    const auto missing_role = board.add_feature(volt::BoardFeature::slot(
        "SLOT", volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{4.0, 1.0}, 1.0));
    const auto outside = board.add_feature(
        volt::BoardFeature::hole("TH", volt::BoardPoint{12.0, 5.0}, 2.0, false, "tooling"));

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *role = find_diagnostic(report, "PCB_BOARD_FEATURE_ROLE_MISSING");
    REQUIRE(role != nullptr);
    CHECK(role->severity() == volt::Severity::Warning);
    CHECK(role->entities() == std::vector{volt::EntityRef::board_feature(missing_role)});

    const auto *outline = find_diagnostic(report, "PCB_BOARD_FEATURE_OUTSIDE_OUTLINE");
    REQUIRE(outline != nullptr);
    CHECK(outline->severity() == volt::Severity::Error);
    CHECK(outline->entities() == std::vector{volt::EntityRef::board_feature(outside)});
}

TEST_CASE("Board validation reports logical nets with no placed pads as board diagnostics") {
    auto fixture = make_multi_component_net(2);
    auto board = volt::Board{fixture.circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *unimplemented_net = find_diagnostic(report, "PCB_NET_WITHOUT_PLACED_PADS");
    REQUIRE(unimplemented_net != nullptr);
    CHECK(unimplemented_net->severity() == volt::Severity::Warning);
    REQUIRE(unimplemented_net->entities().size() == 1);
    CHECK(unimplemented_net->entities()[0] == volt::EntityRef::net(fixture.shared_net));
}

TEST_CASE("Board validation checks transformed pad bodies against the outline") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{5.0, 5.0}));
    const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{1.1, 2.5}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *outside_outline = find_diagnostic(report, "PCB_PLACEMENT_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    REQUIRE(outside_outline->entities().size() == 2);
    CHECK(outside_outline->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(outside_outline->entities()[1] == volt::EntityRef::component_placement(placement));
}

TEST_CASE("Board validation reports first PCB DRC rule violations") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.20, 0.25, 0.30, 0.70, 0.10});

    const auto narrow_track = board.add_track(volt::BoardTrack{
        fixture.first_net,
        front,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}},
        0.10,
    });
    const auto close_track = board.add_track(volt::BoardTrack{
        fixture.second_net,
        front,
        std::vector{volt::BoardPoint{1.0, 1.35}, volt::BoardPoint{8.0, 1.35}},
        0.25,
    });
    const auto small_via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{10.0, 10.0}, front, back, 0.20, 0.50});
    const auto outside_track = board.add_track(volt::BoardTrack{
        fixture.first_net,
        front,
        std::vector{volt::BoardPoint{19.95, 5.0}, volt::BoardPoint{21.0, 5.0}},
        0.25,
    });

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *track_width = find_diagnostic(report, "PCB_TRACK_WIDTH_BELOW_MINIMUM");
    REQUIRE(track_width != nullptr);
    CHECK(track_width->severity() == volt::Severity::Error);
    CHECK(track_width->entities() == std::vector{volt::EntityRef::board_track(narrow_track),
                                                 volt::EntityRef::net(fixture.first_net),
                                                 volt::EntityRef::board_layer(front)});
    REQUIRE(track_width->overlays().size() == 1);
    CHECK(track_width->overlays()[0].kind() == volt::DiagnosticOverlayKind::Segment);
    CHECK(track_width->overlays()[0].points() ==
          std::vector{volt::DiagnosticPoint{1.0, 1.0}, volt::DiagnosticPoint{8.0, 1.0}});
    CHECK(track_width->overlays()[0].entities() ==
          std::vector{volt::EntityRef::board_track(narrow_track)});
    CHECK(track_width->overlays()[0].layers() == std::vector{front});
    REQUIRE(track_width->measurement().has_value());
    CHECK(track_width->measurement().value() == volt::DiagnosticMeasurement{0.10, 0.25});

    const auto *via_drill = find_diagnostic(report, "PCB_VIA_DRILL_BELOW_MINIMUM");
    REQUIRE(via_drill != nullptr);
    CHECK(via_drill->entities() == std::vector{volt::EntityRef::board_via(small_via),
                                               volt::EntityRef::net(fixture.first_net)});
    REQUIRE(via_drill->overlays().size() == 1);
    CHECK(via_drill->overlays()[0].kind() == volt::DiagnosticOverlayKind::Point);
    CHECK(via_drill->overlays()[0].points() == std::vector{volt::DiagnosticPoint{10.0, 10.0}});
    CHECK(via_drill->overlays()[0].entities() ==
          std::vector{volt::EntityRef::board_via(small_via)});
    REQUIRE(via_drill->measurement().has_value());
    CHECK(via_drill->measurement().value() == volt::DiagnosticMeasurement{0.20, 0.30});

    const auto *via_annular = find_diagnostic(report, "PCB_VIA_ANNULAR_BELOW_MINIMUM");
    REQUIRE(via_annular != nullptr);
    CHECK(via_annular->entities() == std::vector{volt::EntityRef::board_via(small_via),
                                                 volt::EntityRef::net(fixture.first_net)});
    REQUIRE(via_annular->overlays().size() == 1);
    CHECK(via_annular->overlays()[0].kind() == volt::DiagnosticOverlayKind::Point);
    REQUIRE(via_annular->measurement().has_value());
    CHECK(via_annular->measurement().value() == volt::DiagnosticMeasurement{0.50, 0.70});

    const auto *clearance = find_diagnostic(report, "PCB_COPPER_CLEARANCE_VIOLATION");
    REQUIRE(clearance != nullptr);
    CHECK(clearance->entities() == std::vector{volt::EntityRef::board_track(narrow_track),
                                               volt::EntityRef::board_track(close_track),
                                               volt::EntityRef::net(fixture.first_net),
                                               volt::EntityRef::net(fixture.second_net),
                                               volt::EntityRef::board_layer(front)});
    REQUIRE(clearance->overlays().size() == 2);
    CHECK(clearance->overlays()[0].kind() == volt::DiagnosticOverlayKind::Segment);
    CHECK(clearance->overlays()[0].entities() ==
          std::vector{volt::EntityRef::board_track(narrow_track)});
    CHECK(clearance->overlays()[0].layers() == std::vector{front});
    CHECK(clearance->overlays()[1].kind() == volt::DiagnosticOverlayKind::Segment);
    CHECK(clearance->overlays()[1].entities() ==
          std::vector{volt::EntityRef::board_track(close_track)});
    REQUIRE(clearance->measurement().has_value());
    CHECK(clearance->measurement()->required_mm == 0.20);
    CHECK(clearance->measurement()->actual_mm < clearance->measurement()->required_mm);

    const auto *outside_outline = find_diagnostic(report, "PCB_COPPER_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->entities() == std::vector{volt::EntityRef::board_track(outside_track),
                                                     volt::EntityRef::net(fixture.first_net),
                                                     volt::EntityRef::board_layer(front)});
    REQUIRE(outside_outline->overlays().size() == 1);
    CHECK(outside_outline->overlays()[0].kind() == volt::DiagnosticOverlayKind::Segment);
    CHECK(outside_outline->overlays()[0].entities() ==
          std::vector{volt::EntityRef::board_track(outside_track)});
    CHECK_FALSE(outside_outline->measurement().has_value());
}

TEST_CASE("Board DRC reports component body and courtyard overlaps with clean pad copper") {
    auto library = volt::FootprintLibrary{};
    library.add(dense_overlap_footprint(true));
    auto fixture = make_two_resistor_circuit(volt::FootprintRef{"test", "DenseOverlap"});
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.20, 0.20, 0.30, 0.70, 0.10});
    const auto first_placement = board.place_component(volt::ComponentPlacement{
        fixture.first_component, volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto second_placement = board.place_component(volt::ComponentPlacement{
        fixture.second_component, volt::BoardPoint{12.5, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, library);

    CHECK(find_diagnostic(report, "PCB_COPPER_CLEARANCE_VIOLATION") == nullptr);

    const auto *body = find_diagnostic(report, "PCB_COMPONENT_BODY_OVERLAP");
    REQUIRE(body != nullptr);
    CHECK(body->severity() == volt::Severity::Error);
    CHECK(body->category() == volt::DiagnosticCategory{volt::diagnostic_categories::Drc});
    CHECK(body->entities() == std::vector{volt::EntityRef::component_placement(first_placement),
                                          volt::EntityRef::component_placement(second_placement),
                                          volt::EntityRef::component(fixture.first_component),
                                          volt::EntityRef::component(fixture.second_component)});
    REQUIRE(body->overlays().size() == 2);
    CHECK(body->overlays()[0].kind() == volt::DiagnosticOverlayKind::Polygon);
    CHECK(body->overlays()[0].entities() ==
          std::vector{volt::EntityRef::component_placement(first_placement)});
    CHECK(body->overlays()[0].points() ==
          std::vector{volt::DiagnosticPoint{8.5, 9.0}, volt::DiagnosticPoint{11.5, 9.0},
                      volt::DiagnosticPoint{11.5, 11.0}, volt::DiagnosticPoint{8.5, 11.0}});
    CHECK(body->overlays()[1].kind() == volt::DiagnosticOverlayKind::Polygon);
    CHECK_FALSE(body->measurement().has_value());

    const auto *courtyard = find_diagnostic(report, "PCB_COMPONENT_COURTYARD_OVERLAP");
    REQUIRE(courtyard != nullptr);
    CHECK(courtyard->severity() == volt::Severity::Error);
    CHECK(courtyard->category() == volt::DiagnosticCategory{volt::diagnostic_categories::Drc});
    CHECK(courtyard->entities() ==
          std::vector{volt::EntityRef::component_placement(first_placement),
                      volt::EntityRef::component_placement(second_placement),
                      volt::EntityRef::component(fixture.first_component),
                      volt::EntityRef::component(fixture.second_component)});
    REQUIRE(courtyard->overlays().size() == 2);
    CHECK(courtyard->overlays()[0].kind() == volt::DiagnosticOverlayKind::Polygon);
    CHECK(courtyard->overlays()[1].kind() == volt::DiagnosticOverlayKind::Polygon);
}

TEST_CASE("Board DRC ignores component geometry overlaps on opposite sides") {
    auto library = volt::FootprintLibrary{};
    library.add(dense_overlap_footprint(true));
    auto fixture = make_two_resistor_circuit(volt::FootprintRef{"test", "DenseOverlap"});
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.20, 0.20, 0.30, 0.70, 0.10});
    [[maybe_unused]] const auto first_placement = board.place_component(
        volt::ComponentPlacement{fixture.first_component, volt::BoardPoint{10.0, 10.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top});
    [[maybe_unused]] const auto second_placement = board.place_component(
        volt::ComponentPlacement{fixture.second_component, volt::BoardPoint{10.0, 10.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Bottom});

    const auto report = volt::validate_board(board, library);

    CHECK(find_diagnostic(report, "PCB_COMPONENT_BODY_OVERLAP") == nullptr);
    CHECK(find_diagnostic(report, "PCB_COMPONENT_COURTYARD_OVERLAP") == nullptr);
    CHECK(find_diagnostic(report, "PCB_COPPER_CLEARANCE_VIOLATION") == nullptr);
}

TEST_CASE("Board DRC explicitly skips component overlap checks without footprint geometry") {
    auto library = volt::FootprintLibrary{};
    library.add(dense_overlap_footprint(false));
    auto fixture = make_two_resistor_circuit(volt::FootprintRef{"test", "DenseOverlap"});
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    const auto first_placement = board.place_component(volt::ComponentPlacement{
        fixture.first_component, volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto second_placement = board.place_component(volt::ComponentPlacement{
        fixture.second_component, volt::BoardPoint{12.5, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto geometries = board.project_footprint_geometries(library);
    REQUIRE(geometries.size() == 2);
    CHECK(geometries[0].placement() == first_placement);
    CHECK_FALSE(geometries[0].courtyard().has_value());
    CHECK_FALSE(geometries[0].body().has_value());
    CHECK(geometries[1].placement() == second_placement);
    CHECK_FALSE(geometries[1].courtyard().has_value());
    CHECK_FALSE(geometries[1].body().has_value());

    const auto report = volt::validate_board(board, library);

    CHECK(find_diagnostic(report, "PCB_COMPONENT_BODY_OVERLAP") == nullptr);
    CHECK(find_diagnostic(report, "PCB_COMPONENT_COURTYARD_OVERLAP") == nullptr);
    CHECK(find_diagnostic(report, "PCB_COPPER_CLEARANCE_VIOLATION") == nullptr);
}

TEST_CASE("Board validation emits no capability diagnostics without an explicit profile") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.01, 0.02, 0.03, 0.04, 0.0});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    CHECK(find_diagnostic(report, "PCB_RULE_BELOW_CAPABILITY") == nullptr);
    CHECK(find_diagnostic(report, "PCB_RULE_AT_CAPABILITY_MINIMUM") == nullptr);
}

TEST_CASE("Board validation reports board scalar rules below capability") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_capability_profile(make_capability_profile());
    board.set_design_rules(volt::BoardDesignRules{0.30, 0.19, 0.35, 0.70, 0.35});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *diagnostic = find_diagnostic(report, "PCB_RULE_BELOW_CAPABILITY");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity() == volt::Severity::Error);
    CHECK(diagnostic->entities() == std::vector{volt::EntityRef::board()});
    CHECK(diagnostic->message().find("minimum track width") != std::string::npos);
}

TEST_CASE("Board validation reports clearance matrix entries below capability") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_capability_profile(make_capability_profile(capability_clearances(0.20, 0.30)));
    auto rules = volt::BoardDesignRules{0.30, 0.30, 0.40, 0.80, 0.40};
    rules.set_clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Pad, 0.19);
    board.set_design_rules(rules);

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *diagnostic = find_diagnostic(report, "PCB_RULE_BELOW_CAPABILITY");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity() == volt::Severity::Error);
    CHECK(diagnostic->entities() == std::vector{volt::EntityRef::board()});
    CHECK(diagnostic->message().find("track-to-pad clearance") != std::string::npos);
}

TEST_CASE("Board validation reports room overrides below capability") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_capability_profile(make_capability_profile());
    board.set_design_rules(volt::BoardDesignRules{0.30, 0.30, 0.40, 0.80, 0.40});
    auto room = volt::BoardRoom{
        "Fine pitch escape",
        volt::BoardOutline::rectangle(volt::BoardPoint{1.0, 1.0}, volt::BoardSize{4.0, 4.0}),
        std::vector{front},
    };
    room.set_copper_clearance_mm(0.10);
    room.set_track_width_mm(0.10);
    const auto room_id = board.add_room(std::move(room));

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *diagnostic = find_diagnostic(report, "PCB_RULE_BELOW_CAPABILITY");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity() == volt::Severity::Error);
    CHECK(diagnostic->entities() == std::vector{volt::EntityRef::board_room(room_id)});
    CHECK(diagnostic->message().find("Fine pitch escape") != std::string::npos);
}

TEST_CASE("Board validation warns when net class rules sit at capability minimum") {
    auto circuit = volt::Circuit{};
    auto net_class = volt::NetClass{volt::NetClassName{"Signal"}};
    net_class.set_track_width_mm(0.20);
    static_cast<void>(circuit.add_net_class(std::move(net_class)));

    auto board = volt::Board{circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_capability_profile(make_capability_profile());
    board.set_design_rules(volt::BoardDesignRules{0.30, 0.30, 0.40, 0.80, 0.40});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *diagnostic = find_diagnostic(report, "PCB_RULE_AT_CAPABILITY_MINIMUM");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity() == volt::Severity::Warning);
    CHECK(diagnostic->message().find("Signal") != std::string::npos);
    CHECK(diagnostic->message().find("at manufacturing minimum") != std::string::npos);
}

TEST_CASE("Board validation applies copper-weight refinements to room overrides") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    auto front_layer =
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top};
    front_layer.set_copper_weight_oz(2.0);
    const auto front = board.add_layer(std::move(front_layer));
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_capability_profile(make_capability_profile(
        capability_clearances(0.10, 0.30),
        std::vector{volt::BoardCapabilityCopperWeightRefinement{2.0, 0.30, 0.25}}));
    board.set_design_rules(volt::BoardDesignRules{0.30, 0.30, 0.40, 0.80, 0.40});
    auto room = volt::BoardRoom{
        "Heavy copper",
        volt::BoardOutline::rectangle(volt::BoardPoint{1.0, 1.0}, volt::BoardSize{4.0, 4.0}),
        std::vector{front},
    };
    room.set_track_width_mm(0.20);
    room.set_copper_clearance_mm(0.20);
    const auto room_id = board.add_room(std::move(room));

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *diagnostic = find_diagnostic(report, "PCB_RULE_BELOW_CAPABILITY");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->entities() == std::vector{volt::EntityRef::board_room(room_id)});
    CHECK(diagnostic->message().find("Heavy copper") != std::string::npos);
}

TEST_CASE("Board validation reports fabrication physical facts outside capability") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    auto front_layer =
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top};
    front_layer.set_copper_weight_oz(0.5);
    const auto front = board.add_layer(std::move(front_layer));
    const auto inner = board.add_layer(
        volt::BoardLayer{"In1.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Inner});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, inner, back}, 0.3});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_capability_profile(make_physical_capability_profile());

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *layer_count = find_diagnostic(report, "PCB_COPPER_LAYER_COUNT_OUTSIDE_CAPABILITY");
    REQUIRE(layer_count != nullptr);
    CHECK(layer_count->severity() == volt::Severity::Error);
    CHECK(layer_count->entities() == std::vector{volt::EntityRef::board()});

    const auto *thickness = find_diagnostic(report, "PCB_BOARD_THICKNESS_OUTSIDE_CAPABILITY");
    REQUIRE(thickness != nullptr);
    CHECK(thickness->severity() == volt::Severity::Error);
    CHECK(thickness->entities() == std::vector{volt::EntityRef::board()});

    const auto *copper_weight = find_diagnostic(report, "PCB_COPPER_WEIGHT_OUTSIDE_CAPABILITY");
    REQUIRE(copper_weight != nullptr);
    CHECK(copper_weight->severity() == volt::Severity::Error);
    CHECK(copper_weight->entities() == std::vector{volt::EntityRef::board_layer(front)});
}

TEST_CASE("Board validation warns when physical facts sit at capability boundaries") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    auto front_layer =
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top};
    front_layer.set_copper_weight_oz(1.0);
    const auto front = board.add_layer(std::move(front_layer));
    auto back_layer =
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom};
    back_layer.set_copper_weight_oz(1.0);
    const auto back = board.add_layer(std::move(back_layer));
    board.set_layer_stack(volt::LayerStack{{front, back}, 0.4});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.20, 0.20, 0.15, 0.25, 0.20});
    board.set_capability_profile(make_physical_capability_profile());
    static_cast<void>(board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{5.0, 5.0}, front, back, 0.15, 0.25}));

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *thickness = find_diagnostic(report, "PCB_BOARD_THICKNESS_AT_CAPABILITY_LIMIT");
    REQUIRE(thickness != nullptr);
    CHECK(thickness->severity() == volt::Severity::Warning);

    const auto *drill = find_diagnostic(report, "PCB_DRILL_DIAMETER_AT_CAPABILITY_LIMIT");
    REQUIRE(drill != nullptr);
    CHECK(drill->severity() == volt::Severity::Warning);
}

TEST_CASE("Board validation reports via and pad drills outside capability") {
    auto fixture = make_resistor_circuit(false);
    fixture.circuit.select_physical_part(
        fixture.component, volt::PhysicalPart{
                               volt::ManufacturerPart{"Example", "TH-2"},
                               volt::PackageRef{"TH"},
                               volt::FootprintRef{"test", "TH_2"},
                               std::vector{volt::PinPadMapping{fixture.first_pin_definition, "1"},
                                           volt::PinPadMapping{fixture.second_pin_definition, "2"}},
                           });
    auto library = volt::FootprintLibrary{};
    library.add(volt::FootprintDefinition{
        volt::FootprintRef{"test", "TH_2"},
        std::vector{
            volt::FootprintPad::through_hole(
                "1", volt::FootprintPadShape::Circle, volt::FootprintPoint{-1.0, 0.0},
                volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::through_hole(),
                volt::FootprintDrill{6.4, volt::FootprintPadPlating::Plated}),
            volt::FootprintPad::through_hole(
                "2", volt::FootprintPadShape::Circle, volt::FootprintPoint{1.0, 0.0},
                volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::through_hole(),
                volt::FootprintDrill{0.20, volt::FootprintPadPlating::Plated}),
        }});
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.20, 0.20, 0.15, 0.25, 0.20});
    board.set_capability_profile(make_physical_capability_profile());
    const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{5.0, 5.0}, volt::BoardRotation::degrees(0.0)});
    const auto via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{8.0, 8.0}, front, back, 6.4, 6.6});

    const auto report = volt::validate_board(board, library);

    const auto drills = find_diagnostics(report, "PCB_DRILL_DIAMETER_OUTSIDE_CAPABILITY");
    REQUIRE(drills.size() == 2);
    CHECK(drills[0]->severity() == volt::Severity::Error);
    CHECK(drills[0]->entities() == std::vector{volt::EntityRef::board_via(via)});
    CHECK(drills[1]->severity() == volt::Severity::Error);
    CHECK(drills[1]->entities() ==
          std::vector{volt::EntityRef::component_placement(placement),
                      volt::EntityRef::footprint_pad(volt::FootprintPadId{0})});
}

TEST_CASE("Board validation ignores absent fabrication physical capability limits") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    auto front_layer =
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top};
    front_layer.set_copper_weight_oz(0.5);
    const auto front = board.add_layer(std::move(front_layer));
    const auto inner = board.add_layer(
        volt::BoardLayer{"In1.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Inner});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, inner, back}, 0.3});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.20, 0.20, 0.10, 0.20, 0.20});
    board.set_capability_profile(make_capability_profile());
    static_cast<void>(board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{8.0, 8.0}, front, back, 6.4, 6.6}));

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    CHECK(find_diagnostic(report, "PCB_COPPER_LAYER_COUNT_OUTSIDE_CAPABILITY") == nullptr);
    CHECK(find_diagnostic(report, "PCB_BOARD_THICKNESS_OUTSIDE_CAPABILITY") == nullptr);
    CHECK(find_diagnostic(report, "PCB_COPPER_WEIGHT_OUTSIDE_CAPABILITY") == nullptr);
    CHECK(find_diagnostic(report, "PCB_DRILL_DIAMETER_OUTSIDE_CAPABILITY") == nullptr);
}

TEST_CASE("Board validation reports netless zones outside the board outline") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{10.0, 10.0}));

    const auto zone = board.add_zone(volt::BoardZone{
        std::vector{
            volt::BoardPoint{8.0, 8.0},
            volt::BoardPoint{12.0, 8.0},
            volt::BoardPoint{12.0, 12.0},
            volt::BoardPoint{8.0, 12.0},
        },
        std::vector{front},
    });

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *outside_outline = find_diagnostic(report, "PCB_COPPER_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->entities() ==
          std::vector{volt::EntityRef::board_zone(zone), volt::EntityRef::board_layer(front)});
}

TEST_CASE("Board validation reports obvious keepout violations") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    const auto keepout = board.add_keepout(volt::BoardKeepout{
        std::vector{
            volt::BoardPoint{4.0, 4.0},
            volt::BoardPoint{8.0, 4.0},
            volt::BoardPoint{8.0, 8.0},
            volt::BoardPoint{4.0, 8.0},
        },
        std::vector{front},
        std::vector{volt::BoardKeepoutRestriction::All},
    });
    const auto track = board.add_track(volt::BoardTrack{
        fixture.first_net,
        front,
        std::vector{volt::BoardPoint{2.0, 6.0}, volt::BoardPoint{10.0, 6.0}},
        0.25,
    });
    const auto via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{6.0, 6.0}, front, back, 0.30, 0.70});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *copper_keepout = find_diagnostic(report, "PCB_KEEPOUT_COPPER_VIOLATION");
    REQUIRE(copper_keepout != nullptr);
    CHECK(copper_keepout->severity() == volt::Severity::Error);
    CHECK(copper_keepout->entities() == std::vector{volt::EntityRef::board_keepout(keepout),
                                                    volt::EntityRef::board_track(track),
                                                    volt::EntityRef::net(fixture.first_net),
                                                    volt::EntityRef::board_layer(front)});

    const auto *via_keepout = find_diagnostic(report, "PCB_KEEPOUT_VIA_VIOLATION");
    REQUIRE(via_keepout != nullptr);
    CHECK(via_keepout->entities() == std::vector{volt::EntityRef::board_keepout(keepout),
                                                 volt::EntityRef::board_via(via),
                                                 volt::EntityRef::net(fixture.first_net),
                                                 volt::EntityRef::board_layer(front)});
}

TEST_CASE("Board validation maps bottom-side surface-mount pads to bottom copper") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.20});
    const auto placement = board.place_component(
        volt::ComponentPlacement{fixture.component, volt::BoardPoint{10.0, 10.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Bottom});
    const auto bottom_track = board.add_track(volt::BoardTrack{
        fixture.second_net,
        back,
        std::vector{volt::BoardPoint{10.75, 10.5}, volt::BoardPoint{12.0, 10.5}},
        0.25,
    });

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *clearance = find_diagnostic(report, "PCB_COPPER_CLEARANCE_VIOLATION");
    REQUIRE(clearance != nullptr);
    CHECK(clearance->entities() ==
          std::vector{volt::EntityRef::board_track(bottom_track),
                      volt::EntityRef::component_placement(placement),
                      volt::EntityRef::footprint_pad(volt::FootprintPadId{0}),
                      volt::EntityRef::net(fixture.second_net),
                      volt::EntityRef::net(fixture.first_net), volt::EntityRef::board_layer(back)});
}

TEST_CASE("Board validation reports unrouted placed logical nets after routing begins") {
    auto fixture = make_multi_component_net(2);
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    const auto first_placement = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto second_placement = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{20.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    [[maybe_unused]] const auto partial_route = board.add_track(volt::BoardTrack{
        fixture.shared_net,
        front,
        std::vector{volt::BoardPoint{10.75, 10.0}, volt::BoardPoint{14.0, 10.0}},
        0.25,
    });

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *unrouted = find_diagnostic(report, "PCB_NET_UNROUTED");
    REQUIRE(unrouted != nullptr);
    CHECK(unrouted->severity() == volt::Severity::Warning);
    CHECK(unrouted->entities() ==
          std::vector{volt::EntityRef::net(fixture.shared_net),
                      volt::EntityRef::component_placement(first_placement),
                      volt::EntityRef::footprint_pad(volt::FootprintPadId{1}),
                      volt::EntityRef::component_placement(second_placement),
                      volt::EntityRef::footprint_pad(volt::FootprintPadId{0})});
}

TEST_CASE("Board validation reports unrouted placed logical nets before routing begins") {
    auto fixture = make_multi_component_net(2);
    auto board = volt::Board{fixture.circuit};
    [[maybe_unused]] const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    const auto first_placement = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto second_placement = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{20.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *unrouted = find_diagnostic(report, "PCB_NET_UNROUTED");
    REQUIRE(unrouted != nullptr);
    CHECK(unrouted->severity() == volt::Severity::Warning);
    CHECK(unrouted->entities() ==
          std::vector{volt::EntityRef::net(fixture.shared_net),
                      volt::EntityRef::component_placement(first_placement),
                      volt::EntityRef::footprint_pad(volt::FootprintPadId{1}),
                      volt::EntityRef::component_placement(second_placement),
                      volt::EntityRef::footprint_pad(volt::FootprintPadId{0})});
}

TEST_CASE("Board validation detects pad edges crossing a concave outline") {
    auto fixture = make_resistor_circuit(false);
    fixture.circuit.select_physical_part(
        fixture.component, volt::PhysicalPart{
                               volt::ManufacturerPart{"Volt", "wide-pad-fixture"},
                               volt::PackageRef{"fixture"},
                               volt::FootprintRef{"test", "WidePad"},
                               std::vector{volt::PinPadMapping{fixture.first_pin_definition, "1"},
                                           volt::PinPadMapping{fixture.second_pin_definition, "2"}},
                           });

    auto library = volt::FootprintLibrary{};
    library.add(volt::FootprintDefinition{
        volt::FootprintRef{"test", "WidePad"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
                volt::FootprintSize{3.0, 0.4}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, -1.0},
                volt::FootprintSize{0.3, 0.3}, volt::FootprintLayerSet::front_smd()),
        },
    });

    auto board = volt::Board{fixture.circuit};
    board.set_outline(volt::BoardOutline{std::vector{
        volt::BoardPoint{0.0, 0.0},
        volt::BoardPoint{6.0, 0.0},
        volt::BoardPoint{6.0, 6.0},
        volt::BoardPoint{4.0, 6.0},
        volt::BoardPoint{4.0, 2.0},
        volt::BoardPoint{2.0, 2.0},
        volt::BoardPoint{2.0, 6.0},
        volt::BoardPoint{0.0, 6.0},
    }});
    const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{3.0, 1.9}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, library);

    const auto *outside_outline = find_diagnostic(report, "PCB_PLACEMENT_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    REQUIRE(outside_outline->entities().size() == 2);
    CHECK(outside_outline->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(outside_outline->entities()[1] == volt::EntityRef::component_placement(placement));
}

TEST_CASE("Board validation diagnoses footprint resolution and pad geometry issues") {
    auto fixture = make_resistor_circuit(false);
    fixture.circuit.select_physical_part(
        fixture.component, volt::PhysicalPart{
                               volt::ManufacturerPart{"Acme", "NOPE"},
                               volt::PackageRef{"custom"},
                               volt::FootprintRef{"missing", "NotARealFootprint"},
                               std::vector{volt::PinPadMapping{fixture.first_pin_definition, "99"},
                                           volt::PinPadMapping{fixture.second_pin_definition, "2"}},
                           });
    auto board = volt::Board{fixture.circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{5.0, 5.0}));
    const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    auto report = volt::validate_board(board, volt::builtin_footprint_library());
    const auto *unresolved_footprint = find_diagnostic(report, "PCB_FOOTPRINT_UNRESOLVED");
    REQUIRE(unresolved_footprint != nullptr);
    CHECK(unresolved_footprint->severity() == volt::Severity::Error);
    REQUIRE(unresolved_footprint->entities().size() == 2);
    CHECK(unresolved_footprint->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(unresolved_footprint->entities()[1] == volt::EntityRef::component_placement(placement));

    fixture.circuit.select_physical_part(
        fixture.component, volt::PhysicalPart{
                               volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                               volt::PackageRef{"0603"},
                               volt::FootprintRef{"passives", "R_0603_1608Metric"},
                               std::vector{volt::PinPadMapping{fixture.first_pin_definition, "99"},
                                           volt::PinPadMapping{fixture.second_pin_definition, "2"}},
                           });

    report = volt::validate_board(board, volt::builtin_footprint_library());
    const auto *unknown_pad = find_diagnostic(report, "PCB_PAD_MAPPING_UNKNOWN_PAD");
    REQUIRE(unknown_pad != nullptr);
    CHECK(unknown_pad->severity() == volt::Severity::Error);
    REQUIRE(unknown_pad->entities().size() == 2);
    CHECK(unknown_pad->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(unknown_pad->entities()[1] == volt::EntityRef::component_placement(placement));

    const auto *missing_pad_mapping = find_diagnostic(report, "PCB_PAD_MAPPING_MISSING_PIN");
    REQUIRE(missing_pad_mapping != nullptr);
    CHECK(missing_pad_mapping->severity() == volt::Severity::Error);
    REQUIRE(missing_pad_mapping->entities().size() == 2);
    CHECK(missing_pad_mapping->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(missing_pad_mapping->entities()[1] == volt::EntityRef::component_placement(placement));

    const auto *outside_outline = find_diagnostic(report, "PCB_PLACEMENT_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    REQUIRE(outside_outline->entities().size() == 2);
    CHECK(outside_outline->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(outside_outline->entities()[1] == volt::EntityRef::component_placement(placement));
}
