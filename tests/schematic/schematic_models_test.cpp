#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <volt/schematic/queries.hpp>
#include <volt/schematic/schematic.hpp>

#include "schematic_test_helpers.hpp"

namespace {

template <typename Schematic>
concept CanReadTemporarySchematicRange =
    requires(Schematic schematic) { std::move(schematic).template all<volt::SheetId>(); };

static_assert(volt::SchematicEntityId<volt::SymbolDefId>);
static_assert(volt::SchematicEntityId<volt::SheetId>);
static_assert(volt::SchematicEntityId<volt::SheetRegionId>);
static_assert(volt::SchematicEntityId<volt::SymbolInstanceId>);
static_assert(volt::SchematicEntityId<volt::WireRunId>);
static_assert(volt::SchematicEntityId<volt::NetLabelId>);
static_assert(volt::SchematicEntityId<volt::JunctionId>);
static_assert(volt::SchematicEntityId<volt::PowerPortId>);
static_assert(volt::SchematicEntityId<volt::NoConnectMarkerId>);
static_assert(volt::SchematicEntityId<volt::SheetPortId>);
static_assert(volt::SchematicEntityId<volt::SymbolFieldId>);
static_assert(!volt::SchematicEntityId<volt::NetId>);
static_assert(!CanReadTemporarySchematicRange<volt::Schematic>);
static_assert(std::forward_iterator<volt::schematic_entity_range_t<volt::SheetId>::iterator>);
static_assert(std::same_as<volt::schematic_entity_type_t<volt::SheetId>, volt::Sheet>);

} // namespace

TEST_CASE("Schematic owns unique symbol definitions behind generic typed reads") {
    auto circuit = volt::Circuit{};
    auto schematic = volt::Schematic{circuit};
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());

    CHECK(symbol == volt::SymbolDefId{0});
    CHECK(schematic.all<volt::SymbolDefId>().size() == 1);
    CHECK(schematic.get(symbol).name() == "Resistor");
    const auto &query_owner = std::as_const(schematic);
    CHECK(volt::queries::symbol_definition_by_name(query_owner, "Resistor") == symbol);
    CHECK_FALSE(volt::queries::symbol_definition_by_name(query_owner, "Missing").has_value());
    CHECK_THROWS_AS(schematic.add_symbol_definition(make_resistor_symbol()), std::logic_error);
    check_kernel_error(
        [&] { static_cast<void>(schematic.add_symbol_definition(make_resistor_symbol())); },
        volt::ErrorCode::DuplicateName, "Symbol definition name already exists");
}

TEST_CASE("Schematic owns sheets and authored regions behind generic typed reads") {
    auto circuit = volt::Circuit{};
    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto region = schematic.add_sheet_region(
        sheet, volt::SheetRegion{"power", "Power", volt::SheetRegionBounds{0.0, 0.0, 20.0, 10.0}});

    const auto sheets = schematic.all<volt::SheetId>();
    CHECK(sheets.size() == 1);
    CHECK(sheets.begin() == sheets.begin());
    CHECK(sheets.begin() != sheets.end());
    CHECK(schematic.get(region).name() == "power");
    const auto &query_owner = std::as_const(schematic);
    CHECK(volt::queries::sheet_by_name(query_owner, "Main") == sheet);
    CHECK(volt::queries::sheet_region_by_name(query_owner, sheet, "power") == region);
    CHECK_THROWS_AS(schematic.add_sheet(volt::Sheet{"Main"}), std::logic_error);
    CHECK_THROWS_AS(schematic.add_sheet_region(
                        sheet, volt::SheetRegion{"power", "Power",
                                                 volt::SheetRegionBounds{0.0, 0.0, 20.0, 10.0}}),
                    std::logic_error);
    check_kernel_error([&] { static_cast<void>(schematic.add_sheet(volt::Sheet{"Main"})); },
                       volt::ErrorCode::DuplicateName, "Sheet name already exists");
    check_kernel_error(
        [&] {
            static_cast<void>(schematic.add_sheet_region(
                sheet, volt::SheetRegion{"power", "Power",
                                         volt::SheetRegionBounds{0.0, 0.0, 20.0, 10.0}}));
        },
        volt::ErrorCode::DuplicateName, "Sheet region name already exists");
}

TEST_CASE("Schematic indexes regions already present on an added sheet") {
    auto circuit = volt::Circuit{};
    auto source = volt::Schematic{circuit};
    const auto source_sheet = source.add_sheet(volt::Sheet{"Source"});
    static_cast<void>(source.add_sheet_region(
        source_sheet,
        volt::SheetRegion{"power", "Power", volt::SheetRegionBounds{0.0, 0.0, 20.0, 10.0}}));

    auto target = volt::Schematic{circuit};
    const auto target_sheet = target.add_sheet(source.get(source_sheet));

    REQUIRE(target.all<volt::SheetRegionId>().size() == 1);
    const auto region = volt::queries::sheet_region_by_name(target, target_sheet, "power");
    REQUIRE(region.has_value());
    CHECK(target.get(region.value()).title() == "Power");
}
