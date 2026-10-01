#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/updates.hpp>
#include <volt/electrical/dc_request.hpp>
#include <volt/electrical/passive_model.hpp>
#include <volt/io/electrical/dc_request_io.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/parts/part_library_bundle.hpp>

namespace volt::test::dc_request {

class Assets final : public PartAssetResolver {
  public:
    void add(const PartAssetReference &reference, std::string bytes) {
        assets_.emplace(std::pair{reference.kind(), reference.key()}, std::move(bytes));
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const PartAssetReference &reference) const override {
        const auto found = assets_.find(std::pair{reference.kind(), reference.key()});
        return found == assets_.end() ? std::nullopt : std::optional{found->second};
    }

  private:
    std::map<std::pair<PartAssetKind, std::string>, std::string> assets_;
};

class UnavailableParts final : public PartDefinitionResolver {
  public:
    [[nodiscard]] const PartDefinition &resolve(const LibraryPartRef &) const & override {
        throw KernelRangeError{ErrorCode::UnknownEntity, "Part unavailable"};
    }
};

struct Fixture {
    std::unique_ptr<Circuit> circuit;
    io::PartLibraryBundle library;
    NetId positive;
    NetId negative;
    ComponentId resistor;
    ComponentId absent;
    ComponentId outside;
    ComponentId non_electrical;
};

[[nodiscard]] inline Fixture make_fixture() {
    auto circuit = std::make_unique<Circuit>();
    const auto spec = ComponentSpec{
        .name = "S1 two-terminal occurrence",
        .pins = {PinSpec{.name = "A", .number = "1"}, PinSpec{.name = "B", .number = "2"}},
        .contract = ComponentContractSpec{.key = ComponentKey{"test.dc/two-terminal@1"},
                                          .pin_keys = {PinKey{"A"}, PinKey{"B"}}}};
    const auto definition = circuit->define_component(spec);
    const auto &component = circuit->get(definition);
    auto model = PartElectricalModelBuilder{component};
    const auto a = model.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    const auto b = model.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
    const auto x = model.internal_node(ModelInternalNodeKey{"x"});
    const auto y = model.internal_node(ModelInternalNodeKey{"y"});
    model.add<ResistanceElement>(ModelElementKey{"esr"}, a, x,
                                 ModelParameter{Quantity{UnitDimension::Resistance, 0.1}});
    model.add<InductanceElement>(ModelElementKey{"esl"}, x, y,
                                 ModelParameter{Quantity{UnitDimension::Inductance, 1.0e-9}});
    model.add<CapacitanceElement>(ModelElementKey{"storage"}, y, b,
                                  ModelParameter{Quantity{UnitDimension::Capacitance, 1.0e-6}});

    const auto footprint = io::write_footprint_asset(FootprintDefinition{
        FootprintRef{"test.dc", "two-terminal"},
        {FootprintPad::surface_mount("1", FootprintPadShape::Rectangle, {-0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd()),
         FootprintPad::surface_mount("2", FootprintPadShape::Rectangle, {0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd())}});
    const auto make_part = [&](std::string key, std::optional<PartElectricalModel> value) {
        return PartDefinition{
            component,
            PartIdentity{"test.dc", key, "1"},
            ElectricalRecordSet{2},
            {PinPackageTerminalMapping{PinKey{"A"}, {PackageTerminalKey{"1"}}},
             PinPackageTerminalMapping{PinKey{"B"}, {PackageTerminalKey{"2"}}}},
            {},
            PartProvenance{"", "volt.tests", "S1 DC request fixture"},
            {},
            OrderablePart{
                ManufacturerPart{"Test", key},
                PackageRef{"TEST-2"},
                HashedFootprintReference{FootprintRef{"test.dc", "two-terminal"},
                                         sha256_content_hash(footprint)},
                {PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5},
                 PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5}},
                {PackageTerminalPadMapping{PackageTerminalKey{"1"}, {FootprintPadKey{"1"}}},
                 PackageTerminalPadMapping{PackageTerminalKey{"2"}, {FootprintPadKey{"2"}}}}},
            std::move(value)};
    };
    const auto modeled = make_part("modeled", model.build());
    const auto absent = make_part("absent", std::nullopt);
    auto assets = Assets{};
    for (const auto &part : {std::cref(modeled), std::cref(absent)}) {
        for (const auto &reference : part_asset_references(part.get())) {
            assets.add(reference, footprint);
        }
    }
    auto builder =
        PartLibraryBuilder{PartLibraryIdentity{"test.dc", "2026", PartLibrarySchemaVersion::V1}};
    builder.add_component(spec).add_part(modeled).add_part(absent);
    auto library = io::PartLibraryBundle::build(
        builder, std::vector{PartKey{"modeled"}, PartKey{"absent"}}, assets);

    const auto positive = circuit->add_net(NetSpec{.name = NetName{"positive"}});
    const auto negative = circuit->add_net(NetSpec{.name = NetName{"negative"}});
    const auto add = [&](std::string reference, std::optional<PartKey> selected) {
        const auto occurrence = circuit->instantiate_component(
            definition, ComponentInstanceSpec{.reference = ReferenceDesignator{reference}});
        if (selected) {
            circuit->update(occurrence, SelectLibraryPart{library, library.require(*selected)});
        }
        circuit->connect(positive, queries::pin_by_number(*circuit, occurrence, "1").value());
        circuit->connect(negative, queries::pin_by_number(*circuit, occurrence, "2").value());
        return occurrence;
    };
    const auto resistor = add("R1", PartKey{"modeled"});
    circuit->update(resistor, SetAssemblyIntent{.dnp = true});
    const auto missing = add("U1", PartKey{"absent"});
    const auto outside = add("J1", std::nullopt);
    const auto non_electrical = add("J2", std::nullopt);
    return {std::move(circuit), std::move(library), positive, negative, resistor, missing, outside,
            non_electrical};
}

[[nodiscard]] inline DcRequest complete_request(const ElectricalInput &input) {
    const auto positive = input.net(NetId{0});
    const auto negative = input.net(NetId{1});
    return DcRequest{
        ElectricalRequestKey{"operating-point"},
        input,
        negative,
        {DcVoltageSource{ElectricalSourceKey{"drive"}, ElectricalNetPair{positive, negative},
                         Quantity{UnitDimension::Voltage, 5.0}},
         DcCurrentSource{ElectricalSourceKey{"load"}, ElectricalNetPair{positive, negative},
                         Quantity{UnitDimension::Current, 0.01}}},
        {DcVoltageProbe{ElectricalProbeKey{"rail"}, ElectricalNetPair{positive, negative}},
         DcSourceCurrentProbe{ElectricalProbeKey{"supply-current"}, ElectricalSourceKey{"drive"}},
         DcModelElementCurrentProbe{ElectricalProbeKey{"resistor-current"},
                                    input.occurrence(ComponentId{0}), ModelElementKey{"esr"}}},
        {DcOccurrenceExclusion{input.occurrence(ComponentId{1}),
                               DcReplacedByStimulusExclusion{{ElectricalSourceKey{"drive"}}}},
         DcOccurrenceExclusion{input.occurrence(ComponentId{2}), DcOutsideAnalysisExclusion{}},
         DcOccurrenceExclusion{input.occurrence(ComponentId{3}), DcNonElectricalExclusion{}}}};
}

} // namespace volt::test::dc_request
