#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/pcb/pcb_reader.hpp>
#include <volt/io/pcb/pcb_writer.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>
#include <volt/pcb/routing/board_router.hpp>

namespace {

struct SoicFixture {
    volt::Circuit circuit;
    volt::ComponentId component;
    std::vector<volt::NetId> nets;
};

struct EscapeBoard {
    volt::Board board;
    volt::BoardLayerId front;
    volt::BoardLayerId back;
};

[[nodiscard]] volt::PinSpec signal_pin(std::string name, std::string number) {
    return volt::PinSpec{
        .name = std::move(name),
        .number = std::move(number),
        .terminal_kind = volt::ElectricalTerminalKind::Signal,
        .direction = volt::ElectricalDirection::Bidirectional,
        .drive_kind = volt::ElectricalDriveKind::Passive,
    };
}

[[nodiscard]] SoicFixture make_soic_fixture(bool leave_first_pin_unconnected = false) {
    auto circuit = volt::Circuit{};
    auto pin_specs = std::vector<volt::PinSpec>{};
    auto nets = std::vector<volt::NetId>{};
    pin_specs.reserve(8U);
    nets.reserve(8U);
    for (std::size_t index = 0; index < 8U; ++index) {
        const auto number = std::to_string(index + 1U);
        pin_specs.push_back(signal_pin("P" + number, number));
        nets.push_back(
            circuit.add_net(volt::NetSpec{volt::NetName{"N" + number}, volt::NetKind::Signal}));
    }

    const auto definition = circuit.define_component(
        volt::ComponentSpec{.name = "TLC555", .pins = std::move(pin_specs)});
    const auto &pins = circuit.get(definition).pins();
    const auto component =
        circuit.instantiate_component(definition, volt::ReferenceDesignator{"U1"});

    auto mappings = std::vector<volt::PinPadMapping>{};
    mappings.reserve(pins.size());
    for (std::size_t index = 0; index < pins.size(); ++index) {
        mappings.emplace_back(pins[index], std::to_string(index + 1U));
        const auto pin = volt::queries::pin_by_definition(circuit, component, pins[index]).value();
        if (!leave_first_pin_unconnected || index != 0U) {
            circuit.connect(nets[index], pin);
        }
    }

    circuit.update(component, volt::SelectPhysicalPart{volt::PhysicalPart{
                                  volt::ManufacturerPart{"Texas Instruments", "TLC555CDR"},
                                  volt::PackageRef{"SOIC-8"},
                                  volt::FootprintRef{"ics", "SOIC-8_3.9x4.9mm_P1.27mm"},
                                  std::move(mappings),
                              }});

    return SoicFixture{std::move(circuit), component, std::move(nets)};
}

[[nodiscard]] EscapeBoard make_escape_board(const SoicFixture &fixture) {
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{40.0, 40.0}));
    board.set_design_rules(volt::BoardDesignRules{0.21, 0.21, 0.31, 0.61, 0.31});
    board.set_capability_profile(volt::BoardCapabilityProfile::conservative_default());
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{20.0, 20.0}, volt::BoardRotation::degrees(0.0)}));
    return EscapeBoard{std::move(board), front, back};
}

[[nodiscard]] std::vector<std::string> pcb_drc_codes(const volt::DiagnosticReport &report) {
    auto codes = std::vector<std::string>{};
    for (const auto &diagnostic : report.diagnostics()) {
        const auto code = diagnostic.code().value();
        if (code.rfind("PCB_", 0) == 0 && code != "PCB_NET_UNROUTED") {
            auto entry = code;
            if (diagnostic.measurement().has_value()) {
                entry += ":" + std::to_string(diagnostic.measurement()->actual_mm) + "/" +
                         std::to_string(diagnostic.measurement()->required_mm);
            }
            codes.push_back(std::move(entry));
        }
    }
    return codes;
}

[[nodiscard]] const volt::BoardEscapePadResult *find_pad(const volt::BoardEscapeResult &result,
                                                         const std::string &label) {
    const auto match = std::find_if(
        result.pads.begin(), result.pads.end(),
        [&label](const volt::BoardEscapePadResult &pad) { return pad.pad_label == label; });
    if (match == result.pads.end()) {
        return nullptr;
    }
    return &*match;
}

} // namespace

