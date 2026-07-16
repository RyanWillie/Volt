#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/parts/part_definition.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/errors.hpp>

namespace {

volt::ContentHash hash(char fill) { return volt::ContentHash{"sha256:" + std::string(64U, fill)}; }

volt::ComponentSpec regulator_spec() {
    return volt::ComponentSpec{
        .name = "Three-pin regulator",
        .pins =
            {
                volt::PinSpec{.name = "GND", .number = "1"},
                volt::PinSpec{.name = "VO", .number = "2"},
                volt::PinSpec{.name = "VI", .number = "3"},
            },
        .source = volt::DefinitionSource{"volt.components", "regulator-3pin", "1.0.0"},
        .schematic_symbols = {volt::SchematicSymbolReference{"volt.power:regulator_3pin"}},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"volt.component/regulator-3pin@1"},
                .pin_keys = {volt::PinKey{"GND"}, volt::PinKey{"VO"}, volt::PinKey{"VI"}},
                .supply_domains = {volt::ContractSupplyDomain{
                    volt::SupplyDomainKey{"output"}, {volt::PinKey{"VO"}}, {volt::PinKey{"GND"}}}},
                .feature_schemas = {volt::supply_source_feature_schema()},
                .feature_bindings = {volt::FeatureBinding{
                    volt::FeatureKey{"output"},
                    volt::FeatureSchemaKey{"volt.feature/supply-source@1"},
                    volt::ComponentSubjectRef::supply_domain(volt::SupplyDomainKey{"output"}),
                    {volt::FeatureRoleBinding{volt::FeatureRoleKey{"positive"},
                                              {volt::PinKey{"VO"}}},
                     volt::FeatureRoleBinding{volt::FeatureRoleKey{"return"},
                                              {volt::PinKey{"GND"}}}}}},
            },
    };
}

volt::ElectricalRecordSet regulator_records() {
    const auto output = volt::ElectricalSubject::supply_domain({volt::ElectricalPinIndex{1}},
                                                               {volt::ElectricalPinIndex{0}});
    return volt::ElectricalRecordSet{
        3,
        {volt::voltage_record(output, volt::ElectricalMeaning::ProvidedRange,
                              volt::ElectricalValue{volt::QuantityRange::bounded(
                                  volt::Quantity{volt::UnitDimension::Voltage, 1.45},
                                  volt::Quantity{volt::UnitDimension::Voltage, 1.55})}),
         volt::current_record(output, volt::ElectricalMeaning::Capability,
                              volt::ElectricalValue{volt::ContinuousCurrent{
                                  volt::Quantity{volt::UnitDimension::Current, 1.0}}},
                              {}, {hash('e')})}};
}

std::vector<volt::PinPackageTerminalMapping> pin_terminal_mappings() {
    return {
        volt::PinPackageTerminalMapping{volt::PinKey{"GND"}, {volt::PackageTerminalKey{"1"}}},
        volt::PinPackageTerminalMapping{
            volt::PinKey{"VO"}, {volt::PackageTerminalKey{"2"}, volt::PackageTerminalKey{"tab"}}},
        volt::PinPackageTerminalMapping{volt::PinKey{"VI"}, {volt::PackageTerminalKey{"3"}}},
    };
}

std::vector<volt::PartFootprintPad> footprint_pads() {
    return {
        volt::PartFootprintPad{"1", -1.0, 0.0, 0.6, 0.6},
        volt::PartFootprintPad{"2", 0.0, 0.0, 0.6, 0.6},
        volt::PartFootprintPad{"3", 1.0, 0.0, 0.6, 0.6},
        volt::PartFootprintPad{"4", 0.0, 2.0, 1.8, 1.8, volt::PartFootprintPadRole::Thermal},
        volt::PartFootprintPad{"MH", 2.0, 2.0, 1.0, 1.0, volt::PartFootprintPadRole::Mechanical},
    };
}

std::vector<volt::PackageTerminalPadMapping> terminal_pad_mappings() {
    return {
        volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                        {volt::FootprintPadKey{"1"}}},
        volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                        {volt::FootprintPadKey{"2"}}},
        volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"3"},
                                        {volt::FootprintPadKey{"3"}}},
        volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"tab"},
                                        {volt::FootprintPadKey{"4"}}},
    };
}

volt::OrderablePart
orderable(std::vector<volt::PackageTerminalPadMapping> mappings = terminal_pad_mappings(),
          std::vector<volt::PartFootprintPad> pads = footprint_pads(),
          std::string package = "SOT-223-3") {
    return volt::OrderablePart{
        volt::ManufacturerPart{"Diodes Incorporated", "AP1117E15G-13"},
        volt::PackageRef{std::move(package)},
        volt::HashedFootprintReference{
            volt::FootprintRef{"Package_TO_SOT_SMD", "SOT-223-3_TabPin2"}, hash('b')},
        std::move(pads),
        std::move(mappings),
        {"AP1117E15G-7"},
        volt::PartModel3DReference{"step", "sot223.step", hash('c'), {0.0, 0.0, 0.8}, 0.0},
    };
}

