#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/pcb/compiled_board_consumers.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/io/project_bundle_v2_writer.hpp>
#include <volt/io/schematic/schematic_writer.hpp>

#include "support/compiled_board_export_helpers.hpp"

namespace {

using OrderedJson = nlohmann::ordered_json;

class MemoryAssetResolver final : public volt::PartAssetResolver {
  public:
    void add(const volt::PartAssetReference &reference, std::string bytes) {
        assets_.insert_or_assign(reference.key(), std::move(bytes));
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto match = assets_.find(reference.key());
        return match == assets_.end() ? std::nullopt : std::optional{match->second};
    }

  private:
    std::map<std::string, std::string> assets_;
};

class TempDirectory final {
  public:
    TempDirectory() {
        static auto counter = std::atomic<std::uint64_t>{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) /
                ("volt-project-bundle-v2-" + std::to_string(stamp) + "-" +
                 std::to_string(counter.fetch_add(1U)));
        std::filesystem::create_directories(path_);
    }

    TempDirectory(const TempDirectory &) = delete;
    TempDirectory &operator=(const TempDirectory &) = delete;

    ~TempDirectory() {
        auto ignored = std::error_code{};
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

struct BoardFixture {
    std::unique_ptr<volt::Circuit> circuit;
    volt::io::PartLibraryBundle bundle;
    volt::Board board;
    volt::CompiledBoard compiled;
    volt::BoardScene scene;
};

struct SymbolVariantFixture {
    volt::Circuit circuit;
    volt::io::PartLibraryBundle bundle;
};

[[nodiscard]] SymbolVariantFixture symbol_variant_fixture() {
    auto circuit = volt::Circuit{};
    const auto spec = volt::ComponentSpec{
        .name = "Variant resistor",
        .pins = {volt::PinSpec{.name = "A", .number = "1"}},
        .source = volt::DefinitionSource{"test.variant", "resistor", "1"},
        .schematic_symbols = {volt::SchematicSymbolReference{"VariantSymbol", "vertical"}},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.variant/resistor@1"},
                .pin_keys = {volt::PinKey{"A"}},
            },
    };
    const auto definition = circuit.define_component(spec);
    const auto component = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});

    const auto symbol_bytes =
        volt::io::write_symbol_definition(volt::SymbolDefinition{"VariantSymbol"});
    const auto symbol_reference =
        volt::PartAssetReference{volt::PartAssetKind::Schematic, "symbol:VariantSymbol@vertical",
                                 volt::sha256_content_hash(symbol_bytes)};
    const auto footprint = volt::FootprintDefinition{
        volt::FootprintRef{"test.variant", "R1"},
        std::vector{volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd())}};
    const auto footprint_bytes = volt::io::write_footprint_asset(footprint);
    const auto footprint_reference =
        volt::PartAssetReference{volt::PartAssetKind::Footprint, "footprint:test.variant/R1",
                                 volt::sha256_content_hash(footprint_bytes)};

    auto library_builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.variant", "1", volt::PartLibrarySchemaVersion::V1}};
    library_builder.add_component(spec).add_part(volt::PartDefinition{
        circuit.get(definition),
        volt::PartIdentity{"test.variant", "resistor", "1"},
        volt::ElectricalRecordSet{1},
        {volt::PinPackageTerminalMapping{volt::PinKey{"A"}, {volt::PackageTerminalKey{"1"}}}},
        {},
        volt::PartProvenance{},
        {volt::PartSchematicAssetReference{"VariantSymbol", "vertical", symbol_reference.digest()}},
        volt::OrderablePart{
            volt::ManufacturerPart{"Volt", "VARIANT-R1"},
            volt::PackageRef{"0603"},
            volt::HashedFootprintReference{footprint.ref(), footprint_reference.digest()},
            {volt::PartFootprintPad{"1", 0.0, 0.0, 1.0, 1.0}},
            {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                             {volt::FootprintPadKey{"1"}}}},
        }});
    auto resolver = MemoryAssetResolver{};
    resolver.add(symbol_reference, symbol_bytes);
    resolver.add(footprint_reference, footprint_bytes);
    const auto key = volt::PartKey{"resistor"};
    const auto keys = std::vector{key};
    auto bundle = volt::io::PartLibraryBundle::build(library_builder, keys, resolver);
    circuit.update(component, volt::SelectLibraryPart{bundle, bundle.require(key)});
    return SymbolVariantFixture{std::move(circuit), std::move(bundle)};
}

[[nodiscard]] std::string minimal_glb() {
    return std::string{'g', 'l', 'T', 'F', '\x02', '\0', '\0', '\0', '\x0c', '\0', '\0', '\0'};
}