TEST_CASE("Escape router fans out SOIC pads into deterministic room-backed stubs",
          "[pcb][escape]") {
    auto fixture = make_soic_fixture();
    auto layout = make_escape_board(fixture);
    auto escape_router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};

    const auto result = escape_router.escape(fixture.component);

    const auto escaped_count = static_cast<std::size_t>(
        std::count_if(result.pads.begin(), result.pads.end(),
                      [](const volt::BoardEscapePadResult &pad) { return pad.escaped; }));
    CAPTURE(result.pads.size());
    CAPTURE(escaped_count);
    CAPTURE(layout.board.room_count());
    CAPTURE(layout.board.track_count());
    REQUIRE(result.complete());
    REQUIRE(result.room.has_value());
    REQUIRE(result.pads.size() == 8U);
    const auto &room = layout.board.room(result.room.value());
    CHECK(room.name() == "escape-U1-at-20.000-20.000");
    REQUIRE(room.copper_clearance_mm().has_value());
    CHECK(room.copper_clearance_mm().value() == Catch::Approx(0.21));
    CHECK_FALSE(room.track_width_mm().has_value());
    CHECK(layout.board.track_count() == 8U);
    CHECK(layout.board.via_count() == 0U);

    for (const auto &pad : result.pads) {
        INFO("pad " << pad.pad_label);
        REQUIRE(pad.escaped);
        CHECK(pad.failure_reason == volt::BoardEscapeFailureReason::None);
        REQUIRE(pad.tracks.size() == 1U);
        CHECK(pad.vias.empty());
        CHECK(pad.blockers.empty());

        const auto &track = layout.board.track(pad.tracks.front());
        CHECK(track.net() == pad.net.value());
        CHECK(track.layer() == layout.front);
        CHECK(track.width_mm() == Catch::Approx(0.21));
        CHECK(track.points().front() == pad.pad_position);
        CHECK(track.points().back() == pad.endpoint);

        const auto outward = pad.endpoint.x_mm() < 20.0 ? -1.0 : 1.0;
        CHECK(pad.endpoint.x_mm() == Catch::Approx(pad.pad_position.x_mm() + outward));
        CHECK(pad.endpoint.y_mm() == Catch::Approx(pad.pad_position.y_mm()));
    }

    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    for (const auto &pad : result.pads) {
        const auto outward = pad.endpoint.x_mm() < 20.0 ? -1.0 : 1.0;
        const auto route_result = router.connect(volt::BoardRouteRequest{
            pad.net.value(), pad.endpoint,
            volt::BoardPoint{pad.endpoint.x_mm() + (2.0 * outward), pad.endpoint.y_mm()},
            layout.front, layout.front});
        INFO("route from escaped pad " << pad.pad_label);
        CHECK(route_result.routed);
    }

    const auto report = volt::validate_board(layout.board, volt::builtin_footprint_library());
    const auto codes = pcb_drc_codes(report);
    CAPTURE(codes);
    CHECK(codes.empty());
}

TEST_CASE("Escape router resolves stub width from net-class rules", "[pcb][escape]") {
    auto fixture = make_soic_fixture();
    auto wide = volt::NetClass{volt::NetClassName{"Wide"}};
    wide.set_track_width_mm(0.30);
    const auto wide_class =
        fixture.circuit.define_net_class(volt::NetClassSpec{.net_class = std::move(wide)});
    fixture.circuit.update(fixture.nets.front(), volt::AssignNetClass{wide_class});

    auto layout = make_escape_board(fixture);
    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto result = router.escape(fixture.component);

    REQUIRE(result.complete());
    const auto *pad_one = find_pad(result, "1");
    REQUIRE(pad_one != nullptr);
    REQUIRE(pad_one->tracks.size() == 1U);
    CHECK(layout.board.track(pad_one->tracks.front()).width_mm() == Catch::Approx(0.30));
}

