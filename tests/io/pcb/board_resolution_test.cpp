#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/pcb/board_resolution.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace {

[[nodiscard]] std::string asset_key(const volt::PartAssetReference &reference) {
    return std::to_string(static_cast<unsigned int>(reference.kind())) + ":" + reference.key();
}

class MemoryResolver final : public volt::PartAssetResolver {
  public:
    void add(const volt::PartAssetReference &reference, std::string bytes) {
        assets_.insert_or_assign(asset_key(reference), std::move(bytes));
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto match = assets_.find(asset_key(reference));
        return match == assets_.end() ? std::nullopt : std::optional{match->second};
    }

  private:
    std::map<std::string, std::string> assets_;
};

[[nodiscard]] volt::ComponentSpec resistor_spec() {
    return volt::ComponentSpec{
        .name = "Resolved resistor",
        .pins = {volt::PinSpec{.name = "A", .number = "1"},
                 volt::PinSpec{.name = "B", .number = "2"}},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.resistor@1"},
                .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"B"}},
            },
    };
}

[[nodiscard]] std::vector<volt::PartFootprintPad>
part_pads(const volt::FootprintDefinition &footprint) {
    auto result = std::vector<volt::PartFootprintPad>{};
    for (const auto &pad : footprint.pads()) {
        result.emplace_back(pad.label(), pad.position().x_mm(), pad.position().y_mm(),
                            pad.size().width_mm(), pad.size().height_mm());
    }
    return result;
}

struct ResolutionFixture {
    volt::ComponentSpec spec;
    volt::PartLibraryBuilder builder;
    MemoryResolver resolver;
    volt::PartKey key;
};

[[nodiscard]] ResolutionFixture resolution_fixture(std::string footprint_bytes = {}) {
    auto spec = resistor_spec();
    auto component_circuit = volt::Circuit{};
    const auto definition_id = component_circuit.define_component(spec);
    const auto &definition = component_circuit.get(definition_id);
    const auto footprint = volt::passive_0603_footprint();
    if (footprint_bytes.empty()) {
        footprint_bytes = volt::io::write_footprint_asset(footprint);
    }
    const auto footprint_reference = volt::PartAssetReference{
        volt::PartAssetKind::Footprint,
        "footprint:" + footprint.ref().library() + "/" + footprint.ref().name(),
        volt::sha256_content_hash(footprint_bytes)};
    const auto key = volt::PartKey{"resolved-resistor"};
    auto part = volt::PartDefinition{
        definition,
        volt::PartIdentity{"test.parts", key.value(), "1"},
        volt::ElectricalRecordSet{2},
        {volt::PinPackageTerminalMapping{volt::PinKey{"A"}, {volt::PackageTerminalKey{"1"}}},
         volt::PinPackageTerminalMapping{volt::PinKey{"B"}, {volt::PackageTerminalKey{"2"}}}},
        {},
        volt::PartProvenance{},
        {},
        volt::OrderablePart{
            volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
            volt::PackageRef{"0603"},
            volt::HashedFootprintReference{footprint.ref(), footprint_reference.digest()},
            part_pads(footprint),
            {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                             {volt::FootprintPadKey{"1"}}},
             volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                             {volt::FootprintPadKey{"2"}}}}}};
    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.parts", "1", volt::PartLibrarySchemaVersion::V1}};
    builder.add_component(spec).add_part(std::move(part));
    auto resolver = MemoryResolver{};
    resolver.add(footprint_reference, std::move(footprint_bytes));
    return ResolutionFixture{std::move(spec), std::move(builder), std::move(resolver), key};
}

struct SelectedCircuit {
    volt::Circuit circuit;
    volt::ComponentId component;
};

[[nodiscard]] SelectedCircuit selected_circuit(const volt::ComponentSpec &spec,
                                               const volt::io::PartLibraryBundle &bundle,
                                               const volt::PartKey &key) {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(spec);
    const auto component = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto reference = bundle.require(key);
    circuit.update(component, volt::SelectLibraryPart{bundle, reference});
    return SelectedCircuit{std::move(circuit), component};
}

} // namespace

