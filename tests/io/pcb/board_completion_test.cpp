#include "../../support/board_completion_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>
#include <ranges>
#include <volt/io/pcb/pcb_fabrication_writer.hpp>
#include <volt/pcb/compiled/board_consumers.hpp>

namespace {
using namespace volt;
namespace codes = pcb_completion_diagnostic_codes;

[[nodiscard]] bool has(const BoardCompletion &result, std::string_view code) {
    return std::ranges::any_of(result.diagnostics().diagnostics(),
                               [&](const auto &d) { return d.code().value() == code; });
}

[[nodiscard]] BoardCompletionState row(const BoardCompletion &result, BoardCompletionCheck check) {
    return result.coverage()[static_cast<std::size_t>(check)].state;
}

[[nodiscard]] nlohmann::json payload(const BoardCompletion &result) {
    return nlohmann::json::parse(io::write_board_completion(result));
}
} // namespace

TEST_CASE("Completion proves supported Solid contacts with exact source-free read-only evidence",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.multi_pad = GENERATE(false, true);
    options.modules = GENERATE(false, true);
    options.via = true;
    auto artifact = test::completion_fixture(options);
    const auto before = std::string{artifact.bytes()};
    const auto logical = std::string{artifact.logical_dependency_snapshot()};
    const auto physical = std::string{artifact.physical_snapshot()};
    const auto core = evaluate_board_completion(artifact);
    CHECK(core.status() == BoardCompletionStatus::NotEvaluated);
    CHECK(row(core, BoardCompletionCheck::ExportFidelity) == BoardCompletionState::NotEvaluated);
    const auto result = io::evaluate_fabrication_completion(artifact);
    INFO(io::write_board_completion(result));
    CHECK(result.status() == BoardCompletionStatus::Complete);
    CHECK(result.components().size() == 2);
    CHECK(result.pins().size() == 2);
    for (const auto &pin : result.pins())
        CHECK(pin.copper.size() == (options.multi_pad ? 2U : 1U));
    const auto bytes = io::write_board_completion(result);
    CHECK(io::write_board_completion(io::evaluate_fabrication_completion(artifact)) == bytes);
    const auto reopened = io::open_compiled_board(before);
    CHECK(io::write_board_completion(io::evaluate_fabrication_completion(reopened)) == bytes);
    CHECK(std::string{artifact.bytes()} == before);
    CHECK(std::string{artifact.logical_dependency_snapshot()} == logical);
    CHECK(std::string{artifact.physical_snapshot()} == physical);
    const auto exported = io::write_pcb_fabrication_files(artifact);
    REQUIRE(result.files().size() == exported.files.size());
    for (std::size_t i = 0; i < exported.files.size(); ++i)
        CHECK(result.files()[i].digest == sha256_content_hash(exported.files[i].text));
    const auto front =
        std::ranges::find(exported.files, "copper-top", &io::PcbFabricationFile::function);
    REQUIRE(front != exported.files.end());
    CHECK(front->text.find("G36*") != std::string::npos);
    CHECK(front->text.find("X0005000000Y0015000000D02*") != std::string::npos);
    CHECK(front->text.find("X0025000000Y0005000000D01*") != std::string::npos);
    CHECK(front->text.find("C,0.800000") != std::string::npos);
}

TEST_CASE("Completion retains every required component and terminal when last or all placements "
          "disappear",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.placements = GENERATE(0U, 1U);
    const auto artifact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(artifact);
    CHECK(result.status() == BoardCompletionStatus::Incomplete);
    CHECK(result.components().size() == 2);
    CHECK(result.pins().size() == 2);
    CHECK(has(result, codes::MissingPlacement));
    CHECK(has(result, codes::MissingContact));
    CHECK(row(result, BoardCompletionCheck::Inventory) == BoardCompletionState::Failed);
}

TEST_CASE("Completion singleton requires its endpoint but no invented route", "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.components = 1;
    options.zone = false;
    const auto singleton = test::completion_fixture(options);
    CHECK(io::evaluate_fabrication_completion(singleton).status() ==
          BoardCompletionStatus::Complete);
    options.placements = 0;
    const auto missing = test::completion_fixture(options);
    CHECK(has(io::evaluate_fabrication_completion(missing), codes::MissingContact));
}

TEST_CASE("Completion distinguishes selection logical requirement and optional-connected endpoints",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    SECTION("missing selection") { options.selected = false; }
    SECTION("required unconnected") {
        options.connected = false;
        options.zone = false;
    }
    SECTION("optional connected endpoint") {
        options.requirement = ConnectionRequirement::Optional;
        options.placements = 1;
    }
    const auto artifact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(artifact);
    CHECK(result.status() == BoardCompletionStatus::Incomplete);
    if (!options.selected) {
        CHECK(has(result, codes::MissingImplementation));
        CHECK(has(result, codes::MissingMapping));
    } else if (!options.connected)
        CHECK(has(result, codes::RequiredLogicalConnection));
    else
        CHECK(has(result, codes::MissingContact));
}