TEST_CASE("Generated escape room does not relax stricter net-class clearance", "[pcb][escape]") {
    auto fixture = make_soic_fixture();
    auto strict = volt::NetClass{volt::NetClassName{"Strict"}};
    strict.set_copper_clearance_mm(0.50);
    const auto strict_class =
        fixture.circuit.define_net_class(volt::NetClassSpec{.net_class = std::move(strict)});
    fixture.circuit.update(fixture.nets[0], volt::AssignNetClass{strict_class});

    auto layout = make_escape_board(fixture);
    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto result = router.escape(fixture.component);

    REQUIRE(result.complete());
    REQUIRE(result.room.has_value());
    CHECK_FALSE(layout.board.room(result.room.value()).copper_clearance_mm().has_value());

    static_cast<void>(layout.board.add_track(volt::BoardTrack{
        fixture.nets[0], layout.front,
        std::vector{volt::BoardPoint{19.4, 20.00}, volt::BoardPoint{20.6, 20.00}}, 0.21}));
    static_cast<void>(layout.board.add_track(volt::BoardTrack{
        fixture.nets[1], layout.front,
        std::vector{volt::BoardPoint{19.4, 20.25}, volt::BoardPoint{20.6, 20.25}}, 0.21}));

    const auto report = volt::validate_board(layout.board, volt::builtin_footprint_library());
    auto found_strict_clearance = false;
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code() != volt::DiagnosticCode{"PCB_COPPER_CLEARANCE_VIOLATION"} ||
            !diagnostic.measurement().has_value()) {
            continue;
        }
        if (diagnostic.measurement()->required_mm != Catch::Approx(0.50)) {
            continue;
        }
        CHECK(std::find(diagnostic.entities().begin(), diagnostic.entities().end(),
                        volt::EntityRef::board_room(result.room.value())) ==
              diagnostic.entities().end());
        found_strict_clearance = true;
    }
    CHECK(found_strict_clearance);
}

TEST_CASE("Escape router selects an allowed layer for multi-layer pads", "[pcb][escape]") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "J2",
        .pins = {signal_pin("A", "1"), signal_pin("B", "2")},
    });
    const auto &definition_pins = circuit.get(definition).pins();
    const auto first_pin = definition_pins[0];
    const auto second_pin = definition_pins[1];
    const auto first_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"A"}, volt::NetKind::Signal});
    const auto second_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"B"}, volt::NetKind::Signal});
    const auto component =
        circuit.instantiate_component(definition, volt::ReferenceDesignator{"J1"});
    circuit.connect(first_net,
                    volt::queries::pin_by_definition(circuit, component, first_pin).value());
    circuit.connect(second_net,
                    volt::queries::pin_by_definition(circuit, component, second_pin).value());
    circuit.update(
        component,
        volt::SelectPhysicalPart{volt::PhysicalPart{
            volt::ManufacturerPart{"Generic", "PinHeader_1x02"},
            volt::PackageRef{"1x02"},
            volt::FootprintRef{"connectors", "PinHeader_1x02_P2.54mm_Vertical"},
            std::vector{volt::PinPadMapping{first_pin, "1"}, volt::PinPadMapping{second_pin, "2"}},
        }});

    auto bottom_only = volt::NetClass{volt::NetClassName{"BottomOnly"}};
    bottom_only.set_layer_scope(volt::NetClassLayerScope::BottomOnly);
    const auto class_id =
        circuit.define_net_class(volt::NetClassSpec{.net_class = std::move(bottom_only)});
    circuit.update(first_net, volt::AssignNetClass{class_id});
    circuit.update(second_net, volt::AssignNetClass{class_id});

    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 30.0}));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        component, volt::BoardPoint{15.0, 15.0}, volt::BoardRotation::degrees(0.0)}));

    auto router = volt::BoardRouter{board, volt::builtin_footprint_library()};
    const auto result = router.escape(component);

    REQUIRE(result.complete());
    REQUIRE(result.pads.size() == 2U);
    for (const auto &pad : result.pads) {
        INFO("pad " << pad.pad_label);
        REQUIRE(pad.tracks.size() == 1U);
        CHECK(board.track(pad.tracks.front()).layer() == back);
    }
}