TEST_CASE("Footprint assets round-trip complete native geometry deterministically") {
    const auto outline = volt::FootprintPolygon{std::vector{
        volt::FootprintPoint{-2.0, -1.0},
        volt::FootprintPoint{2.0, -1.0},
        volt::FootprintPoint{2.0, 1.0},
        volt::FootprintPoint{-2.0, 1.0},
    }};
    const auto footprint = volt::FootprintDefinition{
        volt::FootprintRef{"test", "Complete"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::RoundedRectangle, volt::FootprintPoint{-1.0, 0.0},
                volt::FootprintSize{0.8, 1.1}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "EP", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
                volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::back_smd(),
                volt::FootprintPadMechanicalRole::Thermal),
            volt::FootprintPad::through_hole(
                "M1", volt::FootprintPadShape::Circle, volt::FootprintPoint{1.4, 0.0},
                volt::FootprintSize{1.2, 1.2}, volt::FootprintLayerSet::mechanical_hole(),
                volt::FootprintDrill{0.7, volt::FootprintPadPlating::NonPlated},
                volt::FootprintPadMechanicalRole::Mounting),
        },
        volt::FootprintPackageGeometry{
            outline,
            outline,
            outline,
            outline,
            std::vector{volt::FootprintMarking{
                volt::FootprintMarkingKind::PinOne,
                volt::FootprintPolygon{std::vector{
                    volt::FootprintPoint{-1.8, 0.6},
                    volt::FootprintPoint{-1.5, 0.6},
                    volt::FootprintPoint{-1.8, 0.9},
                }},
            }},
        }};
    const auto first = volt::io::write_footprint_asset(footprint);
    const auto reopened = volt::io::read_footprint_asset(first);
    CHECK(reopened == footprint);
    CHECK(volt::io::write_footprint_asset(reopened) == first);
}

TEST_CASE("Board resolution uses one exact closure deterministically without mutating authoring") {
    auto fixture = resolution_fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        fixture.builder, std::vector{fixture.key}, fixture.resolver);
    auto selected = selected_circuit(fixture.spec, bundle, fixture.key);
    const auto reference = bundle.require(fixture.key);
    auto board = volt::Board{selected.circuit, volt::BoardName{"Main"}};
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{selected.component, volt::BoardPoint{10.0, 5.0},
                                 volt::BoardRotation::degrees(90.0), volt::BoardSide::Top, false}));

    const auto first = volt::io::resolve_board(
        board, bundle, volt::BoardResolutionCapabilities{board.capability_profile()});
    const auto second = volt::io::resolve_board(
        board, bundle, volt::BoardResolutionCapabilities{board.capability_profile()});

    CHECK(first.board_name().value() == "Main");
    CHECK(first.closure_digest() == bundle.digest());
    CHECK(second.closure_digest() == first.closure_digest());
    REQUIRE(first.part(selected.component) != nullptr);
    CHECK(first.part(selected.component)->reference() == reference);
    CHECK(first.footprints().definitions() == second.footprints().definitions());
    CHECK(volt::queries::selected_library_part_ref(selected.circuit, selected.component) ==
          reference);
    CHECK_FALSE(
        volt::queries::selected_physical_part(selected.circuit, selected.component).has_value());
    CHECK(volt::queries::selected_physical_part(first.board().circuit(), selected.component)
              .has_value());

    auto rejected_projection = selected.circuit;
    const auto wrong_reference = volt::LibraryPartRef{
        reference.library_namespace(), reference.library_version(), volt::PartKey{"other"},
        reference.library_digest(), reference.part_digest()};
    CHECK_THROWS_AS(rejected_projection.update(
                        selected.component,
                        volt::SelectPhysicalPart{first.part(selected.component)->physical_part(),
                                                 wrong_reference}),
                    volt::KernelLogicError);
    CHECK(volt::queries::selected_library_part_ref(rejected_projection, selected.component) ==
          reference);
    CHECK_FALSE(
        volt::queries::selected_physical_part(rejected_projection, selected.component).has_value());
}

