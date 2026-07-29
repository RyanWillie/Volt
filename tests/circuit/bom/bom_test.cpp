#include <catch2/catch_test_macros.hpp>

#include "support/circuit_test_helpers.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <volt/circuit/bom/bom.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/bom/bom_writer.hpp>
#include <volt/library/part_library.hpp>

namespace {

constexpr auto footprint_bytes = std::string_view{"bom-native-part-footprint"};

class BomAssetResolver final : public volt::PartAssetResolver {
  public:
    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &) const override {
        return std::string{footprint_bytes};
    }
};

struct BomCircuit {
    volt::Circuit circuit;
    volt::PartLibrary library;
    volt::ComponentId r1;
    volt::ComponentId r2;
    volt::ComponentId r3;
};

[[nodiscard]] volt::ComponentDefId define_resistor(volt::Circuit &circuit) {
    const auto first_pin_spec = volt::PinSpec{"1",
                                              "1",
                                              volt::ConnectionRequirement::Required,
                                              volt::ElectricalTerminalKind::Passive,
                                              volt::ElectricalDirection::Passive,
                                              volt::ElectricalSignalDomain::Unspecified,
                                              volt::ElectricalDriveKind::Passive};
    const auto second_pin_spec = volt::PinSpec{"2",
                                               "2",
                                               volt::ConnectionRequirement::Required,
                                               volt::ElectricalTerminalKind::Passive,
                                               volt::ElectricalDirection::Passive,
                                               volt::ElectricalSignalDomain::Unspecified,
                                               volt::ElectricalDriveKind::Passive};
    return volt::test::define_component(circuit, "Resistor",
                                        std::vector{first_pin_spec, second_pin_spec});
}

[[nodiscard]] volt::PartDefinition resistor_part(const volt::ComponentDefinition &component,
                                                 std::string key, std::string mpn,
                                                 std::vector<std::string> alternates = {}) {
    const auto &pin_keys = component.contract().pin_keys();
    return volt::PartDefinition{
        component,
        volt::PartIdentity{"volt.tests.bom", key, "1.0.0"},
        volt::ElectricalRecordSet{pin_keys.size(), {}},
        {volt::PinPackageTerminalMapping{pin_keys[0], {volt::PackageTerminalKey{"1"}}},
         volt::PinPackageTerminalMapping{pin_keys[1], {volt::PackageTerminalKey{"2"}}}},
        {},
        volt::PartProvenance{},
        {},
        volt::OrderablePart{
            volt::ManufacturerPart{"Yageo", std::move(mpn)},
            volt::PackageRef{"0603"},
            volt::HashedFootprintReference{volt::FootprintRef{"volt.tests.bom", "R_0603"},
                                           volt::sha256_content_hash(footprint_bytes)},
            {volt::PartFootprintPad{"1", -0.5, 0.0, 0.8, 0.8},
             volt::PartFootprintPad{"2", 0.5, 0.0, 0.8, 0.8}},
            {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                             {volt::FootprintPadKey{"1"}}},
             volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                             {volt::FootprintPadKey{"2"}}}},
            std::move(alternates),
        },
    };
}

[[nodiscard]] volt::ComponentId add_resistor(volt::Circuit &circuit,
                                             volt::ComponentDefId component_def,
                                             const volt::PartLibrary &library,
                                             const std::string &reference,
                                             const std::string &part_key) {
    const auto component = circuit.instantiate_component(
        component_def,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{reference}});
    circuit.update(component,
                   volt::SelectLibraryPart{library, library.require(volt::PartKey{part_key})});
    return component;
}

BomCircuit build_bom_circuit() {
    auto circuit = volt::Circuit{};
    const auto component = define_resistor(circuit);
    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"volt.tests.bom", "1.0.0", volt::PartLibrarySchemaVersion::V1}};
    builder.add_component(circuit.get(component));
    builder.add_part(
        resistor_part(circuit.get(component), "330R", "RC0603FR-07330RL", {"RC0603FR-07330RLA"}));
    builder.add_part(
        resistor_part(circuit.get(component), "1K", "RC0603FR-071KL", {"RC0603FR-071KLA"}));
    builder.add_part(resistor_part(circuit.get(component), "bad-alternate", "BAD", {"BAD"}));
    const auto resolver = BomAssetResolver{};
    auto library = builder.build(resolver);
    const auto r1 = add_resistor(circuit, component, library, "R1", "330R");
    const auto r2 = add_resistor(circuit, component, library, "R2", "330R");
    const auto r3 = add_resistor(circuit, component, library, "R3", "1K");
    circuit.update(r1, volt::SetAssemblyIntent{.dnp = false});
    circuit.update(r2, volt::SetAssemblyIntent{.dnp = false});
    circuit.update(r2, volt::SetAssemblyIntent{.selection_override = true});
    circuit.update(r3, volt::SetAssemblyIntent{.dnp = true});
    return BomCircuit{std::move(circuit), std::move(library), r1, r2, r3};
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

    const auto bom = volt::project_bom(test.circuit, test.library, sourcing);

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

    const auto csv =
        volt::io::write_bom_csv(volt::project_bom(test.circuit, test.library, sourcing));

    CHECK(csv == "manufacturer,mpn,package,quantity,references,dnp,approved_alternate_mpns,"
                 "selection_override_references,sourcing.sku,sourcing.supplier\n"
                 "Yageo,RC0603FR-071KL,0603,0,R3,true,RC0603FR-071KLA,,,\n"
                 "Yageo,RC0603FR-07330RL,0603,2,R1 R2,false,RC0603FR-07330RLA,R2,"
                 "311-330HRCT-ND,Digi-Key\n");
}

