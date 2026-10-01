// Exercise native diode authoring, source-free reopen and solving through installed headers.
#include <cmath>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/updates.hpp>
#include <volt/electrical/dc_solve.hpp>
#include <volt/io/electrical/dc_request_io.hpp>
#include <volt/io/electrical/dc_solve_io.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/parts/part_library_bundle.hpp>

namespace {
class Assets final : public volt::PartAssetResolver {
  public:
    std::map<std::pair<volt::PartAssetKind, std::string>, std::string> bytes;

    std::optional<std::string> resolve(const volt::PartAssetReference &reference) const override {
        const auto found = bytes.find({reference.kind(), reference.key()});
        return found == bytes.end() ? std::nullopt : std::optional{found->second};
    }
};
} // namespace

int main() {
    using namespace volt;
    auto circuit = Circuit{};
    const auto spec = ComponentSpec{
        .name = "Idealized diode bias network",
        .pins = {PinSpec{.name = "A", .number = "1"}, PinSpec{.name = "K", .number = "2"}},
        .contract = ComponentContractSpec{.key = ComponentKey{"packaging/diode-bias@1"},
                                          .pin_keys = {PinKey{"A"}, PinKey{"K"}}}};
    const auto definition = circuit.define_component(spec);
    auto builder = PartElectricalModelBuilder{circuit.get(definition)};
    const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    const auto k = builder.terminal(ModelTerminalKey{"k"}, PinKey{"K"});
    const auto x = builder.internal_node(ModelInternalNodeKey{"x"});
    builder.add<ResistanceElement>(ModelElementKey{"series"}, a, x,
                                   ModelParameter{Quantity{UnitDimension::Resistance, 1000.0}});
    builder.add<ShockleyDiodeElement>(
        ModelElementKey{"diode"}, x, k,
        DiodeParameters{ModelParameter{Quantity{UnitDimension::Current, 1e-12}},
                        ModelParameter{Quantity{UnitDimension::Ratio, 1.0}},
                        Quantity{UnitDimension::Temperature, 300.15},
                        QuantityRange::bounded(Quantity{UnitDimension::Voltage, -0.05},
                                               Quantity{UnitDimension::Voltage, 0.8})});
    const auto footprint = io::write_footprint_asset(FootprintDefinition{
        FootprintRef{"packaging", "two-terminal"},
        {FootprintPad::surface_mount("1", FootprintPadShape::Rectangle, {-0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd()),
         FootprintPad::surface_mount("2", FootprintPadShape::Rectangle, {0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd())}});
    const auto part = PartDefinition{
        circuit.get(definition),
        PartIdentity{"packaging", "diode-bias", "1"},
        ElectricalRecordSet{2},
        {PinPackageTerminalMapping{PinKey{"A"}, {PackageTerminalKey{"1"}}},
         PinPackageTerminalMapping{PinKey{"K"}, {PackageTerminalKey{"2"}}}},
        {},
        PartProvenance{"", "volt.packaging", "Idealized uncalibrated diode"},
        {},
        OrderablePart{
            ManufacturerPart{"Test", "diode-bias"},
            PackageRef{"TEST-2"},
            HashedFootprintReference{FootprintRef{"packaging", "two-terminal"},
                                     sha256_content_hash(footprint)},
            {PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5}, PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5}},
            {PackageTerminalPadMapping{PackageTerminalKey{"1"}, {FootprintPadKey{"1"}}},
             PackageTerminalPadMapping{PackageTerminalKey{"2"}, {FootprintPadKey{"2"}}}}},
        builder.build()};
    auto assets = Assets{};
    for (const auto &reference : part_asset_references(part)) {
        assets.bytes.emplace(std::pair{reference.kind(), reference.key()}, footprint);
    }
    auto library_builder =
        PartLibraryBuilder{PartLibraryIdentity{"packaging", "1", PartLibrarySchemaVersion::V1}};
    library_builder.add_component(spec).add_part(part);
    const auto library =
        io::PartLibraryBundle::build(library_builder, std::vector{PartKey{"diode-bias"}}, assets);
    const auto occurrence = circuit.instantiate_component(
        definition, ComponentInstanceSpec{.reference = ReferenceDesignator{"D1"}});
    circuit.update(occurrence, SelectLibraryPart{library, library.require(PartKey{"diode-bias"})});
    const auto positive = circuit.add_net(NetSpec{.name = NetName{"drive"}});
    const auto reference = circuit.add_net(NetSpec{.name = NetName{"reference"}});
    circuit.connect(positive, queries::pin_by_number(circuit, occurrence, "1").value());
    circuit.connect(reference, queries::pin_by_number(circuit, occurrence, "2").value());

    const auto reopened_library = io::PartLibraryBundle::open(library.bytes());
    const auto reopened_circuit = io::read_logical_circuit_text(io::write_logical_circuit(circuit));
    const auto input = io::prepare_electrical_input(reopened_circuit, reopened_library);
    const auto request = DcRequest{
        ElectricalRequestKey{"bias"},
        input,
        input.net(reference),
        {DcVoltageSource{ElectricalSourceKey{"drive"},
                         ElectricalNetPair{input.net(positive), input.net(reference)},
                         Quantity{UnitDimension::Voltage, 5.0}}},
        {DcModelElementCurrentProbe{ElectricalProbeKey{"diode-current"},
                                    input.occurrence(occurrence), ModelElementKey{"diode"}}}};
    const auto rebound = io::read_dc_request(io::write_dc_request(request), input);
    const auto compiled = compile_electrical(rebound);
    if (!compiled.complete()) {
        std::cerr << "installed native diode compilation failed\n";
        return 1;
    }
    const auto refused = solve_dc(*compiled.model());
    const auto solved = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
    const auto limited = solve_dc(*compiled.model(), NonlinearDcSolveOptions{DcSolveOptions{}, 1});
    if (refused.success() || refused.solution() || !solved.success() || !solved.solution() ||
        limited.success() || limited.solution() ||
        std::abs(solved.solution()->probes().front().value.value() - 0.004425523074410702) >
            1e-12 ||
        io::write_dc_solution(*solved.solution()).empty() ||
        io::write_dc_solve_report(limited).empty()) {
        std::cerr << "installed native diode success/refusal contract failed\n";
        return 1;
    }
    std::cout << "installed native diode author/reopen/solve contract passed\n";
}