TEST_CASE(
    "Completion DNP exemption is limited to absent terminals and never suppresses placed copper",
    "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.second_dnp = true;
    options.placements = 1;
    const auto absent = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(absent);
    CHECK(result.status() == BoardCompletionStatus::Complete);
    CHECK_FALSE(result.pins()[1].physical_required);
    options.placements = 2;
    options.different_nets = true;
    const auto placed = test::completion_fixture(options);
    const auto shorted = io::evaluate_fabrication_completion(placed);
    CHECK(shorted.pins()[1].physical_required);
    CHECK(has(shorted, codes::CopperShort));
    options.placements = 0;
    const auto both_absent = test::completion_fixture(options);
    CHECK(has(io::evaluate_fabrication_completion(both_absent), codes::MissingPlacement));
}

TEST_CASE("Completion no-connect precedence contradiction and isolated copper contact are distinct",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.components = 1;
    options.zone = false;
    SECTION("explicit no-connect overrides required") { options.explicit_nc = true; }
    SECTION("intrinsic terminal no-connect") {
        options.terminal = ElectricalTerminalKind::NoConnect;
    }
    SECTION("must not connect") { options.requirement = ConnectionRequirement::MustNotConnect; }
    SECTION("optional disconnected") { options.requirement = ConnectionRequirement::Optional; }
    options.connected = false;
    const auto isolated = test::completion_fixture(options);
    CHECK(io::evaluate_fabrication_completion(isolated).status() ==
          BoardCompletionStatus::Complete);
    options.nc_contact = true;
    const auto contact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(contact);
    CHECK(has(result, codes::IsolatedContact));
    CHECK_FALSE(result.copper().back().net.has_value());
    if (options.requirement != ConnectionRequirement::Optional) {
        options.connected = true;
        const auto contradiction = test::completion_fixture(options);
        CHECK(
            has(io::evaluate_fabrication_completion(contradiction), codes::NoConnectContradiction));
    }
}

TEST_CASE("Completion follows physical layers via spans and planar zone layers", "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.interlayer = true;
    const auto disconnected = test::completion_fixture(options);
    CHECK(has(io::evaluate_fabrication_completion(disconnected), codes::DisconnectedGroup));
    options.via = true;
    const auto joined = test::completion_fixture(options);
    INFO(io::write_board_completion(io::evaluate_fabrication_completion(joined)));
    CHECK(io::evaluate_fabrication_completion(joined).status() == BoardCompletionStatus::Complete);
    options.bad_span = true;
    const auto unsupported = test::completion_fixture(options);
    CHECK(io::evaluate_fabrication_completion(unsupported).status() !=
          BoardCompletionStatus::Complete);
}

TEST_CASE(
    "Completion fails removed shrunk and crossed Solid zones with explicit boundary uncertainty",
    "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    SECTION("removed") { options.zone = false; }
    SECTION("shrunk") { options.shrink = true; }
    SECTION("outside") { options.second_x = 25.500001; }
    SECTION("tangent") { options.second_x = 25.5; }
    SECTION("off-grid boundary") { options.zone_right = 25.0000004; }
    const auto artifact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(artifact);
    CHECK(result.status() != BoardCompletionStatus::Complete);
    if (options.second_x == 25.5 || options.zone_right != 25.0)
        CHECK(row(result, BoardCompletionCheck::Geometry) == BoardCompletionState::NotEvaluated);
    else
        CHECK(has(result, codes::DisconnectedGroup));
}

TEST_CASE("Completion detects exact short islands through labelled and netless zones and pads",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.different_nets = true;
    SECTION("labelled zone") {}
    SECTION("netless zone") { options.netless_zone = true; }
    SECTION("netless mechanical copper pad") {
        options.zone = false;
        options.mechanical_bridge = true;
    }
    SECTION("lossy emitted rectangle") { options.shape = FootprintPadShape::RoundedRectangle; }
    const auto artifact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(artifact);
    INFO(io::write_board_completion(result));
    CHECK(has(result, codes::CopperShort));
    CHECK(result.status() == BoardCompletionStatus::Incomplete);
    const auto shorts = std::ranges::count_if(
        result.islands(), [](const auto &island) { return island.logical_groups.size() > 1; });
    CHECK(shorts >= 1);
    if (options.netless_zone || options.mechanical_bridge)
        CHECK(std::ranges::any_of(result.copper(), [](const auto &item) { return !item.net; }));
}

TEST_CASE("Completion unsupported shapes lossy export and quantization never become complete",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    SECTION("oval") { options.shape = FootprintPadShape::Oval; }
    SECTION("rounded rectangle") { options.shape = FootprintPadShape::RoundedRectangle; }
    SECTION("copper rounding") { options.second_x = 20.0000004; }
    SECTION("actual export transform rounding") { options.outline_shift = 0.0000002; }
    SECTION("drill rounding") {
        options.via = true;
        options.drill_off_grid = true;
    }
    const auto artifact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(artifact);
    CHECK(result.status() != BoardCompletionStatus::Complete);
    CHECK(
        (has(result, codes::GeometryNotEvaluated) ||
         row(result, BoardCompletionCheck::ExportFidelity) == BoardCompletionState::NotEvaluated));
}

