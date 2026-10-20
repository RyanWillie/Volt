#pragma once
#include "manufacturing_fixture.hpp"
#include <volt/circuit/validation/validation.hpp>

namespace volt::test {
inline Quantity rating_volts(double value) { return {UnitDimension::Voltage, value}; }

inline Quantity rating_amps(double value) { return {UnitDimension::Current, value}; }

inline ElectricalSubject rating_subject() {
    return ElectricalSubject::supply_domain({ElectricalPinIndex{0}}, {ElectricalPinIndex{1}});
}

inline ElectricalRecordSpec voltage_rating(double minimum = 0, double maximum = 5) {
    return voltage_record(
        rating_subject(), ElectricalMeaning::AbsoluteLimit,
        ElectricalValue{QuantityRange::bounded(rating_volts(minimum), rating_volts(maximum))});
}

struct RatingOccurrence {
    std::vector<ElectricalRecordSpec> records = {voltage_rating()};
    std::optional<bool> dnp = false;
    bool selected = true;
    bool placed = true;
    bool connected = true;
    bool override_selection = false;
    std::size_t selected_index = 0;
    BoardSide side = BoardSide::Top;
    double rotation = 0;
    std::string mpn = "SELECTED";
    std::vector<std::string> alternates = {"APPROVED-ONLY"};
};

struct RatingFixtureOptions {
    std::vector<RatingOccurrence> occurrences = {RatingOccurrence{}};
    std::optional<double> voltage = 3.3;
    std::optional<double> return_voltage;
    bool modules = false;
    bool noncanonical_voltage = false;
    bool contradictory_voltage = false;
};

inline void set_rating_voltage(Circuit &circuit, NetId net, double value) {
    circuit.update(net, SetNetElectricalAttribute{
                            {ElectricalAttributeName{"voltage"}, ElectricalAttributeOwner::Net,
                             ElectricalAttributeKind::DesignInput, UnitDimension::Voltage},
                            ElectricalAttributeValue{rating_volts(value)}});
}

// All source owners are local and destroyed before the returned immutable artifact is used.
inline CompiledBoard assembly_rating_fixture(RatingFixtureOptions options = {}) {
    auto circuit = Circuit{};
    const auto spec = ComponentSpec{
        .name = "Synthetic exact rating subject",
        .pins = {PinSpec{.name = "P", .number = "1"}, PinSpec{.name = "N", .number = "2"},
                 PinSpec{.name = "Q", .number = "3"}},
        .schematic_symbols = {SchematicSymbolReference{"test.rating:symbol"}},
        .contract = ComponentContractSpec{.key = ComponentKey{"test.rating.subject@1"},
                                          .pin_keys = {PinKey{"P"}, PinKey{"N"}, PinKey{"Q"}}}};
    const auto def = circuit.define_component(spec);
    const auto &definition = circuit.get(def);
    const auto footprint = FootprintDefinition{
        FootprintRef{"test.rating", "subject"},
        {FootprintPad::surface_mount("1", FootprintPadShape::Circle, {0, -1}, {0.8, 0.8},
                                     FootprintLayerSet::front_smd()),
         FootprintPad::surface_mount("2", FootprintPadShape::Circle, {0, 1}, {0.8, 0.8},
                                     FootprintLayerSet::front_smd()),
         FootprintPad::surface_mount("3", FootprintPadShape::Circle, {1, -1}, {0.8, 0.8},
                                     FootprintLayerSet::front_smd())}};
    const auto bytes = io::write_footprint_asset(footprint);
    const auto asset = PartAssetReference{PartAssetKind::Footprint, "footprint:test.rating/subject",
                                          sha256_content_hash(bytes)};
    auto builder =
        PartLibraryBuilder{PartLibraryIdentity{"test.rating", "1", PartLibrarySchemaVersion::V1}};
    builder.add_component(spec);
    auto keys = std::vector<PartKey>{};
    for (std::size_t i = 0; i < options.occurrences.size(); ++i) {
        const auto &input = options.occurrences[i];
        keys.emplace_back("part-" + std::to_string(i));
        builder.add_part(PartDefinition{
            definition,
            PartIdentity{"test.rating", keys.back().value(), "1"},
            ElectricalRecordSet{3, input.records},
            {PinPackageTerminalMapping{PinKey{"P"}, {PackageTerminalKey{"1"}}},
             PinPackageTerminalMapping{PinKey{"N"}, {PackageTerminalKey{"2"}}},
             PinPackageTerminalMapping{PinKey{"Q"}, {PackageTerminalKey{"3"}}}},
            {},
            PartProvenance{"synthetic://rating-datasheet", "test", "synthetic only"},
            {},
            OrderablePart{
                ManufacturerPart{"Synthetic", input.mpn},
                PackageRef{"TEST-2"},
                HashedFootprintReference{footprint.ref(), asset.digest()},
                export_fixture_part_pads(footprint),
                {PackageTerminalPadMapping{PackageTerminalKey{"1"}, {FootprintPadKey{"1"}}},
                 PackageTerminalPadMapping{PackageTerminalKey{"2"}, {FootprintPadKey{"2"}}},
                 PackageTerminalPadMapping{PackageTerminalKey{"3"}, {FootprintPadKey{"3"}}}},
                input.alternates,
                {}}});
    }
    auto assets = ExportFixtureAssetResolver{};
    assets.add(asset, bytes);
    const auto bundle = io::PartLibraryBundle::build(builder, keys, assets);
    auto positive = std::optional<NetId>{};
    auto negative = std::optional<NetId>{};
    const auto add_outer = [&] {
        positive = circuit.add_net(NetSpec{.name = NetName{"P"}, .kind = NetKind::Power});
        negative = circuit.add_net(NetSpec{.name = NetName{"N"}, .kind = NetKind::Ground});
        if (options.voltage)
            set_rating_voltage(circuit, *positive, *options.voltage);
        if (options.return_voltage)
            set_rating_voltage(circuit, *negative, *options.return_voltage);
    };
    if (!options.noncanonical_voltage)
        add_outer();
    auto components = std::vector<ComponentId>{};
    auto instances = std::vector<ModuleInstanceId>{};
    auto module = std::optional<ModuleDefId>{};
    if (options.modules) {
        module = circuit.define_module(ModuleSpec{
            .name = ModuleName{"Subject"},
            .template_nets = {TemplateNetDefinition{NetName{"P"}, NetKind::Power},
                              TemplateNetDefinition{NetName{"N"}, NetKind::Ground}},
            .components = {ModuleComponentTemplate{def, ReferenceDesignator{"U"}}},
            .connections = {ModulePinConnectionSpec{NetName{"P"}, ReferenceDesignator{"U"},
                                                    definition.pins()[0]},
                            ModulePinConnectionSpec{NetName{"N"}, ReferenceDesignator{"U"},
                                                    definition.pins()[1]}},
            .ports = {ModulePortSpec{PortName{"P"}, NetName{"P"}, PortRole::Passive},
                      ModulePortSpec{PortName{"N"}, NetName{"N"}, PortRole::Passive}}});
    }
    for (std::size_t i = 0; i < options.occurrences.size(); ++i) {
        if (module) {
            const auto instance = circuit.instantiate_module(
                *module, ModuleInstanceSpec{.name = ModuleInstanceName{"M" + std::to_string(i)}});
            instances.push_back(instance);
            components.push_back(*queries::concrete_component_for(
                circuit, instance, circuit.get(*module).components()[0]));
        } else
            components.push_back(circuit.instantiate_component(
                def,
                ComponentInstanceSpec{.reference = ReferenceDesignator{"U" + std::to_string(i)}}));
    }
    if (!positive)
        add_outer();
    for (std::size_t i = 0; i < components.size(); ++i) {
        const auto component = components[i];
        const auto &input = options.occurrences[i];
        if (input.selected)
            circuit.update(component,
                           SelectLibraryPart{bundle, bundle.require(keys[input.selected_index])});
        if (input.dnp || input.override_selection)
            circuit.update(component,
                           SetAssemblyIntent{.dnp = input.dnp,
                                             .selection_override = input.override_selection});
        if (module) {
            if (input.connected) {
                static_cast<void>(
                    circuit.bind_port(instances[i], circuit.get(*module).ports()[0], *positive));
                static_cast<void>(
                    circuit.bind_port(instances[i], circuit.get(*module).ports()[1], *negative));
            }
            if (options.contradictory_voltage)
                set_rating_voltage(
                    circuit,
                    *queries::concrete_net_for(circuit, instances[i],
                                               circuit.get(*module).template_nets()[0]),
                    9.0);
        } else if (input.connected) {
            static_cast<void>(
                circuit.connect(*positive, *queries::pin_by_name(circuit, component, "Q")));
            static_cast<void>(
                circuit.connect(*positive, *queries::pin_by_name(circuit, component, "P")));
            static_cast<void>(
                circuit.connect(*negative, *queries::pin_by_name(circuit, component, "N")));
        }
    }
    auto board = Board{circuit, BoardName{"Synthetic ratings"}};
    auto top = BoardLayer{"F.Cu", BoardLayerRole::Copper, BoardLayerSide::Top};
    top.set_copper_weight_oz(1);
    auto bottom = BoardLayer{"B.Cu", BoardLayerRole::Copper, BoardLayerSide::Bottom};
    bottom.set_copper_weight_oz(1);
    const auto front = board.add_layer(top);
    const auto back = board.add_layer(bottom);
    board.set_layer_stack(LayerStack{{front, back}, 1.6});
    board.set_outline(BoardOutline::rectangle({0, 0}, {30, 20}));
    board.set_capability_profile(manufacturing_capability());
    for (std::size_t i = 0; i < components.size(); ++i)
        if (options.occurrences[i].placed)
            static_cast<void>(board.place_component(
                ComponentPlacement{components[i],
                                   {5.0 + 5.0 * static_cast<double>(i), 10},
                                   BoardRotation::degrees(options.occurrences[i].rotation),
                                   options.occurrences[i].side}));
    auto compiled = io::compile_board(circuit, board, bundle,
                                      CompiledBoardCapabilities{manufacturing_capability()});
    if (!compiled.has_artifact())
        throw KernelLogicError{ErrorCode::InvalidState, compiled.failure()->message()};
    return std::move(compiled).take_artifact();
}

inline ManufacturingTarget assembly_rating_target(const CompiledBoard &compiled,
                                                  bool assembly = true,
                                                  CplProjectionOptions offsets = {}) {
    auto process = manufacturing_process();
    if (assembly)
        process.assembly = ManufacturingAssembly::SmtAndThroughHole;
    return {compiled.identity(),
            compiled.content_digest(),
            assembly ? ManufacturingGoal::FabricationAndAssembly : ManufacturingGoal::Fabrication,
            process,
            manufacturing_profile(false, process),
            assembly ? std::optional{std::move(offsets)} : std::nullopt};
}
} // namespace volt::test