TEST_CASE("Named Board resolutions remain independent over one selected closure") {
    auto fixture = resolution_fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        fixture.builder, std::vector{fixture.key}, fixture.resolver);
    auto selected = selected_circuit(fixture.spec, bundle, fixture.key);
    auto first_board = volt::Board{selected.circuit, volt::BoardName{"First"}};
    auto second_board = volt::Board{selected.circuit, volt::BoardName{"Second"}};
    static_cast<void>(first_board.place_component(
        volt::ComponentPlacement{selected.component, volt::BoardPoint{1.0, 2.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, false}));
    static_cast<void>(second_board.place_component(volt::ComponentPlacement{
        selected.component, volt::BoardPoint{9.0, 8.0}, volt::BoardRotation::degrees(180.0),
        volt::BoardSide::Bottom, false}));

    const auto first = volt::io::resolve_board(first_board, bundle,
                                               volt::BoardResolutionCapabilities{std::nullopt});
    const auto second = volt::io::resolve_board(second_board, bundle,
                                                volt::BoardResolutionCapabilities{std::nullopt});

    CHECK(first.authoring_board().name().value() == "First");
    CHECK(second.authoring_board().name().value() == "Second");
    CHECK(first.board().get(volt::ComponentPlacementId{0}).position() ==
          volt::BoardPoint{1.0, 2.0});
    CHECK(second.board().get(volt::ComponentPlacementId{0}).position() ==
          volt::BoardPoint{9.0, 8.0});
}

TEST_CASE("Board resolution rejects bad assets and wrong closures atomically") {
    auto fixture = resolution_fixture("{}");
    const auto bad_asset_bundle = volt::io::PartLibraryBundle::build(
        fixture.builder, std::vector{fixture.key}, fixture.resolver);
    auto selected = selected_circuit(fixture.spec, bad_asset_bundle, fixture.key);
    auto board = volt::Board{selected.circuit, volt::BoardName{"Main"}};

    CHECK_THROWS_AS(
        volt::io::resolve_board(board, bad_asset_bundle,
                                volt::BoardResolutionCapabilities{board.capability_profile()}),
        volt::KernelArgumentError);
    CHECK(board.all<volt::FootprintDefId>().size() == 0U);
    CHECK_FALSE(
        volt::queries::selected_physical_part(selected.circuit, selected.component).has_value());

    auto complete_fixture = resolution_fixture();
    const auto complete = volt::io::PartLibraryBundle::build(
        complete_fixture.builder, std::vector{complete_fixture.key}, complete_fixture.resolver);
    const auto empty = volt::io::PartLibraryBundle::build(
        complete_fixture.builder, std::vector<volt::PartKey>{}, complete_fixture.resolver);
    auto complete_selected =
        selected_circuit(complete_fixture.spec, complete, complete_fixture.key);
    auto complete_board = volt::Board{complete_selected.circuit, volt::BoardName{"Main"}};
    CHECK_THROWS_AS(volt::io::resolve_board(
                        complete_board, empty,
                        volt::BoardResolutionCapabilities{complete_board.capability_profile()}),
                    volt::KernelRangeError);
    CHECK_FALSE(volt::queries::selected_physical_part(complete_selected.circuit,
                                                      complete_selected.component)
                    .has_value());

    auto cached_board = volt::Board{complete_selected.circuit, volt::BoardName{"Cached"}};
    static_cast<void>(cached_board.cache_footprint_definition(volt::passive_0603_footprint()));
    CHECK_THROWS_AS(volt::io::resolve_board(
                        cached_board, complete,
                        volt::BoardResolutionCapabilities{cached_board.capability_profile()}),
                    volt::KernelLogicError);
    CHECK(cached_board.all<volt::FootprintDefId>().size() == 1U);
    CHECK_FALSE(volt::queries::selected_physical_part(complete_selected.circuit,
                                                      complete_selected.component)
                    .has_value());

    auto profiled_board = volt::Board{complete_selected.circuit, volt::BoardName{"Profiled"}};
    profiled_board.set_capability_profile(volt::BoardCapabilityProfile{
        "Exact fab",
        volt::BoardCapabilityProvenance{"Native fixture", "2026-07-22"},
        0.1,
        0.2,
        0.4,
        {}});
    CHECK_THROWS_AS(volt::io::resolve_board(profiled_board, complete,
                                            volt::BoardResolutionCapabilities{std::nullopt}),
                    volt::KernelLogicError);
    CHECK(profiled_board.all<volt::FootprintDefId>().size() == 0U);
    CHECK_FALSE(volt::queries::selected_physical_part(complete_selected.circuit,
                                                      complete_selected.component)
                    .has_value());
}
