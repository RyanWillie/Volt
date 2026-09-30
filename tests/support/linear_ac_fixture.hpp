#pragma once
#include "electrical_compilation_fixture.hpp"
#include <volt/electrical/ac_request.hpp>

namespace volt::test::linear_ac {
struct Fixture {
    std::unique_ptr<Circuit> circuit;
    io::PartLibraryBundle library;
    ComponentDefId component;
    NetId supply, output, middle, reference;

    ComponentId add(std::string key, std::string reference_designator, NetId from, NetId to) {
        const auto id = circuit->instantiate_component(
            component,
            ComponentInstanceSpec{.reference = ReferenceDesignator{reference_designator}});
        circuit->update(id, SelectLibraryPart{library, library.require(PartKey{key})});
        circuit->connect(from, queries::pin_by_number(*circuit, id, "1").value());
        circuit->connect(to, queries::pin_by_number(*circuit, id, "2").value());
        return id;
    }
};

inline Fixture fixture(double resistance = 1000, double inductance = 0.1, double capacitance = 1e-6,
                       bool include_middle = false, bool include_output = true) {
    auto circuit = std::make_unique<Circuit>();
    const auto spec = ComponentSpec{
        .name = "AC two terminal",
        .pins = {PinSpec{.name = "A", .number = "1"}, PinSpec{.name = "B", .number = "2"}},
        .contract = ComponentContractSpec{.key = ComponentKey{"test.ac/component@1"},
                                          .pin_keys = {PinKey{"A"}, PinKey{"B"}}}};
    const auto id = circuit->define_component(spec);
    const auto &component = circuit->get(id);
    const auto model = [&](UnitDimension dimension, double value) {
        PartElectricalModelBuilder builder{component};
        const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
        const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
        const auto parameter =
            ModelParameter{Quantity{dimension, value},
                           value == 0 ? std::optional{Tolerance::percent(0)} : std::nullopt};
        if (dimension == UnitDimension::Resistance)
            builder.add<ResistanceElement>(ModelElementKey{"body"}, a, b, parameter);
        else if (dimension == UnitDimension::Inductance)
            builder.add<InductanceElement>(ModelElementKey{"body"}, a, b, parameter);
        else
            builder.add<CapacitanceElement>(ModelElementKey{"body"}, a, b, parameter);
        return builder.build();
    };
    const auto footprint = io::write_footprint_asset(FootprintDefinition{
        FootprintRef{"test.compile", "two-terminal"},
        {FootprintPad::surface_mount("1", FootprintPadShape::Rectangle, {-0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd()),
         FootprintPad::surface_mount("2", FootprintPadShape::Rectangle, {0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd())}});
    const auto make_part = [&](const ComponentDefinition &part_component, std::string key,
                               std::optional<PartElectricalModel> part_model) {
        return PartDefinition{
            part_component,
            PartIdentity{"test.ac", key, "1"},
            ElectricalRecordSet{2},
            {PinPackageTerminalMapping{PinKey{"A"}, {PackageTerminalKey{"1"}}},
             PinPackageTerminalMapping{PinKey{"B"}, {PackageTerminalKey{"2"}}}},
            {},
            PartProvenance{"", "volt.tests", "S2 electrical compilation fixture"},
            {},
            OrderablePart{
                ManufacturerPart{"Test", key},
                PackageRef{"TEST-2"},
                HashedFootprintReference{FootprintRef{"test.compile", "two-terminal"},
                                         sha256_content_hash(footprint)},
                {PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5},
                 PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5}},
                {PackageTerminalPadMapping{PackageTerminalKey{"1"}, {FootprintPadKey{"1"}}},
                 PackageTerminalPadMapping{PackageTerminalKey{"2"}, {FootprintPadKey{"2"}}}}},
            std::move(part_model)};
    };

    const auto r = make_part(component, "resistor", model(UnitDimension::Resistance, resistance));
    const auto l = make_part(component, "inductor", model(UnitDimension::Inductance, inductance));
    const auto c =
        make_part(component, "capacitor", model(UnitDimension::Capacitance, capacitance));
    electrical_compilation::Assets assets;
    for (const auto &part : {std::cref(r), std::cref(l), std::cref(c)})
        for (const auto &ref : part_asset_references(part.get()))
            assets.add(ref, footprint);
    PartLibraryBuilder builder{
        PartLibraryIdentity{"test.ac", "2026", PartLibrarySchemaVersion::V1}};
    builder.add_component(spec).add_part(r).add_part(l).add_part(c);
    const auto part_keys =
        std::vector{PartKey{"resistor"}, PartKey{"inductor"}, PartKey{"capacitor"}};
    auto library = io::PartLibraryBundle::build(builder, part_keys, assets);
    const auto supply = circuit->add_net(NetSpec{.name = NetName{"supply"}});
    const auto reference = circuit->add_net(NetSpec{.name = NetName{"reference"}});
    const auto output =
        include_output ? circuit->add_net(NetSpec{.name = NetName{"output"}}) : supply;
    const auto middle =
        include_middle ? circuit->add_net(NetSpec{.name = NetName{"middle"}}) : reference;
    return {std::move(circuit), std::move(library), id, supply, output, middle, reference};
}
} // namespace volt::test::linear_ac
