#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include <volt/circuit/bom.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/io/bom_writer.hpp>

namespace {

struct BomCircuit {
    volt::Circuit circuit;
    volt::ComponentId r1;
    volt::ComponentId r2;
    volt::ComponentId r3;
};

volt::ComponentId add_resistor(volt::Circuit &circuit, const std::string &reference,
                               const std::string &mpn, std::vector<std::string> alternates = {}) {
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{reference});
    circuit.select_physical_part(component, volt::PhysicalPart{
                                                volt::ManufacturerPart{"Yageo", mpn},
                                                volt::PackageRef{"0603"},
                                                volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                                std::vector{volt::PinPadMapping{first_pin, "1"},
                                                            volt::PinPadMapping{second_pin, "2"}},
                                                {},
                                                std::nullopt,
                                                std::move(alternates),
                                            });
    return component;
}

BomCircuit build_bom_circuit() {
    auto circuit = volt::Circuit{};
    const auto r1 = add_resistor(circuit, "R1", "RC0603FR-07330RL", {"RC0603FR-07330RLA"});
    const auto r2 = add_resistor(circuit, "R2", "RC0603FR-07330RL", {"RC0603FR-07330RLA"});
    const auto r3 = add_resistor(circuit, "R3", "RC0603FR-071KL", {"RC0603FR-071KLA"});
    circuit.set_component_dnp(r1, false);
    circuit.set_component_dnp(r2, false);
    circuit.set_component_selection_override(r2, true);
    circuit.set_component_dnp(r3, true);
    return BomCircuit{std::move(circuit), r1, r2, r3};
}

} // namespace

TEST_CASE("BOM projection groups populated parts and keeps DNP rows visible") {
    auto test = build_bom_circuit();
    auto sourcing = volt::BomSourcingSnapshot{};
    sourcing.set_mpn_properties(
        "RC0603FR-07330RL", volt::PropertyMap{
                                {volt::PropertyKey{"sku"}, volt::PropertyValue{"311-330HRCT-ND"}},
                                {volt::PropertyKey{"supplier"}, volt::PropertyValue{"Digi-Key"}},
                            });

    const auto bom = volt::project_bom(test.circuit, sourcing);

    REQUIRE(bom.lines().size() == 2);
    CHECK(bom.lines()[0].mpn() == "RC0603FR-071KL");
    CHECK(bom.lines()[0].dnp());
    CHECK(bom.lines()[0].quantity() == 0);
    CHECK(bom.lines()[0].references() == std::vector<std::string>{"R3"});
    CHECK(bom.lines()[0].approved_alternate_mpns() == std::vector<std::string>{"RC0603FR-071KLA"});

    CHECK(bom.lines()[1].mpn() == "RC0603FR-07330RL");
    CHECK_FALSE(bom.lines()[1].dnp());
    CHECK(bom.lines()[1].quantity() == 2);
    CHECK(bom.lines()[1].references() == std::vector<std::string>{"R1", "R2"});
    CHECK(bom.lines()[1].selection_override_references() == std::vector<std::string>{"R2"});
    CHECK(bom.lines()[1].sourcing().get(volt::PropertyKey{"supplier"}).as_string() == "Digi-Key");

    const auto payload = nlohmann::json::parse(volt::io::write_bom_json(bom));
    CHECK(payload["format"] == "volt.bom");
    CHECK(payload["lines"][1]["references"] == nlohmann::json::array({"R1", "R2"}));
    CHECK(payload["lines"][1]["approved_alternate_mpns"] ==
          nlohmann::json::array({"RC0603FR-07330RLA"}));
    CHECK(payload["lines"][1]["selection_override_references"] == nlohmann::json::array({"R2"}));
    CHECK(payload["lines"][1]["sourcing"] ==
          nlohmann::json({{"sku", "311-330HRCT-ND"}, {"supplier", "Digi-Key"}}));
    CHECK(payload["components"][2]["dnp"] == true);
}

TEST_CASE("BOM CSV output is deterministic and includes alternates and sourcing") {
    auto test = build_bom_circuit();
    auto sourcing = volt::BomSourcingSnapshot{};
    sourcing.set_mpn_properties(
        "RC0603FR-07330RL", volt::PropertyMap{
                                {volt::PropertyKey{"sku"}, volt::PropertyValue{"311-330HRCT-ND"}},
                                {volt::PropertyKey{"supplier"}, volt::PropertyValue{"Digi-Key"}},
                            });

    const auto csv = volt::io::write_bom_csv(volt::project_bom(test.circuit, sourcing));

    CHECK(csv == "manufacturer,mpn,package,quantity,references,dnp,approved_alternate_mpns,"
                 "selection_override_references,sourcing.sku,sourcing.supplier\n"
                 "Yageo,RC0603FR-071KL,0603,0,R3,true,RC0603FR-071KLA,,,\n"
                 "Yageo,RC0603FR-07330RL,0603,2,R1 R2,false,RC0603FR-07330RLA,R2,"
                 "311-330HRCT-ND,Digi-Key\n");
}

TEST_CASE("BOM readiness reports stable diagnostics on offending instances") {
    volt::Circuit circuit;
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto missing_part =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});
    const auto missing_dnp =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R2"});
    const auto bad_alternate =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R3"});

    circuit.set_component_dnp(missing_part, false);
    circuit.select_physical_part(
        missing_dnp, volt::PhysicalPart{volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                        volt::PackageRef{"0603"},
                                        volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                        std::vector{volt::PinPadMapping{first_pin, "1"},
                                                    volt::PinPadMapping{second_pin, "2"}}});
    circuit.set_component_dnp(bad_alternate, false);
    circuit.select_physical_part(
        bad_alternate,
        volt::PhysicalPart{
            volt::ManufacturerPart{"Yageo", "RC0603FR-071KL"},
            volt::PackageRef{"0603"},
            volt::FootprintRef{"passives", "R_0603_1608Metric"},
            std::vector{volt::PinPadMapping{first_pin, "1"}, volt::PinPadMapping{second_pin, "2"}},
            {},
            std::nullopt,
            std::vector<std::string>{"RC0603FR-071KL"},
        });

    const auto report = volt::validate_bom_readiness(circuit);

    REQUIRE(report.count() == 3);
    CHECK(report.diagnostics()[0].code().value() == "BOM_COMPONENT_MISSING_SELECTED_PART");
    CHECK(report.diagnostics()[0].entities() ==
          std::vector{volt::EntityRef::component(missing_part),
                      volt::EntityRef::component_def(component_def)});
    CHECK(report.diagnostics()[1].code().value() == "BOM_COMPONENT_IMPLICIT_DNP");
    CHECK(report.diagnostics()[1].entities() ==
          std::vector{volt::EntityRef::component(missing_dnp),
                      volt::EntityRef::component_def(component_def)});
    CHECK(report.diagnostics()[2].code().value() == "BOM_APPROVED_ALTERNATE_INCOMPATIBLE");
    CHECK(report.diagnostics()[2].entities() ==
          std::vector{volt::EntityRef::component(bad_alternate),
                      volt::EntityRef::component_def(component_def)});
    CHECK(report.diagnostics()[2].category() == volt::DiagnosticCategory{"bom"});
}