TEST_CASE("Escape router reports blocked pads without hiding partial success", "[pcb][escape]") {
    auto fixture = make_soic_fixture();
    auto layout = make_escape_board(fixture);
    const auto pad_resolutions = layout.board.resolve_pads(volt::builtin_footprint_library());
    const auto pad_one =
        std::find_if(pad_resolutions.begin(), pad_resolutions.end(),
                     [](const volt::PadResolution &pad) { return pad.pad_label() == "1"; });
    REQUIRE(pad_one != pad_resolutions.end());
    static_cast<void>(layout.board.add_keepout(volt::BoardKeepout{
        std::vector{
            volt::BoardPoint{pad_one->position().x_mm() - 0.30, pad_one->position().y_mm() - 0.30},
            volt::BoardPoint{pad_one->position().x_mm() + 0.30, pad_one->position().y_mm() - 0.30},
            volt::BoardPoint{pad_one->position().x_mm() + 0.30, pad_one->position().y_mm() + 0.30},
            volt::BoardPoint{pad_one->position().x_mm() - 0.30, pad_one->position().y_mm() + 0.30}},
        std::vector{layout.front}, std::vector{volt::BoardKeepoutRestriction::Copper}}));

    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto result = router.escape(fixture.component);

    CHECK_FALSE(result.complete());
    REQUIRE(result.room.has_value());
    REQUIRE(result.pads.size() == 8U);
    const auto *blocked = find_pad(result, "1");
    REQUIRE(blocked != nullptr);
    CHECK_FALSE(blocked->escaped);
    CHECK(blocked->failure_reason == volt::BoardEscapeFailureReason::NoLegalCandidate);
    REQUIRE_FALSE(blocked->blockers.empty());
    CHECK(blocked->blockers.front().kind == volt::BoardSpatialBlockerKind::Keepout);
    CHECK(blocked->blockers.front().layer == layout.front);

    const auto escaped_count = static_cast<std::size_t>(
        std::count_if(result.pads.begin(), result.pads.end(),
                      [](const volt::BoardEscapePadResult &pad) { return pad.escaped; }));
    CHECK(escaped_count == 7U);
    CHECK(layout.board.track_count() == escaped_count);
}

TEST_CASE("Escape router reports per-pad unconnected pins without hiding partial success",
          "[pcb][escape]") {
    auto fixture = make_soic_fixture(true);
    auto layout = make_escape_board(fixture);

    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto result = router.escape(fixture.component);

    CHECK_FALSE(result.complete());
    REQUIRE(result.room.has_value());
    REQUIRE(result.pads.size() == 8U);
    const auto *unconnected = find_pad(result, "1");
    REQUIRE(unconnected != nullptr);
    CHECK_FALSE(unconnected->escaped);
    CHECK(unconnected->failure_reason == volt::BoardEscapeFailureReason::PadUnconnected);
    CHECK(unconnected->tracks.empty());
    CHECK(unconnected->blockers.empty());

    const auto escaped_count = static_cast<std::size_t>(
        std::count_if(result.pads.begin(), result.pads.end(),
                      [](const volt::BoardEscapePadResult &pad) { return pad.escaped; }));
    CHECK(escaped_count == 7U);
    CHECK(layout.board.track_count() == escaped_count);
}

