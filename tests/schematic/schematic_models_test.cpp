#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <stdexcept>
#include <vector>

#include <volt/schematic/schematic_items_model.hpp>
#include <volt/schematic/schematic_library_model.hpp>
#include <volt/schematic/schematic_sheet_model.hpp>

#include "schematic_test_helpers.hpp"

namespace {

template <typename Model>
concept HasMutableSheetAccessor = requires(Model model, volt::SheetId sheet) {
    { model.sheet(sheet) } -> std::same_as<volt::Sheet &>;
};

static_assert(!HasMutableSheetAccessor<volt::SchematicSheetModel>);

} // namespace

TEST_CASE("SchematicLibraryModel owns unique symbol definitions") {
    auto model = volt::SchematicLibraryModel{};
    const auto symbol = model.add_symbol_definition(make_resistor_symbol());

    CHECK(symbol == volt::SymbolDefId{0});
    CHECK(model.symbol_definition_count() == 1);
    CHECK(model.symbol_definition(symbol).name() == "Resistor");
    CHECK(model.symbol_definition_by_name("Resistor") == symbol);
    CHECK_FALSE(model.symbol_definition_by_name("Missing").has_value());
    CHECK_THROWS_AS(model.add_symbol_definition(make_resistor_symbol()), std::logic_error);
}

TEST_CASE("SchematicSheetModel owns sheets, regions, and sheet membership lists") {
    auto model = volt::SchematicSheetModel{};
    const auto sheet = model.add_sheet(volt::Sheet{"Main"});
    const auto region = model.add_sheet_region(
        sheet, volt::SheetRegion{"power", "Power", volt::SheetRegionBounds{0.0, 0.0, 20.0, 10.0}});

    model.add_symbol_instance(sheet, volt::SymbolInstanceId{2});
    model.add_wire_run(sheet, volt::WireRunId{3});
    model.add_net_label(sheet, volt::NetLabelId{4});
    model.add_junction(sheet, volt::JunctionId{5});
    model.add_power_port(sheet, volt::PowerPortId{6});
    model.add_no_connect_marker(sheet, volt::NoConnectMarkerId{7});
    model.add_sheet_port(sheet, volt::SheetPortId{8});
    model.add_symbol_field(sheet, volt::SymbolFieldId{9});

    CHECK(model.sheet_count() == 1);
    CHECK(model.sheet_by_name("Main") == sheet);
    CHECK(model.sheet_region_by_name(sheet, "power") == region);
    CHECK(model.sheet(sheet).symbol_instances() == std::vector{volt::SymbolInstanceId{2}});
    CHECK(model.sheet(sheet).wire_runs() == std::vector{volt::WireRunId{3}});
    CHECK(model.sheet(sheet).net_labels() == std::vector{volt::NetLabelId{4}});
    CHECK(model.sheet(sheet).junctions() == std::vector{volt::JunctionId{5}});
    CHECK(model.sheet(sheet).power_ports() == std::vector{volt::PowerPortId{6}});
    CHECK(model.sheet(sheet).no_connect_markers() == std::vector{volt::NoConnectMarkerId{7}});
    CHECK(model.sheet(sheet).sheet_ports() == std::vector{volt::SheetPortId{8}});
    CHECK(model.sheet(sheet).symbol_fields() == std::vector{volt::SymbolFieldId{9}});
    CHECK_THROWS_AS(model.add_sheet(volt::Sheet{"Main"}), std::logic_error);
    CHECK_THROWS_AS(model.add_sheet_region(
                        sheet, volt::SheetRegion{"power", "Power",
                                                 volt::SheetRegionBounds{0.0, 0.0, 20.0, 10.0}}),
                    std::logic_error);
}

TEST_CASE("SchematicItemsModel owns presentation items and text movement") {
    auto model = volt::SchematicItemsModel{};
    const auto symbol = model.add_symbol_instance(
        volt::SymbolInstance{volt::SymbolDefId{1}, volt::ComponentId{2}, volt::Point{3.0, 4.0}});
    const auto wire = model.add_wire_run(
        volt::WireRun{volt::NetId{5}, std::vector{volt::Point{0.0, 0.0}, volt::Point{10.0, 0.0}}});
    const auto label = model.add_net_label(volt::NetLabel{volt::NetId{5}, volt::Point{1.0, 1.0}});
    const auto junction = model.add_junction(volt::Junction{volt::NetId{5}, volt::Point{2.0, 2.0}});
    const auto power = model.add_power_port(
        volt::PowerPort{volt::NetId{5}, volt::PowerPortKind::Power, volt::Point{3.0, 3.0}});
    const auto no_connect =
        model.add_no_connect_marker(volt::NoConnectMarker{volt::PinId{6}, volt::Point{4.0, 4.0}});
    const auto sheet_port = model.add_sheet_port(volt::SheetPort{
        volt::NetId{5}, "VIN", volt::SheetPortKind::OffPage, volt::Point{5.0, 5.0}});
    const auto field =
        model.add_symbol_field(volt::SymbolField{symbol, "value", "10k", volt::Point{6.0, 6.0}});

    model.move_net_label_text(label, volt::Point{7.0, 7.0});
    model.move_power_port_label(power, volt::Point{8.0, 8.0});
    model.move_symbol_field(field, volt::Point{9.0, 9.0});

    CHECK(model.symbol_instance(symbol).component() == volt::ComponentId{2});
    CHECK(model.wire_run(wire).net() == volt::NetId{5});
    CHECK(model.net_label(label).text_position() == volt::Point{7.0, 7.0});
    CHECK(model.junction(junction).position() == volt::Point{2.0, 2.0});
    CHECK(model.power_port(power).explicit_label_position() == volt::Point{8.0, 8.0});
    CHECK(model.no_connect_marker(no_connect).pin() == volt::PinId{6});
    CHECK(model.sheet_port(sheet_port).name() == "VIN");
    CHECK(model.symbol_field(field).position() == volt::Point{9.0, 9.0});
    CHECK(model.symbol_instance_count() == 1);
    CHECK(model.wire_run_count() == 1);
    CHECK(model.net_label_count() == 1);
    CHECK(model.junction_count() == 1);
    CHECK(model.power_port_count() == 1);
    CHECK(model.no_connect_marker_count() == 1);
    CHECK(model.sheet_port_count() == 1);
    CHECK(model.symbol_field_count() == 1);
    CHECK_THROWS_AS(model.add_symbol_field(volt::SymbolField{volt::SymbolInstanceId{99}, "bad",
                                                             "dangling", volt::Point{0.0, 0.0}}),
                    std::out_of_range);
}
