#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include <volt/io/assembly/cpl_writer.hpp>
#include <volt/pcb/assembly/cpl.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace {

struct CplCircuit {
    volt::Circuit circuit;
    volt::ComponentId r1;
    volt::ComponentId r2;
    volt::ComponentId r3;
    volt::ComponentId r4;
};

[[nodiscard]] volt::ComponentId add_resistor(volt::Circuit &circuit, const std::string &reference,
                                             const std::string &mpn) {
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", {first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(definition, volt::ReferenceDesignator{reference});
    circuit.select_physical_part(
        component,
        volt::PhysicalPart{volt::ManufacturerPart{"Yageo", mpn}, volt::PackageRef{"0603"},
                           volt::FootprintRef{"passives", "R_0603_1608Metric"},
                           std::vector{volt::PinPadMapping{first_pin, "1"},
                                       volt::PinPadMapping{second_pin, "2"}}});
    return component;
}

[[nodiscard]] volt::ComponentId add_unselected_resistor(volt::Circuit &circuit,
                                                        const std::string &reference) {
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", {first_pin, second_pin}});
    return circuit.instantiate_component(definition, volt::ReferenceDesignator{reference});
}

[[nodiscard]] CplCircuit make_cpl_circuit() {
    auto circuit = volt::Circuit{};
    auto r1 = add_resistor(circuit, "R1", "RC0603FR-07330RL");
    auto r2 = add_resistor(circuit, "R2", "RC0603FR-07330RL");
    auto r3 = add_resistor(circuit, "R3", "RC0603FR-071KL");
    auto r4 = add_resistor(circuit, "R4", "RC0603FR-07470RL");
    circuit.set_component_dnp(r1, false);
    circuit.set_component_dnp(r2, false);
    circuit.set_component_dnp(r3, true);
    circuit.set_component_dnp(r4, false);
    return CplCircuit{std::move(circuit), r1, r2, r3, r4};
}

[[nodiscard]] std::vector<std::string> diagnostic_codes(const volt::DiagnosticReport &report) {
    auto codes = std::vector<std::string>{};
    codes.reserve(report.count());
    for (const auto &diagnostic : report.diagnostics()) {
        codes.push_back(diagnostic.code().value());
    }
    return codes;
}

} // namespace

TEST_CASE("CPL projection writes deterministic JSON and JLCPCB CSV with placement conventions") {
    auto fixture = make_cpl_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Assembly"}};
    static_cast<void>(board.cache_footprint_definition(volt::passive_0603_footprint()));
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{fixture.r2, volt::BoardPoint{20.5, 11.0},
                                 volt::BoardRotation::degrees(270.0), volt::BoardSide::Bottom}));
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{fixture.r1, volt::BoardPoint{10.0, 5.25},
                                 volt::BoardRotation::degrees(90.0), volt::BoardSide::Top}));
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{fixture.r3, volt::BoardPoint{4.0, 4.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top}));

    auto options = volt::CplProjectionOptions{};
    options.rotation_offsets.push_back(
        volt::CplRotationOffset{volt::FootprintRef{"passives", "R_0603_1608Metric"}, 10.0});

    const auto cpl = volt::project_cpl(board, volt::builtin_footprint_library(), options);

    CHECK(cpl.diagnostics().count() == 1);
    CHECK(cpl.diagnostics().diagnostics()[0].code() ==
          volt::DiagnosticCode{std::string{volt::assembly_diagnostic_codes::ComponentUnplaced}});
    REQUIRE(cpl.rows().size() == 2);
    CHECK(cpl.rows()[0].reference() == "R1");
    CHECK(cpl.rows()[0].placement() == volt::ComponentPlacementId{1});
    CHECK(cpl.rows()[0].side() == volt::BoardSide::Top);
    CHECK(cpl.rows()[0].position() == volt::BoardPoint{10.0, 5.25});
    CHECK(cpl.rows()[0].authored_rotation_deg() == 90.0);
    CHECK(cpl.rows()[0].rotation_offset_deg() == 10.0);
    CHECK(cpl.rows()[0].rotation_deg() == 100.0);
    REQUIRE(cpl.rows()[0].part_identity().has_value());
    CHECK(cpl.rows()[0].part_identity()->manufacturer() == "Yageo");
    CHECK(cpl.rows()[0].part_identity()->mpn() == "RC0603FR-07330RL");
    CHECK(cpl.rows()[0].footprint().has_value());
    CHECK(cpl.rows()[0].footprint()->name() == "R_0603_1608Metric");
    CHECK(cpl.rows()[1].reference() == "R2");
    CHECK(cpl.rows()[1].rotation_deg() == 280.0);

    const auto first_json = volt::io::write_cpl_json(cpl);
    const auto second_json = volt::io::write_cpl_json(cpl);
    CHECK(first_json == second_json);
    const auto payload = nlohmann::json::parse(first_json);
    CHECK(payload["format"] == "volt.cpl");
    CHECK(payload["version"] == 1);
    CHECK(payload["metadata"]["units"] == "mm");
    CHECK(payload["metadata"]["origin"]["convention"] == "board_origin");
    CHECK(payload["metadata"]["rotation"]["includes_rotation_offsets"] == true);
    CHECK(payload["rows"][0]["designator"] == "R1");
    CHECK(payload["rows"][0]["component"] == "component:0");
    CHECK(payload["rows"][0]["placement"] == "component_placement:1");
    CHECK(payload["rows"][0]["footprint"] ==
          nlohmann::json({{"library", "passives"}, {"name", "R_0603_1608Metric"}}));
    CHECK(payload["rows"][0]["side"] == "top");
    CHECK(payload["rows"][0]["position_mm"] == nlohmann::json::array({10.0, 5.25}));
    CHECK(payload["rows"][0]["authored_rotation_deg"] == 90.0);
    CHECK(payload["rows"][0]["rotation_offset_deg"] == 10.0);
    CHECK(payload["rows"][0]["rotation_deg"] == 100.0);
    CHECK(payload["rows"][0]["part"] ==
          nlohmann::json(
              {{"manufacturer", "Yageo"}, {"mpn", "RC0603FR-07330RL"}, {"package", "0603"}}));
    CHECK(payload["diagnostics"][0]["code"] == "ASSEMBLY_COMPONENT_UNPLACED");

    CHECK(volt::io::write_cpl_csv(cpl) == "Designator,Mid X,Mid Y,Layer,Rotation\n"
                                          "R1,10,5.25,Top,100\n"
                                          "R2,20.5,11,Bottom,280\n");
}