[[nodiscard]] BoardFixture board_fixture(double width = 30.0, bool models3d = false) {
    auto circuit = std::make_unique<volt::Circuit>();
    const auto spec = volt::ComponentSpec{
        .name = "Project resistor",
        .pins = {volt::PinSpec{.name = "A", .number = "1"}},
        .source = volt::DefinitionSource{"test.project", "resistor", "1"},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.project/resistor@1"},
                .pin_keys = {volt::PinKey{"A"}},
            },
    };
    const auto definition = circuit->define_component(spec);
    const auto component = circuit->instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto footprint = volt::FootprintDefinition{
        volt::FootprintRef{"test.project", "R1"},
        std::vector{volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd())}};
    const auto physical = volt::PhysicalPart{
        volt::ManufacturerPart{"Volt", "PROJECT-R1"},
        volt::PackageRef{"0603"},
        footprint.ref(),
        std::vector{volt::PinPadMapping{circuit->get(definition).pins().front(), "1"}},
        {},
        models3d ? std::optional{volt::PartModel3D{"glb", "resistor.glb", {0.0, 0.0, 0.0}, 0.0}}
                 : std::nullopt};
    auto library = volt::test::make_export_fixture_library(
        {{spec, physical, footprint, volt::PartKey{"resistor"},
          models3d ? std::optional{minimal_glb()} : std::nullopt}});
    circuit->update(component, volt::SelectLibraryPart{
                                   library.bundle, library.bundle.require(library.keys.front())});

    auto board = volt::Board{*circuit, volt::BoardName{"Main"}};
    board.set_capability_profile(volt::test::export_fixture_profile());
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{width, 20.0}));
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{component, volt::BoardPoint{5.0, 5.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, false}));

    auto compiled = volt::test::compile_export_fixture(
        *circuit, board, library.bundle,
        models3d ? std::vector{volt::BoardAssetCapability::Models3D}
                 : std::vector<volt::BoardAssetCapability>{});
    auto scene = volt::prepare_board_scene(compiled);
    return BoardFixture{std::move(circuit), std::move(library.bundle), std::move(board),
                        std::move(compiled), std::move(scene)};
}

[[nodiscard]] volt::io::ProjectBundleV2Builder project_builder() {
    return volt::io::ProjectBundleV2Builder{
        volt::io::ProjectIdentity{"board-fixture", std::nullopt, std::nullopt},
        volt::io::ProjectRunSummary{
            true, volt::io::ProjectStatus::Clean, "default", {"design", "board"}},
        volt::io::LogicalInputName{"project.py"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"project.py"}, "project source"}},
        volt::io::ProjectReport{
            R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
}

[[nodiscard]] volt::io::ProjectBundleV2Builder board_builder(const BoardFixture &fixture) {
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
    builder.add_board(volt::io::DesignKey{"main"}, fixture.board, fixture.compiled, fixture.scene,
                      fixture.bundle);
    return builder;
}

} // namespace

TEST_CASE("ProjectBundle v2 reopens a selected part with a non-default symbol variant") {
    const auto fixture = symbol_variant_fixture();
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"main"}, fixture.circuit, fixture.bundle);
    const auto bundle = builder.build();
    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "symbol-variant.volt";
    bundle.write(path);

    const auto manifest = OrderedJson::parse(bundle.manifest_bytes());
    const auto symbol = std::ranges::find(manifest.at("artifacts"), "symbol_definition",
                                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(symbol != manifest.at("artifacts").end());
    CHECK(symbol->at("id").at("owner").at("value").at("key") == "symbol:VariantSymbol@vertical");

    const auto reopened = volt::io::ProjectBundle::open(path);
    CHECK(reopened.require_v2().loaded_project().circuits().size() == 1U);
}