TEST_CASE("Completion does not certify contact inside drills or copper severed by openings",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    SECTION("inside drill") {
        options.zone = false;
        options.drill_only = true;
    }
    SECTION("severing opening") { options.severing_hole = true; }
    const auto artifact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(artifact);
    CHECK(result.status() != BoardCompletionStatus::Complete);
    CHECK(row(result, BoardCompletionCheck::Geometry) != BoardCompletionState::Pass);
    if (options.drill_only)
        CHECK_FALSE(has(result, codes::CopperShort));
}

TEST_CASE("Completion diagnostic catalog matches public documented finite codes", "[completion]") {
    CHECK(diagnostic_code_catalogs::PcbCompletion.size() == 12);
    const auto artifact = test::completion_fixture();
    const auto result = payload(io::evaluate_fabrication_completion(artifact));
    CHECK(result["format"] == "volt.board-completion");
    CHECK(result["schema_version"] == 1);
    CHECK(result["coverage"].size() == 7);
    CHECK(result["counts"]["required_contacts"] == 2);
}

TEST_CASE("Completion proves track contacts and plated terminals but not NPTH interlayer links",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    SECTION("tracks") {
        options.zone = false;
        options.tracks = true;
    }
    SECTION("plated through-hole terminals") {
        options.interlayer = true;
        options.pth_terminal = true;
    }
    const auto artifact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(artifact);
    INFO(io::write_board_completion(result));
    CHECK(result.status() == BoardCompletionStatus::Complete);
    if (options.pth_terminal) {
        const auto files = io::write_pcb_fabrication_files(artifact).files;
        const auto drill =
            std::ranges::find(files, "drill-plated", &io::PcbFabricationFile::function);
        REQUIRE(drill != files.end());
        CHECK(drill->text.find("C0.300000") != std::string::npos);
        CHECK(result.copper().back().layers.size() == 2);
    }
}

TEST_CASE("Completion NPTH mechanical holes never invent an interlayer connection",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.interlayer = true;
    options.npth_hole = true;
    const auto artifact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(artifact);
    CHECK(has(result, codes::DisconnectedGroup));
    CHECK(result.copper().size() == 4);
    const auto files = io::write_pcb_fabrication_files(artifact).files;
    const auto drill =
        std::ranges::find(files, "drill-non-plated", &io::PcbFabricationFile::function);
    REQUIRE(drill != files.end());
    CHECK(drill->text.find("C0.300000") != std::string::npos);
}

TEST_CASE("Completion exact missing and foreign mappings fail at native structural boundaries",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    SECTION("absent mapping") { options.omit_mapping = true; }
    SECTION("absent footprint pad") { options.wrong_mapping = true; }
    CHECK_THROWS_AS(test::completion_fixture(options), KernelError);
}

TEST_CASE("Completion retains fabrication-critical loss as explicit missing fidelity",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.fab_critical_slot = true;
    const auto artifact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(artifact);
    CHECK(result.status() != BoardCompletionStatus::Complete);
    CHECK(row(result, BoardCompletionCheck::ExportFidelity) == BoardCompletionState::NotEvaluated);
    CHECK(has(result, pcb_fabrication_diagnostic_codes::NativeFabUnsupportedGeometry));
}

TEST_CASE("Completion netless Solid may implement one group without assigning logical labels",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.netless_zone = true;
    const auto artifact = test::completion_fixture(options);
    const auto before = std::string{artifact.physical_snapshot()};
    const auto result = io::evaluate_fabrication_completion(artifact);
    CHECK(result.status() == BoardCompletionStatus::Complete);
    CHECK_FALSE(result.copper().front().net.has_value());
    CHECK(std::string{artifact.physical_snapshot()} == before);
}

TEST_CASE("Completion unplaced DNP missing selection stays outside physical scope",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.second_dnp = true;
    options.second_selected = false;
    options.placements = 1;
    const auto artifact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(artifact);
    CHECK(result.status() == BoardCompletionStatus::Complete);
    CHECK_FALSE(result.components()[1].selection.has_value());
    CHECK_FALSE(result.pins()[1].physical_required);
    CHECK(has(result, "PCB_COMPONENT_MISSING_SELECTED_PART"));
    options.placements = 2;
    const auto placed = test::completion_fixture(options);
    CHECK(has(io::evaluate_fabrication_completion(placed), codes::MissingImplementation));
}

TEST_CASE("Completion bounds native 4.6 coordinates before unsafe integer conversion",
          "[completion]") {
    auto options = test::CompletionFixtureOptions{};
    options.components = 1;
    options.zone = false;
    options.first_x = GENERATE(10010.0, 1.0e14);
    options.outline_x = options.first_x - 10.0;
    const auto artifact = test::completion_fixture(options);
    const auto result = io::evaluate_fabrication_completion(artifact);
    CHECK(result.status() == BoardCompletionStatus::NotEvaluated);
    CHECK(row(result, BoardCompletionCheck::ExportFidelity) == BoardCompletionState::NotEvaluated);
    CHECK(has(result, codes::ExportNotEvaluated));
    CHECK(result.files().empty());
}
