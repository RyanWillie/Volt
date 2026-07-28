#include "project_bundle_v2_board_test_support.hpp"

namespace volt::test::project_bundle_v2 {

BoardFixture mixed_footprint_board_fixture() {
    auto circuit = std::make_unique<volt::Circuit>();
    const auto first_spec = volt::ComponentSpec{
        .name = "Small part",
        .pins = {volt::PinSpec{.name = "A", .number = "1"}},
        .source = volt::DefinitionSource{"test.project", "small", "1"},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.project/small@1"},
                .pin_keys = {volt::PinKey{"A"}},
            },
    };
    const auto second_spec = volt::ComponentSpec{
        .name = "Large part",
        .pins = {volt::PinSpec{.name = "A", .number = "1"},
                 volt::PinSpec{.name = "B", .number = "2"}},
        .source = volt::DefinitionSource{"test.project", "large", "1"},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.project/large@1"},
                .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"B"}},
            },
    };
    const auto first_definition = circuit->define_component(first_spec);
    const auto second_definition = circuit->define_component(second_spec);
    const auto first_component = circuit->instantiate_component(
        first_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto second_component = circuit->instantiate_component(
        second_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto first_footprint = volt::FootprintDefinition{
        volt::FootprintRef{"test.project", "Small"},
        std::vector{volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd())}};
    const auto second_footprint = volt::FootprintDefinition{
        volt::FootprintRef{"test.project", "Large"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-0.5, 0.0},
                volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.5, 0.0},
                volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd()),
        }};
    const auto first_physical = volt::PhysicalPart{
        volt::ManufacturerPart{"Volt", "SMALL"}, volt::PackageRef{"small"}, first_footprint.ref(),
        std::vector{volt::PinPadMapping{circuit->get(first_definition).pins().front(), "1"}}};
    const auto &second_pins = circuit->get(second_definition).pins();
    const auto second_physical = volt::PhysicalPart{
        volt::ManufacturerPart{"Volt", "LARGE"}, volt::PackageRef{"large"}, second_footprint.ref(),
        std::vector{volt::PinPadMapping{second_pins[0], "1"},
                    volt::PinPadMapping{second_pins[1], "2"}}};
    auto library = volt::test::make_export_fixture_library(
        {{first_spec, first_physical, first_footprint, volt::PartKey{"small"}},
         {second_spec, second_physical, second_footprint, volt::PartKey{"large"}}});
    circuit->update(first_component, volt::SelectLibraryPart{
                                         library.bundle, library.bundle.require(library.keys[0])});
    circuit->update(second_component, volt::SelectLibraryPart{
                                          library.bundle, library.bundle.require(library.keys[1])});

    auto board = volt::Board{*circuit, volt::BoardName{"Main"}};
    board.set_capability_profile(volt::test::export_fixture_profile());
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{first_component, volt::BoardPoint{5.0, 5.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, false}));
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{second_component, volt::BoardPoint{15.0, 5.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, false}));

    auto compiled = volt::test::compile_export_fixture(*circuit, board, library.bundle);
    auto scene = volt::prepare_board_scene(compiled);
    return BoardFixture{std::move(circuit), std::move(library.bundle), std::move(board),
                        std::move(compiled), std::move(scene)};
}

} // namespace volt::test::project_bundle_v2