TEST_CASE("Escape router reports pads with no copper layer while escaping other pads",
          "[pcb][escape]") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "MixedSide",
        .pins = {signal_pin("A", "1"), signal_pin("B", "2")},
    });
    const auto &definition_pins = circuit.get(definition).pins();
    const auto first_pin = definition_pins[0];
    const auto second_pin = definition_pins[1];
    const auto first_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"A"}, volt::NetKind::Signal});
    const auto second_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"B"}, volt::NetKind::Signal});
    const auto component =
        circuit.instantiate_component(definition, volt::ReferenceDesignator{"U1"});
    circuit.connect(first_net,
                    volt::queries::pin_by_definition(circuit, component, first_pin).value());
    circuit.connect(second_net,
                    volt::queries::pin_by_definition(circuit, component, second_pin).value());
    circuit.update(component, volt::SelectPhysicalPart{volt::PhysicalPart{
                                  volt::ManufacturerPart{"Volt", "MixedSide"},
                                  volt::PackageRef{"MixedSide"},
                                  volt::FootprintRef{"tests", "MixedSide"},
                                  std::vector{volt::PinPadMapping{first_pin, "1"},
                                              volt::PinPadMapping{second_pin, "2"}},
                              }});

    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 30.0}));
    static_cast<void>(board.cache_footprint_definition(volt::FootprintDefinition{
        volt::FootprintRef{"tests", "MixedSide"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-1.0, 0.0},
                volt::FootprintSize{0.8, 0.6}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{1.0, 0.0},
                volt::FootprintSize{0.8, 0.6}, volt::FootprintLayerSet::back_smd()),
        }}));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        component, volt::BoardPoint{15.0, 15.0}, volt::BoardRotation::degrees(0.0)}));

    auto router = volt::BoardRouter{board, volt::builtin_footprint_library()};
    const auto result = router.escape(component);

    CHECK_FALSE(result.complete());
    REQUIRE(result.room.has_value());
    const auto *escaped = find_pad(result, "1");
    REQUIRE(escaped != nullptr);
    CHECK(escaped->escaped);
    CHECK(escaped->failure_reason == volt::BoardEscapeFailureReason::None);
    const auto *no_copper = find_pad(result, "2");
    REQUIRE(no_copper != nullptr);
    CHECK_FALSE(no_copper->escaped);
    CHECK(no_copper->failure_reason == volt::BoardEscapeFailureReason::NoCopperLayer);
    CHECK(no_copper->tracks.empty());
    CHECK(board.track_count() == 1U);
    CHECK(board.track(escaped->tracks.front()).layer() == front);
}

TEST_CASE("Escape router reports pads disallowed by net-class layer scope", "[pcb][escape]") {
    auto fixture = make_soic_fixture();
    auto bottom_only = volt::NetClass{volt::NetClassName{"BottomOnly"}};
    bottom_only.set_layer_scope(volt::NetClassLayerScope::BottomOnly);
    const auto class_id =
        fixture.circuit.define_net_class(volt::NetClassSpec{.net_class = std::move(bottom_only)});
    fixture.circuit.update(fixture.nets.front(), volt::AssignNetClass{class_id});
    auto layout = make_escape_board(fixture);

    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto result = router.escape(fixture.component);

    CHECK_FALSE(result.complete());
    REQUIRE(result.room.has_value());
    const auto *disallowed = find_pad(result, "1");
    REQUIRE(disallowed != nullptr);
    CHECK_FALSE(disallowed->escaped);
    CHECK(disallowed->failure_reason == volt::BoardEscapeFailureReason::DisallowedLayer);
    CHECK(disallowed->tracks.empty());

    const auto escaped_count = static_cast<std::size_t>(
        std::count_if(result.pads.begin(), result.pads.end(),
                      [](const volt::BoardEscapePadResult &pad) { return pad.escaped; }));
    CHECK(escaped_count == 7U);
    CHECK(layout.board.track_count() == escaped_count);
}

