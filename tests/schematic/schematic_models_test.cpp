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

template <typename Model>
concept CanAddSheetSymbolInstance =
    requires(Model model, volt::SheetId sheet, volt::SymbolInstanceId instance) {
        model.add_symbol_instance(sheet, instance);
    };

template <typename Model>
concept CanAddSheetWireRun = requires(Model model, volt::SheetId sheet, volt::WireRunId wire) {
    model.add_wire_run(sheet, wire);
};

template <typename Model>
concept CanAddSheetNetLabel = requires(Model model, volt::SheetId sheet, volt::NetLabelId label) {
    model.add_net_label(sheet, label);
};

template <typename Model>
concept CanAddSheetJunction =
    requires(Model model, volt::SheetId sheet, volt::JunctionId junction) {
        model.add_junction(sheet, junction);
    };

template <typename Model>
concept CanAddSheetPowerPort = requires(Model model, volt::SheetId sheet, volt::PowerPortId port) {
    model.add_power_port(sheet, port);
};

template <typename Model>
concept CanAddSheetNoConnect =
    requires(Model model, volt::SheetId sheet, volt::NoConnectMarkerId marker) {
        model.add_no_connect_marker(sheet, marker);
    };

template <typename Model>
concept CanAddSheetPortMembership =
    requires(Model model, volt::SheetId sheet, volt::SheetPortId port) {
        model.add_sheet_port(sheet, port);
    };

template <typename Model>
concept CanAddSheetSymbolField =
    requires(Model model, volt::SheetId sheet, volt::SymbolFieldId field) {
        model.add_symbol_field(sheet, field);
    };

template <typename Model>
concept CanAddSchematicSymbolInstance =
    requires(Model model, volt::SymbolInstance instance) { model.add_symbol_instance(instance); };

template <typename Model>
concept CanAddSchematicWireRun =
    requires(Model model, volt::WireRun wire) { model.add_wire_run(wire); };

template <typename Model>
concept CanAddSchematicNetLabel =
    requires(Model model, volt::NetLabel label) { model.add_net_label(label); };

template <typename Model>
concept CanAddSchematicJunction =
    requires(Model model, volt::Junction junction) { model.add_junction(junction); };

template <typename Model>
concept CanAddSchematicPowerPort =
    requires(Model model, volt::PowerPort port) { model.add_power_port(port); };

template <typename Model>
concept CanAddSchematicNoConnect =
    requires(Model model, volt::NoConnectMarker marker) { model.add_no_connect_marker(marker); };

template <typename Model>
concept CanAddSchematicSheetPort =
    requires(Model model, volt::SheetPort port) { model.add_sheet_port(port); };

template <typename Model>
concept CanAddSchematicSymbolField =
    requires(Model model, volt::SymbolField field) { model.add_symbol_field(field); };

static_assert(!HasMutableSheetAccessor<volt::SchematicSheetModel>);
static_assert(CanAddSheetSymbolInstance<volt::SchematicSheetModel>);
static_assert(CanAddSheetWireRun<volt::SchematicSheetModel>);
static_assert(CanAddSheetNetLabel<volt::SchematicSheetModel>);
static_assert(CanAddSheetJunction<volt::SchematicSheetModel>);
static_assert(CanAddSheetPowerPort<volt::SchematicSheetModel>);
static_assert(CanAddSheetNoConnect<volt::SchematicSheetModel>);
static_assert(CanAddSheetPortMembership<volt::SchematicSheetModel>);
static_assert(CanAddSheetSymbolField<volt::SchematicSheetModel>);
static_assert(CanAddSchematicSymbolInstance<volt::SchematicItemsModel>);
static_assert(CanAddSchematicWireRun<volt::SchematicItemsModel>);
static_assert(CanAddSchematicNetLabel<volt::SchematicItemsModel>);
static_assert(CanAddSchematicJunction<volt::SchematicItemsModel>);
static_assert(CanAddSchematicPowerPort<volt::SchematicItemsModel>);
static_assert(CanAddSchematicNoConnect<volt::SchematicItemsModel>);
static_assert(CanAddSchematicSheetPort<volt::SchematicItemsModel>);
static_assert(CanAddSchematicSymbolField<volt::SchematicItemsModel>);

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

TEST_CASE("SchematicSheetModel owns sheets and authored regions") {
    auto model = volt::SchematicSheetModel{};
    const auto sheet = model.add_sheet(volt::Sheet{"Main"});
    const auto region = model.add_sheet_region(
        sheet, volt::SheetRegion{"power", "Power", volt::SheetRegionBounds{0.0, 0.0, 20.0, 10.0}});

    CHECK(model.sheet_count() == 1);
    CHECK(model.sheet_by_name("Main") == sheet);
    CHECK(model.sheet_region_by_name(sheet, "power") == region);
    CHECK_THROWS_AS(model.add_sheet(volt::Sheet{"Main"}), std::logic_error);
    CHECK_THROWS_AS(model.add_sheet_region(
                        sheet, volt::SheetRegion{"power", "Power",
                                                 volt::SheetRegionBounds{0.0, 0.0, 20.0, 10.0}}),
                    std::logic_error);
}