TEST_CASE("ProjectBundle v2 reopens independent multiple named Board alternatives") {
    const auto fixture = board_fixture();
    auto alternative = volt::Board{*fixture.circuit, volt::BoardName{"Alternative"}};
    alternative.set_capability_profile(volt::test::export_fixture_profile());
    const auto front = alternative.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    alternative.set_layer_stack(volt::LayerStack{{front}, 1.6});
    alternative.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{40.0, 25.0}));
    const auto component = volt::ComponentId{0};
    static_cast<void>(alternative.place_component(
        volt::ComponentPlacement{component, volt::BoardPoint{10.0, 8.0},
                                 volt::BoardRotation::degrees(90.0), volt::BoardSide::Top, false}));
    const auto alternative_compiled =
        volt::test::compile_export_fixture(*fixture.circuit, alternative, fixture.bundle);
    const auto alternative_scene = volt::prepare_board_scene(alternative_compiled);

    auto builder = board_builder(fixture);
    builder.add_board(volt::io::DesignKey{"main"}, alternative, alternative_compiled,
                      alternative_scene, fixture.bundle);
    const auto bundle = builder.build();
    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "multiple-boards.volt.zip";
    bundle.write(path);

    const auto reopened = volt::io::ProjectBundle::open(path);
    const auto loaded = reopened.require_v2().loaded_project();
    const auto boards = loaded.boards();
    const auto compiled = loaded.compiled_boards();
    const auto scenes = loaded.board_scenes();
    REQUIRE(boards.size() == 2U);
    REQUIRE(compiled.size() == 2U);
    REQUIRE(scenes.size() == 2U);
    CHECK(boards[0].board().value() == "Alternative");
    CHECK(boards[1].board().value() == "Main");
    CHECK(compiled[0].identity() != compiled[1].identity());
    const auto first_scene_compiled = scenes[0].compiled_board();
    const auto second_scene_compiled = scenes[1].compiled_board();
    CHECK(first_scene_compiled.identity() != second_scene_compiled.identity());
}

TEST_CASE("ProjectBundle v2 pairs same-name Boards by exact typed design edge") {
    const auto first = board_fixture(30.0);
    const auto second = board_fixture(31.0);
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"first"}, *first.circuit, first.bundle);
    builder.add_logical(volt::io::DesignKey{"second"}, *second.circuit, second.bundle);
    builder.add_board(volt::io::DesignKey{"first"}, first.board, first.compiled, first.scene,
                      first.bundle);
    builder.add_board(volt::io::DesignKey{"second"}, second.board, second.compiled, second.scene,
                      second.bundle);
    const auto bundle = builder.build();
    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "same-name-boards.volt";
    bundle.write(path);

    const auto loaded = volt::io::ProjectBundle::open(path).require_v2().loaded_project();
    const auto boards = loaded.boards();
    const auto compiled = loaded.compiled_boards();
    REQUIRE(boards.size() == 2U);
    REQUIRE(compiled.size() == 2U);
    CHECK(boards[0].board().value() == "Main");
    CHECK(boards[1].board().value() == "Main");
    CHECK(boards[0].design().value() == "first");
    CHECK(boards[1].design().value() == "second");
    CHECK(compiled[0].identity() != compiled[1].identity());
}

TEST_CASE("ProjectBundle v2 reopens the exact capability-gated GLB closure") {
    const auto fixture = board_fixture(30.0, true);
    const auto bundle = board_builder(fixture).build();
    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "models3d.volt";
    bundle.write(path);

    const auto reopened = volt::io::ProjectBundle::open(path);
    const auto graph = reopened.require_v2();
    const auto loaded = graph.loaded_project();
    const auto scenes = loaded.board_scenes();
    REQUIRE(scenes.size() == 1U);
    REQUIRE(scenes.front().model().models().size() == 1U);
    CHECK(scenes.front().model().models().front().reference().digest() ==
          volt::sha256_content_hash(minimal_glb()));
    CHECK(std::ranges::count(graph.artifacts(), volt::io::ArtifactKind::GlbAsset,
                             [](const auto &artifact) { return artifact.descriptor().kind(); }) ==
          1);
}

TEST_CASE("ProjectBundle v2 typed leases survive source destruction and owner moves") {
    const auto fixture = board_fixture(30.0, true);
    const auto publication = board_builder(fixture).build();
    const auto temporary = TempDirectory{};
    const auto archive = temporary.path() / "lease.volt.zip";
    publication.write(archive, volt::io::ProjectBundleV2Representation::Zip);

    const auto retained = [&] {
        auto owner = volt::io::ProjectBundle::open(archive);
        auto moved = std::move(owner);
        auto reassigned = volt::io::ProjectBundle::open(archive);
        reassigned = std::move(moved);
        const auto loaded = reassigned.require_v2().loaded_project();
        return std::tuple{loaded.boards().front(), loaded.compiled_boards().front(),
                          loaded.board_scenes().front(), loaded.diagnostics()};
    }();
    std::filesystem::remove(archive);

    const auto &[board, compiled, scene, diagnostics] = retained;
    CHECK(board.board().value() == "Main");
    const auto logical = board.circuit();
    CHECK(logical.model().all<volt::ComponentId>().size() == 1U);
    CHECK(compiled.identity() == fixture.compiled.identity());
    CHECK(compiled.model().content_digest() == fixture.compiled.content_digest());
    const auto scene_compiled = scene.compiled_board();
    CHECK(scene_compiled.artifact().id_json() == compiled.artifact().id_json());
    CHECK(scene.model().models().size() == 1U);
    CHECK_FALSE(diagnostics.bytes().empty());
}