TEST_CASE("Escape router rejects component requests that cannot be attempted", "[pcb][escape]") {
    auto fixture = make_soic_fixture();
    auto layout = make_escape_board(fixture);
    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto other = fixture.circuit.instantiate_component(
        fixture.circuit.component(fixture.component).definition(), volt::ReferenceDesignator{"U2"});

    CHECK_THROWS_MATCHES(
        router.escape(other), std::invalid_argument,
        Catch::Matchers::Message("Cannot escape component without a board placement"));
    try {
        static_cast<void>(router.escape(other));
        FAIL("Escape requests without board placement must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidState);
        CHECK(std::string{error.what()} == "Cannot escape component without a board placement");
        REQUIRE(error.entity().has_value());
        CHECK(error.entity()->kind() == volt::EntityKind::Component);
        CHECK(error.entity()->index() == other.index());
    }

    auto no_part_circuit = volt::Circuit{};
    const auto no_part_definition = no_part_circuit.define_component(volt::ComponentSpec{
        .name = "NoPart",
        .pins = {signal_pin("A", "1")},
    });
    const auto pin = no_part_circuit.get(no_part_definition).pins()[0];
    const auto no_part_component =
        no_part_circuit.instantiate_component(no_part_definition, volt::ReferenceDesignator{"U1"});
    auto no_part_board = volt::Board{no_part_circuit};
    static_cast<void>(no_part_board.place_component(volt::ComponentPlacement{
        no_part_component, volt::BoardPoint{0.0, 0.0}, volt::BoardRotation::degrees(0.0)}));
    auto no_part_router = volt::BoardRouter{no_part_board, volt::builtin_footprint_library()};
    CHECK_THROWS_MATCHES(
        no_part_router.escape(no_part_component), std::invalid_argument,
        Catch::Matchers::Message("Cannot escape component without a selected physical part"));

    no_part_circuit.update(
        no_part_component,
        volt::SelectPhysicalPart{volt::PhysicalPart{
            volt::ManufacturerPart{"Volt", "MissingFootprint"}, volt::PackageRef{"Missing"},
            volt::FootprintRef{"tests", "Missing"}, std::vector{volt::PinPadMapping{pin, "1"}}}});
    auto missing_footprint_board = volt::Board{no_part_circuit};
    static_cast<void>(missing_footprint_board.place_component(volt::ComponentPlacement{
        no_part_component, volt::BoardPoint{0.0, 0.0}, volt::BoardRotation::degrees(0.0)}));
    auto missing_footprint_router =
        volt::BoardRouter{missing_footprint_board, volt::builtin_footprint_library()};
    CHECK_THROWS_MATCHES(
        missing_footprint_router.escape(no_part_component), std::invalid_argument,
        Catch::Matchers::Message("Cannot escape component with an unresolved footprint"));
}

TEST_CASE("Escape room and stubs serialize deterministically and round-trip byte-stable",
          "[pcb][escape]") {
    auto fixture = make_soic_fixture();

    const auto escape_once = [&fixture]() {
        auto layout = make_escape_board(fixture);
        auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
        const auto result = router.escape(fixture.component);
        REQUIRE(result.complete());
        return volt::io::write_pcb_board(layout.board, volt::builtin_footprint_library());
    };

    const auto first = escape_once();
    const auto second = escape_once();
    CHECK(first == second);

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, first);
    CHECK(volt::io::write_pcb_board(restored, volt::builtin_footprint_library()) == first);
}

TEST_CASE("Room-sourced escape DRC diagnostics reference the explicit board room",
          "[pcb][escape]") {
    auto fixture = make_soic_fixture();
    auto layout = make_escape_board(fixture);
    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto result = router.escape(fixture.component);
    REQUIRE(result.complete());
    REQUIRE(result.room.has_value());

    static_cast<void>(layout.board.add_track(volt::BoardTrack{
        fixture.nets[0], layout.front,
        std::vector{volt::BoardPoint{19.4, 20.00}, volt::BoardPoint{20.6, 20.00}}, 0.21}));
    static_cast<void>(layout.board.add_track(volt::BoardTrack{
        fixture.nets[1], layout.front,
        std::vector{volt::BoardPoint{19.4, 20.25}, volt::BoardPoint{20.6, 20.25}}, 0.21}));

    const auto report = volt::validate_board(layout.board, volt::builtin_footprint_library());
    auto found_room_diagnostic = false;
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code() != volt::DiagnosticCode{"PCB_COPPER_CLEARANCE_VIOLATION"}) {
            continue;
        }
        if (std::find(diagnostic.entities().begin(), diagnostic.entities().end(),
                      volt::EntityRef::board_room(result.room.value())) !=
            diagnostic.entities().end()) {
            found_room_diagnostic = true;
        }
    }
    CHECK(found_room_diagnostic);
}