volt::PartDefinition part(
    const volt::ComponentDefinition &component,
    std::vector<volt::PinPackageTerminalMapping> pin_mappings = pin_terminal_mappings(),
    std::vector<volt::DisposedPackageTerminal> dispositions = {},
    volt::OrderablePart physical = orderable(),
    volt::ElectricalRecordSet records = regulator_records(),
    volt::PartProvenance provenance = volt::PartProvenance{"AP1117 rev 24", "volt.tests", "manual"},
    std::vector<volt::PartSchematicAssetReference> schematic_assets = {
        volt::PartSchematicAssetReference{"volt.power:regulator_3pin", "default", hash('a')}}) {
    return volt::PartDefinition{
        component,
        volt::PartIdentity{"volt.power", "AP1117-15", "1.0.0"},
        std::move(records),
        std::move(pin_mappings),
        std::move(dispositions),
        std::move(provenance),
        std::move(schematic_assets),
        std::move(physical),
    };
}

const volt::Diagnostic *find_diagnostic(const volt::DiagnosticReport &report,
                                        std::string_view code) {
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code().value() == code) {
            return &diagnostic;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Exact part stores one component digest canonical records and two physical seams") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(regulator_spec());
    const auto exact = part(circuit.get(definition));

    CHECK(exact.implemented_component() == circuit.get(definition).content_identity());
    CHECK(exact.electrical_records().records().size() == 2U);
    CHECK(exact.pin_terminal_mappings().size() == 3U);
    CHECK(exact.orderable_part().terminal_pad_mappings().size() == 4U);
    CHECK(exact.pin_terminal_mappings()[1].pin() == volt::PinKey{"VI"});
    CHECK(exact.pin_terminal_mappings()[2].terminals() ==
          std::vector{volt::PackageTerminalKey{"2"}, volt::PackageTerminalKey{"tab"}});
    CHECK(exact.content_identity().value().starts_with("sha256:"));
}

TEST_CASE("Exact part normalizes unordered physical mappings into one content identity") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(regulator_spec());
    const auto &component = circuit.get(definition);
    const auto baseline = part(component);

    auto reversed_pins = pin_terminal_mappings();
    std::ranges::reverse(reversed_pins);
    auto reversed_pads = terminal_pad_mappings();
    std::ranges::reverse(reversed_pads);
    const auto reordered =
        part(component, std::move(reversed_pins), {}, orderable(std::move(reversed_pads)));

    CHECK(reordered.content_identity() == baseline.content_identity());
}

TEST_CASE("Exact part rejects missing foreign and multiply owned logical pin mappings") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(regulator_spec());
    const auto &component = circuit.get(definition);

    auto missing = pin_terminal_mappings();
    missing.pop_back();
    CHECK_THROWS_AS(part(component, std::move(missing)), std::invalid_argument);

    auto foreign = pin_terminal_mappings();
    foreign.back() =
        volt::PinPackageTerminalMapping{volt::PinKey{"FOREIGN"}, {volt::PackageTerminalKey{"3"}}};
    CHECK_THROWS_AS(part(component, std::move(foreign)), std::invalid_argument);

    auto duplicate_terminal = pin_terminal_mappings();
    duplicate_terminal.back() =
        volt::PinPackageTerminalMapping{volt::PinKey{"VI"}, {volt::PackageTerminalKey{"1"}}};
    CHECK_THROWS_AS(part(component, std::move(duplicate_terminal)), std::invalid_argument);
}

TEST_CASE("Exact part requires explicit non-electrical package terminal dispositions") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(regulator_spec());
    const auto &component = circuit.get(definition);

    auto pads = footprint_pads();
    pads.emplace_back("NC", 3.0, 0.0, 0.6, 0.6);
    auto pad_mappings = terminal_pad_mappings();
    pad_mappings.emplace_back(volt::PackageTerminalKey{"NC"},
                              std::vector{volt::FootprintPadKey{"NC"}});

    CHECK_THROWS_AS(part(component, pin_terminal_mappings(), {}, orderable(pad_mappings, pads)),
                    std::invalid_argument);

    const auto accepted =
        part(component, pin_terminal_mappings(),
             {volt::DisposedPackageTerminal{volt::PackageTerminalKey{"NC"},
                                            volt::PackageTerminalDisposition::NoConnect}},
             orderable(std::move(pad_mappings), std::move(pads)));
    REQUIRE(accepted.terminal_dispositions().size() == 1U);
    CHECK(accepted.terminal_dispositions()[0].disposition() ==
          volt::PackageTerminalDisposition::NoConnect);
}

