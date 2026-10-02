#pragma once

#include "electrical_compilation_fixture.hpp"

namespace volt::test::diode {

inline DiodeParameters canonical_parameters(double ideality = 1.0, double temperature = 300.15) {
    return DiodeParameters{ModelParameter{Quantity{UnitDimension::Current, 1.0e-12}},
                           ModelParameter{Quantity{UnitDimension::Ratio, ideality}},
                           Quantity{UnitDimension::Temperature, temperature},
                           QuantityRange::bounded(Quantity{UnitDimension::Voltage, -0.05},
                                                  Quantity{UnitDimension::Voltage, 0.8})};
}

struct Fixture {
    std::unique_ptr<Circuit> circuit;
    io::PartLibraryBundle library;
    ComponentDefId definition;
    NetId supply;
    NetId junction;
    NetId reference;
    ComponentId diode;
    std::optional<ComponentId> resistor;
    ComponentSpec spec;
};

inline Fixture make_fixture(std::optional<double> resistance = 1000.0,
                            DiodeParameters parameters = canonical_parameters(),
                            bool tied = false) {
    auto circuit = std::make_unique<Circuit>();
    const auto spec = ComponentSpec{
        .name = "Idealized two-terminal law",
        .pins = {PinSpec{.name = "A", .number = "1"}, PinSpec{.name = "K", .number = "2"}},
        .contract = ComponentContractSpec{.key = ComponentKey{"test.diode/two-terminal@1"},
                                          .pin_keys = {PinKey{"A"}, PinKey{"K"}}}};
    const auto definition = circuit->define_component(spec);
    const auto &component = circuit->get(definition);
    auto diode_builder = PartElectricalModelBuilder{component};
    const auto anode = diode_builder.terminal(ModelTerminalKey{"anode"}, PinKey{"A"});
    const auto cathode = diode_builder.terminal(ModelTerminalKey{"cathode"}, PinKey{"K"});
    diode_builder.add<ShockleyDiodeElement>(ModelElementKey{"junction"}, anode, cathode,
                                            std::move(parameters));
    const auto footprint = io::write_footprint_asset(FootprintDefinition{
        FootprintRef{"test.diode", "two-terminal"},
        {FootprintPad::surface_mount("1", FootprintPadShape::Rectangle, {-0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd()),
         FootprintPad::surface_mount("2", FootprintPadShape::Rectangle, {0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd())}});
    const auto part = [&](std::string name, PartElectricalModel model) {
        return PartDefinition{
            component,
            PartIdentity{"test.diode", name, "1"},
            ElectricalRecordSet{2},
            {PinPackageTerminalMapping{PinKey{"A"}, {PackageTerminalKey{"1"}}},
             PinPackageTerminalMapping{PinKey{"K"}, {PackageTerminalKey{"2"}}}},
            {},
            PartProvenance{"", "volt.tests", "idealized law; no manufacturer calibration"},
            {},
            OrderablePart{
                ManufacturerPart{"Test", name},
                PackageRef{"TEST-2"},
                HashedFootprintReference{FootprintRef{"test.diode", "two-terminal"},
                                         sha256_content_hash(footprint)},
                {PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5},
                 PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5}},
                {PackageTerminalPadMapping{PackageTerminalKey{"1"}, {FootprintPadKey{"1"}}},
                 PackageTerminalPadMapping{PackageTerminalKey{"2"}, {FootprintPadKey{"2"}}}}},
            std::move(model)};
    };
    const auto diode_part = part("diode", diode_builder.build());
    auto resistor_builder = PartElectricalModelBuilder{component};
    const auto a = resistor_builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    const auto b = resistor_builder.terminal(ModelTerminalKey{"b"}, PinKey{"K"});
    resistor_builder.add<ResistanceElement>(
        ModelElementKey{"body"}, a, b,
        ModelParameter{Quantity{UnitDimension::Resistance, resistance.value_or(1000.0)}});
    const auto resistor_part = part("resistor", resistor_builder.build());
    auto assets = electrical_compilation::Assets{};
    for (const auto &exact : {std::cref(diode_part), std::cref(resistor_part)}) {
        for (const auto &reference : part_asset_references(exact.get()))
            assets.add(reference, footprint);
    }
    auto library_builder =
        PartLibraryBuilder{PartLibraryIdentity{"test.diode", "1", PartLibrarySchemaVersion::V1}};
    library_builder.add_component(spec).add_part(diode_part).add_part(resistor_part);
    const auto part_keys = std::vector{PartKey{"diode"}, PartKey{"resistor"}};
    const auto library = io::PartLibraryBundle::build(library_builder, part_keys, assets);
    const auto ground = circuit->add_net(NetSpec{.name = NetName{"reference"}});
    const auto junction = tied ? ground : circuit->add_net(NetSpec{.name = NetName{"junction"}});
    const auto supply =
        resistance ? circuit->add_net(NetSpec{.name = NetName{"supply"}}) : junction;
    const auto occurrence = [&](std::string name, PartKey selected, NetId from, NetId to) {
        const auto id = circuit->instantiate_component(
            definition, ComponentInstanceSpec{.reference = ReferenceDesignator{name}});
        circuit->update(id, SelectLibraryPart{library, library.require(selected)});
        circuit->connect(from, queries::pin_by_number(*circuit, id, "1").value());
        circuit->connect(to, queries::pin_by_number(*circuit, id, "2").value());
        return id;
    };
    const auto diode_id = occurrence("D1", PartKey{"diode"}, junction, ground);
    const auto resistor_id =
        resistance ? std::optional{occurrence("R1", PartKey{"resistor"}, supply, junction)}
                   : std::nullopt;
    return {std::move(circuit), library,     definition, supply, junction, ground,
            diode_id,           resistor_id, spec};
}

inline void replace_model(Fixture &fixture, PartElectricalModel model) {
    const auto &component = fixture.circuit->get(fixture.definition);
    const auto &original = fixture.library.resolve(fixture.library.require(PartKey{"diode"}));
    const auto &resistor = fixture.library.resolve(fixture.library.require(PartKey{"resistor"}));
    const auto replacement = PartDefinition{component,
                                            original.identity(),
                                            original.electrical_records(),
                                            original.pin_terminal_mappings(),
                                            original.terminal_dispositions(),
                                            original.provenance(),
                                            original.schematic_assets(),
                                            original.orderable_part(),
                                            std::move(model)};
    auto assets = electrical_compilation::Assets{};
    for (const auto &part : {std::cref(replacement), std::cref(resistor)}) {
        for (const auto &reference : part_asset_references(part.get())) {
            assets.add(reference, std::string{fixture.library.asset(reference).value()});
        }
    }
    auto builder =
        PartLibraryBuilder{PartLibraryIdentity{"test.diode", "1", PartLibrarySchemaVersion::V1}};
    builder.add_component(fixture.spec).add_part(replacement).add_part(resistor);
    fixture.library = io::PartLibraryBundle::build(
        builder, std::vector{PartKey{"diode"}, PartKey{"resistor"}}, assets);
    fixture.circuit->update(
        fixture.diode,
        SelectLibraryPart{fixture.library, fixture.library.require(PartKey{"diode"})});
    if (fixture.resistor) {
        fixture.circuit->update(
            *fixture.resistor,
            SelectLibraryPart{fixture.library, fixture.library.require(PartKey{"resistor"})});
    }
}

inline ElectricalInput input(const Fixture &fixture) {
    return io::prepare_electrical_input(*fixture.circuit, fixture.library);
}

inline DcRequest voltage_request(const Fixture &fixture, const ElectricalInput &owner,
                                 double voltage) {
    return DcRequest{
        ElectricalRequestKey{"diode-voltage-drive"},
        owner,
        owner.net(fixture.reference),
        {DcVoltageSource{ElectricalSourceKey{"supply"},
                         ElectricalNetPair{owner.net(fixture.supply), owner.net(fixture.reference)},
                         Quantity{UnitDimension::Voltage, voltage}}},
        {DcVoltageProbe{
             ElectricalProbeKey{"junction-voltage"},
             ElectricalNetPair{owner.net(fixture.junction), owner.net(fixture.reference)}},
         DcModelElementCurrentProbe{ElectricalProbeKey{"diode-current"},
                                    owner.occurrence(fixture.diode), ModelElementKey{"junction"}}}};
}

inline DcRequest current_request(const Fixture &fixture, const ElectricalInput &owner,
                                 double current) {
    return DcRequest{
        ElectricalRequestKey{"diode-current-drive"},
        owner,
        owner.net(fixture.reference),
        {DcCurrentSource{ElectricalSourceKey{"supply"},
                         ElectricalNetPair{owner.net(fixture.reference), owner.net(fixture.supply)},
                         Quantity{UnitDimension::Current, current}}},
        {DcVoltageProbe{
             ElectricalProbeKey{"junction-voltage"},
             ElectricalNetPair{owner.net(fixture.junction), owner.net(fixture.reference)}},
         DcModelElementCurrentProbe{ElectricalProbeKey{"diode-current"},
                                    owner.occurrence(fixture.diode), ModelElementKey{"junction"}}}};
}

} // namespace volt::test::diode