TEST_CASE("CPL projection reports missing assembly data and skips DNP components") {
    auto circuit = volt::Circuit{};
    const auto missing_selected = add_unselected_resistor(circuit, "R0");
    const auto unplaced = add_resistor(circuit, "R1", "RC0603FR-07330RL");
    const auto dnp = add_resistor(circuit, "R2", "RC0603FR-071KL");
    circuit.set_component_dnp(missing_selected, false);
    circuit.set_component_dnp(unplaced, false);
    circuit.set_component_dnp(dnp, true);
    auto board = volt::Board{circuit};
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        missing_selected, volt::BoardPoint{1.0, 2.0}, volt::BoardRotation::degrees(0.0)}));

    const auto cpl = volt::project_cpl(board, volt::builtin_footprint_library());

    REQUIRE(cpl.rows().size() == 1);
    CHECK(cpl.rows()[0].reference() == "R0");
    CHECK_FALSE(cpl.rows()[0].footprint().has_value());
    CHECK_FALSE(cpl.rows()[0].part_identity().has_value());
    CHECK(diagnostic_codes(cpl.diagnostics()) ==
          std::vector<std::string>{"ASSEMBLY_COMPONENT_MISSING_SELECTED_PART",
                                   "ASSEMBLY_PART_IDENTITY_MISSING",
                                   "ASSEMBLY_COMPONENT_UNPLACED"});
    CHECK(cpl.diagnostics().diagnostics()[0].category() ==
          volt::DiagnosticCategory{std::string{volt::diagnostic_categories::Assembly}});
}

TEST_CASE("CPL projection diagnoses ambiguous rotation-offset data") {
    auto fixture = make_cpl_circuit();
    auto board = volt::Board{fixture.circuit};
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{fixture.r1, volt::BoardPoint{1.0, 2.0},
                                 volt::BoardRotation::degrees(45.0), volt::BoardSide::Top}));
    auto options = volt::CplProjectionOptions{};
    options.rotation_offsets.push_back(
        volt::CplRotationOffset{volt::FootprintRef{"passives", "R_0603_1608Metric"}, 0.0});
    options.rotation_offsets.push_back(
        volt::CplRotationOffset{volt::FootprintRef{"passives", "R_0603_1608Metric"}, 90.0});

    const auto cpl = volt::project_cpl(board, volt::builtin_footprint_library(), options);

    REQUIRE(cpl.rows().size() == 1);
    CHECK(cpl.rows()[0].rotation_offset_deg() == 0.0);
    CHECK(diagnostic_codes(cpl.diagnostics()) ==
          std::vector<std::string>{"ASSEMBLY_COMPONENT_UNPLACED", "ASSEMBLY_COMPONENT_UNPLACED",
                                   "ASSEMBLY_ORIENTATION_AMBIGUOUS"});
}

TEST_CASE("Assembly diagnostic codes are stable catalog entries") {
    CHECK(std::string{volt::diagnostic_categories::Assembly} == "assembly");
    CHECK(std::vector<std::string>{volt::diagnostic_code_catalogs::Assembly.begin(),
                                   volt::diagnostic_code_catalogs::Assembly.end()} ==
          std::vector<std::string>{"ASSEMBLY_COMPONENT_MISSING_SELECTED_PART",
                                   "ASSEMBLY_PART_IDENTITY_MISSING", "ASSEMBLY_COMPONENT_UNPLACED",
                                   "ASSEMBLY_ORIENTATION_AMBIGUOUS"});
}