TEST_CASE("BOM CSV escapes dynamic sourcing headers") {
    auto test = build_bom_circuit();
    auto sourcing = volt::BomSourcingSnapshot{};
    sourcing.set_mpn_properties(
        "RC0603FR-07330RL",
        volt::PropertyMap{
            {volt::PropertyKey{"unit,price"}, volt::PropertyValue{std::int64_t{123}}},
            {volt::PropertyKey{"quote\"key"}, volt::PropertyValue{"quoted value"}},
        });

    const auto csv =
        volt::io::write_bom_csv(volt::project_bom(test.circuit, test.library, sourcing));

    CHECK(csv == "manufacturer,mpn,package,quantity,references,dnp,approved_alternate_mpns,"
                 "selection_override_references,\"sourcing.quote\"\"key\",\"sourcing.unit,"
                 "price\"\n"
                 "Yageo,RC0603FR-071KL,0603,0,R3,true,RC0603FR-071KLA,,,\n"
                 "Yageo,RC0603FR-07330RL,0603,2,R1 R2,false,RC0603FR-07330RLA,R2,"
                 "quoted value,123\n");
}

TEST_CASE("BOM sourcing snapshot output is deterministic") {
    auto sourcing = volt::BomSourcingSnapshot{};
    sourcing.set_mpn_properties(
        "ZZZ", volt::PropertyMap{{volt::PropertyKey{"supplier"}, volt::PropertyValue{"Vendor B"}}});
    sourcing.set_mpn_properties(
        "AAA", volt::PropertyMap{
                   {volt::PropertyKey{"unit_price"}, volt::PropertyValue{std::int64_t{123}}},
                   {volt::PropertyKey{"supplier"}, volt::PropertyValue{"Vendor A"}},
               });

    const auto payload =
        nlohmann::json::parse(volt::io::write_bom_sourcing_snapshot_json(sourcing));

    CHECK(payload["format"] == "volt.bom_sourcing_snapshot");
    CHECK(payload["version"] == 1);
    REQUIRE(payload["entries"].size() == 2);
    CHECK(payload["entries"][0]["mpn"] == "AAA");
    CHECK(payload["entries"][0]["sourcing"] ==
          nlohmann::json({{"supplier", "Vendor A"}, {"unit_price", 123}}));
    CHECK(payload["entries"][1]["mpn"] == "ZZZ");
}

TEST_CASE("BOM sourcing snapshot rejects empty MPNs with a typed kernel error") {
    auto sourcing = volt::BomSourcingSnapshot{};

    CHECK_THROWS_AS(sourcing.set_mpn_properties("", {}), std::invalid_argument);

    try {
        sourcing.set_mpn_properties("", {});
        FAIL("Expected empty BOM sourcing MPN to throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
        CHECK(std::string{error.what()} == "BOM sourcing MPN must not be empty");
    }
}

TEST_CASE("BOM readiness reports stable diagnostics on offending instances") {
    auto test = build_bom_circuit();
    const auto component_def = volt::ComponentDefId{0};
    const auto missing_part = test.circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R4"}});
    const auto missing_dnp = add_resistor(test.circuit, component_def, test.library, "R5", "330R");
    const auto bad_alternate =
        add_resistor(test.circuit, component_def, test.library, "R6", "bad-alternate");

    test.circuit.update(missing_part, volt::SetAssemblyIntent{.dnp = false});
    test.circuit.update(bad_alternate, volt::SetAssemblyIntent{.dnp = false});

    const auto report = volt::validate_bom_readiness(test.circuit, test.library);

    REQUIRE(report.count() == 3);
    CHECK(report.diagnostics()[0].code().value() == "BOM_COMPONENT_MISSING_SELECTED_PART");
    CHECK(report.diagnostics()[0].entities() ==
          std::vector{volt::EntityRef::component(missing_part),
                      volt::EntityRef::component_def(component_def)});
    CHECK(report.diagnostics()[1].code().value() == "BOM_COMPONENT_IMPLICIT_DNP");
    CHECK(report.diagnostics()[1].entities() ==
          std::vector{volt::EntityRef::component(missing_dnp),
                      volt::EntityRef::component_def(component_def)});
    CHECK(report.diagnostics()[2].code().value() == "BOM_APPROVED_ALTERNATE_DUPLICATES_PRIMARY");
    CHECK(report.diagnostics()[2].entities() ==
          std::vector{volt::EntityRef::component(bad_alternate),
                      volt::EntityRef::component_def(component_def)});
    CHECK(report.diagnostics()[2].category() == volt::DiagnosticCategory{"bom"});
}
