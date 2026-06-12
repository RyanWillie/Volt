#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/queries.hpp>
#include <volt/io/pcb_reader.hpp>
#include <volt/io/pcb_writer.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/board_router.hpp>
#include <volt/pcb/footprints.hpp>

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

[[nodiscard]] SoicFixture make_soic_fixture() {
    auto circuit = volt::Circuit{};
    auto pins = std::vector<volt::PinDefId>{};
    auto nets = std::vector<volt::NetId>{};
    pins.reserve(8U);
    nets.reserve(8U);
    for (std::size_t index = 0; index < 8U; ++index) {
        const auto number = std::to_string(index + 1U);
        pins.push_back(circuit.add_pin_definition(volt::PinDefinition{
            "P" + number, number, volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Bidirectional,
            volt::ElectricalSignalDomain::Unspecified, volt::ElectricalDriveKind::Passive}));
        nets.push_back(
            circuit.add_net(volt::Net{volt::NetName{"N" + number}, volt::NetKind::Signal}));
    }

    const auto definition =
        circuit.add_component_definition(volt::ComponentDefinition{"TLC555", pins});
    const auto component =
        circuit.instantiate_component(definition, volt::ReferenceDesignator{"U1"});

    auto mappings = std::vector<volt::PinPadMapping>{};
    mappings.reserve(pins.size());
    for (std::size_t index = 0; index < pins.size(); ++index) {
        mappings.emplace_back(pins[index], std::to_string(index + 1U));
        const auto pin = volt::queries::pin_by_definition(circuit, component, pins[index]).value();
        circuit.connect(nets[index], pin);
    }

    circuit.select_physical_part(component,
                                 volt::PhysicalPart{
                                     volt::ManufacturerPart{"Texas Instruments", "TLC555CDR"},
                                     volt::PackageRef{"SOIC-8"},
                                     volt::FootprintRef{"ics", "SOIC-8_3.9x4.9mm_P1.27mm"},
                                     std::move(mappings),
                                 });

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
    CHECK(layout.board.room(result.room.value()).name() == "escape-U1-at-20.000-20.000");
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
    const auto wide_class = fixture.circuit.add_net_class(std::move(wide));
    REQUIRE(fixture.circuit.assign_net_class(fixture.nets.front(), wide_class));

    auto layout = make_escape_board(fixture);
    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto result = router.escape(fixture.component);

    REQUIRE(result.complete());
    const auto *pad_one = find_pad(result, "1");
    REQUIRE(pad_one != nullptr);
    REQUIRE(pad_one->tracks.size() == 1U);
    CHECK(layout.board.track(pad_one->tracks.front()).width_mm() == Catch::Approx(0.30));
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