TEST_CASE("Exact part rejects missing foreign and duplicate footprint pad ownership") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(regulator_spec());
    const auto &component = circuit.get(definition);

    auto missing_terminal = terminal_pad_mappings();
    missing_terminal.pop_back();
    CHECK_THROWS_AS(
        part(component, pin_terminal_mappings(), {}, orderable(std::move(missing_terminal))),
        std::invalid_argument);

    auto foreign_terminal = terminal_pad_mappings();
    foreign_terminal.back() = volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"FOREIGN"},
                                                              {volt::FootprintPadKey{"4"}}};
    CHECK_THROWS_AS(
        part(component, pin_terminal_mappings(), {}, orderable(std::move(foreign_terminal))),
        std::invalid_argument);

    auto foreign_pad = terminal_pad_mappings();
    foreign_pad.back() = volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"tab"},
                                                         {volt::FootprintPadKey{"99"}}};
    CHECK_THROWS_AS(part(component, pin_terminal_mappings(), {}, orderable(std::move(foreign_pad))),
                    std::invalid_argument);

    auto duplicate_pad = terminal_pad_mappings();
    duplicate_pad.back() = volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"tab"},
                                                           {volt::FootprintPadKey{"2"}}};
    CHECK_THROWS_AS(
        part(component, pin_terminal_mappings(), {}, orderable(std::move(duplicate_pad))),
        std::invalid_argument);

    auto unexplained_pad = footprint_pads();
    unexplained_pad.emplace_back("5", 3.0, 0.0, 0.6, 0.6);
    CHECK_THROWS_AS(part(component, pin_terminal_mappings(), {},
                         orderable(terminal_pad_mappings(), std::move(unexplained_pad))),
                    std::invalid_argument);
}

TEST_CASE("Exact part rejects incomplete or mismatched P1 implementation records") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(regulator_spec());
    const auto &component = circuit.get(definition);

    CHECK_THROWS_AS(
        part(component, pin_terminal_mappings(), {}, orderable(), volt::ElectricalRecordSet{3}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        part(component, pin_terminal_mappings(), {}, orderable(), volt::ElectricalRecordSet{2}),
        std::invalid_argument);
}

TEST_CASE("Exact part content identity changes with intrinsic part truth") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(regulator_spec());
    const auto &component = circuit.get(definition);
    const auto baseline = part(component);

    const auto changed_package =
        part(component, pin_terminal_mappings(), {},
             orderable(terminal_pad_mappings(), footprint_pads(), "TO-220"));
    const auto changed_provenance =
        part(component, pin_terminal_mappings(), {}, orderable(), regulator_records(),
             volt::PartProvenance{"AP1117 rev 25", "volt.tests", "manual"});
    const auto changed_asset = part(
        component, pin_terminal_mappings(), {}, orderable(), regulator_records(),
        volt::PartProvenance{"AP1117 rev 24", "volt.tests", "manual"},
        {volt::PartSchematicAssetReference{"volt.power:regulator_3pin", "default", hash('f')}});

    CHECK(changed_package.content_identity() != baseline.content_identity());
    CHECK(changed_provenance.content_identity() != baseline.content_identity());
    CHECK(changed_asset.content_identity() != baseline.content_identity());
}

TEST_CASE("Part footprint polygons reject structurally invalid geometry") {
    CHECK_THROWS_AS(volt::PartFootprintPolygon(std::vector<volt::PartFootprintPoint>{}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::PartFootprintPolygon(std::vector{
                        volt::PartFootprintPoint{0.0, 0.0},
                        volt::PartFootprintPoint{1.0, 0.0},
                    }),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::PartFootprintPolygon(std::vector{
                        volt::PartFootprintPoint{0.0, 0.0},
                        volt::PartFootprintPoint{1.0, 0.0},
                        volt::PartFootprintPoint{2.0, 0.0},
                    }),
                    std::invalid_argument);
}

TEST_CASE("Part lineup diagnostics retain geometry-only concerns") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(regulator_spec());
    auto pads = footprint_pads();
    pads[0] = volt::PartFootprintPad{"1", -0.2, 0.0, 0.6, 0.6};
    pads[1] = volt::PartFootprintPad{"2", 0.2, 0.0, 0.6, 0.6};

    const auto report =
        volt::validate_part_lineup(part(circuit.get(definition), pin_terminal_mappings(), {},
                                        orderable(terminal_pad_mappings(), std::move(pads))));

    const auto *diagnostic = find_diagnostic(report, "PART_PAD_OVERLAP");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity() == volt::Severity::Warning);
    CHECK(diagnostic->category() == volt::DiagnosticCategory{"part.lineup"});
}
