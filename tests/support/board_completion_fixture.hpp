#pragma once
#include "compiled_board_export_helpers.hpp"
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/io/pcb/board_completion.hpp>

namespace volt::test {
/** Explicitly synthetic fixture variants; no vendor capability claims. */
struct CompletionFixtureOptions {
    std::size_t components = 2;
    std::size_t placements = 2;
    bool selected = true;
    bool second_selected = true;
    bool connected = true;
    bool second_connected = true;
    bool second_dnp = false;
    bool different_nets = false;
    bool zone = true;
    bool tracks = false;
    bool pth_terminal = false;
    bool npth_hole = false;
    bool giant_body = false;
    bool giant_mask_pad = false;
    bool edge_text = false;
    bool omit_mapping = false;
    bool wrong_mapping = false;
    bool fab_critical_slot = false;
    bool netless_zone = false;
    bool multi_pad = false;
    bool modules = false;
    bool explicit_nc = false;
    bool nc_contact = false;
    bool interlayer = false;
    bool via = false;
    bool bad_span = false;
    bool mechanical_bridge = false;
    bool shrink = false;
    bool drill_only = false;
    bool severing_hole = false;
    bool drill_off_grid = false;
    double first_x = 10.0;
    double outline_x = 0.0;
    double second_x = 20.0;
    double zone_right = 25.0;
    double outline_shift = 0.0;
    ConnectionRequirement requirement = ConnectionRequirement::Required;
    ElectricalTerminalKind terminal = ElectricalTerminalKind::Passive;
    FootprintPadShape shape = FootprintPadShape::Circle;
};

[[nodiscard]] inline CompiledBoard completion_fixture(CompletionFixtureOptions options = {}) {
    auto circuit = Circuit{};
    const auto spec = ComponentSpec{
        .name = "Synthetic completion contact",
        .pins = {PinSpec{.name = "P",
                         .number = "1",
                         .requirement = options.requirement,
                         .terminal_kind = options.terminal}},
        .contract = ComponentContractSpec{.key = ComponentKey{"test.completion-contact@1"},
                                          .pin_keys = {PinKey{"P"}}}};
    const auto def = circuit.define_component(spec);
    const auto &definition = circuit.get(def);
    auto pads = std::vector{FootprintPad::surface_mount("1", options.shape, {0, 0}, {1, 1},
                                                        FootprintLayerSet::front_smd())};
    if (options.pth_terminal)
        pads[0] = FootprintPad::through_hole("1", FootprintPadShape::Circle, {0, 0}, {1, 1},
                                             FootprintLayerSet::through_hole(),
                                             FootprintDrill{0.3, FootprintPadPlating::Plated});
    if (options.npth_hole || options.giant_mask_pad)
        pads.push_back(FootprintPad::through_hole(
            "H", options.giant_mask_pad ? FootprintPadShape::Rectangle : FootprintPadShape::Circle,
            {5, 0}, {options.giant_mask_pad ? 1.0e14 : 0.5, 0.5},
            FootprintLayerSet::mechanical_hole(),
            FootprintDrill{0.3, FootprintPadPlating::NonPlated},
            FootprintPadMechanicalRole::Mounting));
    if (options.multi_pad)
        pads.push_back(FootprintPad::surface_mount("2", options.shape, {0, 2}, {1, 1},
                                                   FootprintLayerSet::front_smd()));
    if (options.mechanical_bridge)
        pads.push_back(FootprintPad::surface_mount("T", FootprintPadShape::Rectangle, {5, 0},
                                                   {11, 1}, FootprintLayerSet::front_smd(),
                                                   FootprintPadMechanicalRole::MechanicalSupport));
    const auto body = options.giant_body
                          ? std::optional{FootprintPolygon{
                                {{1.0e14, 0}, {1.0e14 + 1, 0}, {1.0e14 + 1, 1}, {1.0e14, 1}}}}
                          : std::nullopt;
    const auto footprint =
        FootprintDefinition{FootprintRef{"test.completion", "contact"}, pads, std::nullopt, body};
    const auto bytes = io::write_footprint_asset(footprint);
    const auto asset = PartAssetReference{
        PartAssetKind::Footprint, "footprint:test.completion/contact", sha256_content_hash(bytes)};
    auto terminals = std::vector{PackageTerminalKey{"1"}};
    auto terminal_pads =
        std::vector{PackageTerminalPadMapping{PackageTerminalKey{"1"}, {FootprintPadKey{"1"}}}};
    if (options.multi_pad) {
        terminals.push_back(PackageTerminalKey{"2"});
        terminal_pads.emplace_back(PackageTerminalKey{"2"}, std::vector{FootprintPadKey{"2"}});
    }
    if (options.omit_mapping)
        terminal_pads.clear();
    if (options.wrong_mapping)
        terminal_pads = {
            PackageTerminalPadMapping{PackageTerminalKey{"1"}, {FootprintPadKey{"ABSENT"}}}};
    auto builder = PartLibraryBuilder{
        PartLibraryIdentity{"test.completion", "1", PartLibrarySchemaVersion::V1}};
    builder.add_component(spec).add_part(
        PartDefinition{definition,
                       PartIdentity{"test.completion", "contact", "1"},
                       ElectricalRecordSet{1},
                       {PinPackageTerminalMapping{PinKey{"P"}, terminals}},
                       {},
                       PartProvenance{},
                       {},
                       OrderablePart{ManufacturerPart{"Synthetic", "CONTACT"},
                                     PackageRef{"TEST"},
                                     HashedFootprintReference{footprint.ref(), asset.digest()},
                                     export_fixture_part_pads(footprint),
                                     terminal_pads,
                                     {},
                                     {}}});
    auto resolver = ExportFixtureAssetResolver{};
    resolver.add(asset, bytes);
    const auto key = PartKey{"contact"};
    const auto bundle = io::PartLibraryBundle::build(builder, std::vector{key}, resolver);
    const auto net = circuit.add_net(NetSpec{.name = NetName{"GND"}, .kind = NetKind::Ground});
    const auto other = circuit.add_net(NetSpec{.name = NetName{"OTHER"}});
    auto components = std::vector<ComponentId>{};
    if (options.modules) {
        const auto module = circuit.define_module(
            ModuleSpec{.name = ModuleName{"Contact"},
                       .template_nets = {TemplateNetDefinition{NetName{"N"}, NetKind::Ground}},
                       .components = {ModuleComponentTemplate{def, ReferenceDesignator{"U"}}},
                       .connections = {ModulePinConnectionSpec{
                           NetName{"N"}, ReferenceDesignator{"U"}, definition.pins()[0]}},
                       .ports = {ModulePortSpec{PortName{"P"}, NetName{"N"}, PortRole::Passive}}});
        for (std::size_t i = 0; i < options.components; ++i) {
            const auto instance = circuit.instantiate_module(
                module,
                ModuleInstanceSpec{.name = ModuleInstanceName{"M" + std::to_string(i + 1)}});
            components.push_back(*queries::concrete_component_for(
                circuit, instance, circuit.get(module).components()[0]));
            static_cast<void>(circuit.bind_port(instance, circuit.get(module).ports()[0], net));
        }
    } else {
        for (std::size_t i = 0; i < options.components; ++i) {
            const auto component = circuit.instantiate_component(
                def, ComponentInstanceSpec{.reference =
                                               ReferenceDesignator{"U" + std::to_string(i + 1)}});
            components.push_back(component);
            const auto pin = *queries::pin_by_name(circuit, component, "P");
            if (options.connected && (i == 0 || options.second_connected))
                static_cast<void>(
                    circuit.connect(options.different_nets && i > 0 ? other : net, pin));
            if (options.explicit_nc)
                circuit.mark_no_connect(pin);
        }
    }
    for (const auto component : components)
        if (options.selected && (component == components.front() || options.second_selected))
            circuit.update(component, SelectLibraryPart{bundle, bundle.require(key)});
    if (options.second_dnp && components.size() > 1)
        circuit.update(components[1], SetAssemblyIntent{.dnp = true});
    auto board = Board{circuit, BoardName{"Synthetic completion"}};
    const auto front =
        board.add_layer(BoardLayer{"F.Cu", BoardLayerRole::Copper, BoardLayerSide::Top});
    const auto back =
        board.add_layer(BoardLayer{"B.Cu", BoardLayerRole::Copper, BoardLayerSide::Bottom});
    const auto inner =
        options.bad_span ? std::optional{board.add_layer(
                               BoardLayer{"In.Cu", BoardLayerRole::Copper, BoardLayerSide::Inner})}
                         : std::nullopt;
    board.set_layer_stack(LayerStack{{front, back}, 1.6});
    board.set_outline(
        BoardOutline::rectangle({options.outline_x, options.outline_shift}, {30, 20}));
    for (std::size_t i = 0; i < std::min(options.placements, components.size()); ++i)
        static_cast<void>(board.place_component(
            ComponentPlacement{components[i],
                               {i == 0 ? options.first_x : options.second_x, 10},
                               BoardRotation::degrees(0),
                               options.interlayer && i == 0 ? BoardSide::Bottom : BoardSide::Top}));
    if (options.zone) {
        const auto outline = std::vector<BoardPoint>{{5, 5},
                                                     {options.shrink ? 15 : options.zone_right, 5},
                                                     {options.shrink ? 15 : options.zone_right, 15},
                                                     {5, 15}};
        static_cast<void>(board.add_zone(
            BoardZone{outline, {front}, options.netless_zone ? std::nullopt : std::optional{net}}));
        if (options.interlayer)
            static_cast<void>(board.add_zone(BoardZone{outline, {back}, net}));
    }
    if (options.via)
        static_cast<void>(board.add_via(BoardVia{net,
                                                 {15, 10},
                                                 front,
                                                 inner.value_or(back),
                                                 options.drill_off_grid ? 0.3000004 : 0.3,
                                                 0.8}));
    if (options.tracks)
        static_cast<void>(board.add_track(BoardTrack{net, front, {{10, 10}, {20, 10}}, 0.2}));
    if (options.fab_critical_slot)
        static_cast<void>(board.add_feature(BoardFeature::slot("SLOT", {2, 2}, {3, 2}, 0.5)));
    if (options.nc_contact)
        static_cast<void>(board.add_track(BoardTrack{net, front, {{9, 10}, {11, 10}}, 0.2}));
    if (options.drill_only) {
        static_cast<void>(board.add_via(BoardVia{net, {15, 10}, front, back, 0.6, 1.0}));
        static_cast<void>(board.add_zone(
            BoardZone{{{14.9, 9.9}, {15.1, 9.9}, {15.1, 10.1}, {14.9, 10.1}}, {front}, other}));
    }
    if (options.severing_hole)
        static_cast<void>(board.add_feature(BoardFeature::hole("CUT", {15, 5}, 1.0)));
    if (options.edge_text) {
        const auto silk =
            board.add_layer(BoardLayer{"F.SilkS", BoardLayerRole::Silkscreen, BoardLayerSide::Top});
        static_cast<void>(board.add_text(
            BoardText{"A", {9999, 10}, BoardRotation::degrees(0), silk, 4.0, false}));
    }
    // The native compiler, not the fixture, owns closure consistency and mapping validation.
    return compile_export_fixture(circuit, std::move(board), bundle);
}
} // namespace volt::test
