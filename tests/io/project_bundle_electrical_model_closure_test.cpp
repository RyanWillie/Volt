#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/electrical/passive_model.hpp>
#include <volt/io/parts/part_definition_reader.hpp>
#include <volt/io/parts/part_definition_writer.hpp>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using namespace volt::test::project_bundle_v2;

constexpr auto shared_evidence = "shared model and Voltage evidence";
constexpr auto model_evidence = "model parameter evidence";
constexpr auto current_evidence = "canonical Current evidence";

class EvidenceAssets final : public volt::PartAssetResolver {
  public:
    void add(const volt::PartAssetReference &reference, std::string bytes) {
        assets_.insert_or_assign(reference.key(), std::move(bytes));
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto found = assets_.find(reference.key());
        return found == assets_.end() ? std::nullopt : std::optional{found->second};
    }

  private:
    std::map<std::string, std::string> assets_;
};

struct ElectricalFixture {
    std::unique_ptr<volt::Circuit> circuit;
    volt::io::PartLibraryBundle library;
};

[[nodiscard]] ElectricalFixture electrical_fixture(std::string library_namespace = "test.e2") {
    auto circuit = std::make_unique<volt::Circuit>();
    const auto spec = volt::ComponentSpec{
        .name = "Passive",
        .pins = {volt::PinSpec{.name = "A", .number = "1"},
                 volt::PinSpec{.name = "B", .number = "2"}},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.e2/passive@1"},
                .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"B"}},
            },
    };
    const auto definition = circuit->define_component(spec);
    const auto &component = circuit->get(definition);
    const auto footprint = volt::FootprintDefinition{
        volt::FootprintRef{"test.e2", "passive"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-1.0, 0.0},
                volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{1.0, 0.0},
                volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd()),
        }};
    const auto footprint_bytes = volt::io::write_footprint_asset(footprint);
    auto records = volt::ElectricalRecordSet{2};
    const auto subject = volt::ElectricalSubject::directed_relation(volt::ElectricalPinIndex{0},
                                                                    volt::ElectricalPinIndex{1});
    records = records.with_record(
        volt::voltage_record(subject, volt::ElectricalMeaning::AbsoluteLimit,
                             volt::ElectricalValue{volt::QuantityRange::bounded(
                                 volt::Quantity{volt::UnitDimension::Voltage, -25.0},
                                 volt::Quantity{volt::UnitDimension::Voltage, 25.0})},
                             {}, {volt::sha256_content_hash(shared_evidence)}));
    records = records.with_record(volt::current_record(
        subject, volt::ElectricalMeaning::Characteristic,
        volt::ElectricalValue{volt::Quantity{volt::UnitDimension::Current, 0.01}}, {},
        {volt::sha256_content_hash(current_evidence)}));
    auto model = volt::PartElectricalModelBuilder{component};
    const auto a = model.terminal(volt::ModelTerminalKey{"positive"}, volt::PinKey{"A"});
    const auto b = model.terminal(volt::ModelTerminalKey{"negative"}, volt::PinKey{"B"});
    const auto x = model.internal_node(volt::ModelInternalNodeKey{"after_esr"});
    const auto y = model.internal_node(volt::ModelInternalNodeKey{"after_esl"});
    model.add<volt::ResistanceElement>(
        volt::ModelElementKey{"esr"}, a, x,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, 0.08}});
    model.add<volt::InductanceElement>(
        volt::ModelElementKey{"esl"}, x, y,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Inductance, 1.0e-9}});
    model.add<volt::CapacitanceElement>(
        volt::ModelElementKey{"storage"}, y, b,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Capacitance, 10.0e-6},
                             volt::Tolerance::percent(0.2),
                             {volt::sha256_content_hash(shared_evidence),
                              volt::sha256_content_hash(model_evidence)}});
    const auto glb = std::string{"glTF\2\0\0\0\14\0\0\0", 12U};
    const auto exact_part = volt::PartDefinition{
        component,
        volt::PartIdentity{library_namespace, "capacitor", "1"},
        records,
        {volt::PinPackageTerminalMapping{volt::PinKey{"A"}, {volt::PackageTerminalKey{"1"}}},
         volt::PinPackageTerminalMapping{volt::PinKey{"B"}, {volt::PackageTerminalKey{"2"}}}},
        {},
        volt::PartProvenance{"datasheet", "Volt tests", "illustrative"},
        {},
        volt::OrderablePart{
            volt::ManufacturerPart{"Volt", "E2-C"},
            volt::PackageRef{"0603"},
            volt::HashedFootprintReference{footprint.ref(),
                                           volt::sha256_content_hash(footprint_bytes)},
            {volt::PartFootprintPad{"1", -1.0, 0.0, 1.0, 1.0},
             volt::PartFootprintPad{"2", 1.0, 0.0, 1.0, 1.0}},
            {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                             {volt::FootprintPadKey{"1"}}},
             volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                             {volt::FootprintPadKey{"2"}}}},
            {},
            volt::PartModel3DReference{
                "glb", "unused.glb", volt::sha256_content_hash(glb), {0.0, 0.0, 0.0}, 0.0}},
        model.build()};
    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{library_namespace, "1", volt::PartLibrarySchemaVersion::V1}};
    builder.add_component(spec).add_part(exact_part);
    auto assets = EvidenceAssets{};
    for (const auto &reference : volt::part_asset_references(exact_part)) {
        if (reference.kind() == volt::PartAssetKind::Footprint) {
            assets.add(reference, footprint_bytes);
        } else if (reference.kind() == volt::PartAssetKind::Model3D) {
            assets.add(reference, glb);
        } else if (reference.digest() == volt::sha256_content_hash(shared_evidence)) {
            assets.add(reference, shared_evidence);
        } else {
            assets.add(reference, model_evidence);
        }
    }
    auto attachments = std::vector<volt::io::PartLibraryBundleAttachment>{};
    for (const auto &[kind, key, bytes] : std::array{
             std::tuple{volt::PartAssetKind::Evidence, "evidence:current", current_evidence},
             std::tuple{volt::PartAssetKind::Evidence, "evidence:unrelated", "unrelated document"},
             std::tuple{volt::PartAssetKind::Licence, "licence", "licence bytes"},
             std::tuple{volt::PartAssetKind::Simulation, "simulation", "simulation attachment"}}) {
        const auto reference =
            volt::PartAssetReference{kind, key, volt::sha256_content_hash(bytes)};
        assets.add(reference, bytes);
        attachments.emplace_back(volt::PartKey{"capacitor"}, reference);
    }
    auto library = volt::io::PartLibraryBundle::build(
        builder, std::vector{volt::PartKey{"capacitor"}}, assets, attachments);
    const auto positive = circuit->add_net(volt::NetSpec{.name = volt::NetName{"positive"}});
    const auto negative = circuit->add_net(volt::NetSpec{.name = volt::NetName{"negative"}});
    for (const auto &reference : {"C1", "C2"}) {
        const auto instance = circuit->instantiate_component(
            definition,
            volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{reference}});
        circuit->update(instance, volt::SelectLibraryPart{
                                      library, library.require(volt::PartKey{"capacitor"})});
        circuit->connect(
            positive, *volt::queries::pin_by_definition(*circuit, instance, component.pins()[0]));
        circuit->connect(
            negative, *volt::queries::pin_by_definition(*circuit, instance, component.pins()[1]));
    }
    return {std::move(circuit), std::move(library)};
}

[[nodiscard]] volt::io::ProjectBundlePublication publication(const ElectricalFixture &fixture,
                                                             bool with_board = false) {
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.library);
    if (!with_board) {
        return builder.build();
    }
    auto board = volt::Board{*fixture.circuit, volt::BoardName{"Main"}};
    board.set_capability_profile(volt::test::export_fixture_profile());
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    for (auto index = std::size_t{0}; index < 2U; ++index) {
        static_cast<void>(board.place_component(volt::ComponentPlacement{
            volt::ComponentId{index}, volt::BoardPoint{5.0 + 5.0 * static_cast<double>(index), 5.0},
            volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, false}));
    }
    const auto compiled =
        volt::test::compile_export_fixture(*fixture.circuit, board, fixture.library);
    const auto scene = volt::prepare_board_scene(compiled);
    builder.add_board(volt::io::DesignKey{"main"}, board, compiled, scene, fixture.library);
    return builder.build();
}

} // namespace

TEST_CASE(
    "ProjectBundle preserves selected electrical model and complete evidence without source") {
    auto with_board = false;
    SECTION("logical-only") {}
    SECTION("Board-bearing") { with_board = true; }
    auto temporary = TempDirectory{};
    const auto root = temporary.path() / "project";
    const auto expected = [&] {
        const auto fixture = electrical_fixture();
        const auto &part =
            fixture.library.resolve(fixture.library.require(volt::PartKey{"capacitor"}));
        const auto expected_part = volt::io::write_part_definition(part);
        const auto output = publication(fixture, with_board);
        CHECK(output.archive_bytes() == publication(fixture, with_board).archive_bytes());
        output.write(root);
        return expected_part;
    }();
    const auto bundle = volt::io::ProjectBundle::open(root);
    CHECK(bundle.schema_version() == volt::io::ProjectBundleSchemaVersion::V3);
    const auto graph = bundle.graph();
    const auto loaded = graph.loaded_project();
    CHECK(loaded.boards().size() == (with_board ? 1U : 0U));
    REQUIRE(loaded.circuits().size() == 1U);
    const auto logical = loaded.circuits().front();
    const auto &circuit = logical.model();
    REQUIRE(circuit.all<volt::ComponentId>().size() == 2U);
    const auto first = circuit.get(volt::ComponentId{0}).selected_library_part_ref();
    const auto second = circuit.get(volt::ComponentId{1}).selected_library_part_ref();
    REQUIRE(first.has_value());
    CHECK(first == second);
    const auto part_artifact =
        graph.artifact(volt::io::ArtifactId{volt::io::ArtifactKind::PartDefinition, *first});
    REQUIRE(part_artifact.has_value());
    CHECK(part_artifact->bytes() == expected);
    const auto part = volt::io::read_part_definition_text(part_artifact->bytes(),
                                                          circuit.get(volt::ComponentDefId{0}));
    CHECK(part.content_identity() == first->part_digest());
    REQUIRE(part.electrical_model().has_value());
    CHECK(part.electrical_model()->internal_nodes().size() == 2U);
    CHECK(part.electrical_model()->elements().size() == 3U);
    CHECK(part.electrical_records().records().size() == 2U);
    const auto &storage =
        std::get<volt::CapacitanceElement>(part.electrical_model()->elements().back());
    CHECK(storage.parameter().nominal().value() == 10.0e-6);
    REQUIRE(storage.parameter().tolerance().has_value());
    CHECK(storage.parameter().evidence().size() == 2U);
    const auto artifacts = graph.artifacts();
    CHECK(std::ranges::count_if(artifacts, [](const auto &artifact) {
              return artifact.descriptor().kind() == volt::io::ArtifactKind::PartDefinition;
          }) == 1);
    CHECK(std::ranges::count_if(artifacts, [](const auto &artifact) {
              return artifact.descriptor().kind() == volt::io::ArtifactKind::EvidenceAsset;
          }) == 3);
    for (const auto &bytes : {shared_evidence, model_evidence, current_evidence}) {
        const auto digest = volt::sha256_content_hash(bytes);
        const auto id = volt::io::ArtifactId{
            volt::io::ArtifactKind::EvidenceAsset,
            volt::io::LibraryAssetRef{first->library_namespace(), first->library_version(),
                                      volt::io::LibraryAssetKind::Evidence, first->library_digest(),
                                      digest}};
        const auto evidence = graph.artifact(id);
        REQUIRE(evidence.has_value());
        CHECK(evidence->bytes() == bytes);
        CHECK(evidence->descriptor().content_digest() == digest);
        CHECK(std::ranges::count(part_artifact->descriptor().dependencies(),
                                 volt::io::ArtifactRef{id, digest}) == 1);
    }
    CHECK(std::ranges::none_of(artifacts, [](const auto &artifact) {
        return artifact.descriptor().kind() == volt::io::ArtifactKind::GlbAsset ||
               artifact.descriptor().kind() == volt::io::ArtifactKind::StepAsset;
    }));
    CHECK(graph.dependency_lock().selected_parts().size() == 1U);
}

TEST_CASE("ProjectBundle rejects incomplete or tampered selected electrical evidence closure") {
    const auto fixture = electrical_fixture();
    const auto output = publication(fixture);
    auto temporary = TempDirectory{};
    const auto root = temporary.path() / "project";
    output.write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
    auto digest = volt::sha256_content_hash(model_evidence).value();
    SECTION("missing model evidence artifact and edge") {}
    SECTION("missing canonical Current evidence artifact and edge") {
        digest = volt::sha256_content_hash(current_evidence).value();
    }
    SECTION("altered evidence bytes") {
        const auto evidence =
            std::ranges::find(manifest.at("artifacts"), digest, [](const auto &artifact) {
                return artifact.at("content_digest").template get<std::string>();
            });
        REQUIRE(evidence != manifest.at("artifacts").end());
        write_bytes(root / evidence->at("path").get<std::string>(), "tampered");
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
        return;
    }
    SECTION("rehashing altered bytes does not change the immutable evidence owner") {
        const auto evidence =
            std::ranges::find(manifest.at("artifacts"), digest, [](const auto &artifact) {
                return artifact.at("content_digest").template get<std::string>();
            });
        REQUIRE(evidence != manifest.at("artifacts").end());
        const auto new_digest = volt::sha256_content_hash("tampered").value();
        const auto id = evidence->at("id");
        write_bytes(root / evidence->at("path").get<std::string>(), "tampered");
        evidence->at("content_digest") = new_digest;
        for (auto &artifact : manifest.at("artifacts")) {
            for (auto &dependency : artifact.at("depends_on")) {
                if (dependency.at("artifact") == id) {
                    dependency.at("content_digest") = new_digest;
                }
            }
        }
        reseal_manifest(root, manifest);
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
        return;
    }
    SECTION("stale evidence edge digest") {
        const auto part = std::ranges::find(
            manifest.at("artifacts"), "part_definition",
            [](const auto &artifact) { return artifact.at("kind").template get<std::string>(); });
        REQUIRE(part != manifest.at("artifacts").end());
        const auto edge =
            std::ranges::find(part->at("depends_on"), digest, [](const auto &dependency) {
                return dependency.at("content_digest").template get<std::string>();
            });
        REQUIRE(edge != part->at("depends_on").end());
        edge->at("content_digest") = volt::sha256_content_hash("stale").value();
        reseal_manifest(root, manifest);
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
        return;
    }
    SECTION("evidence asset has an extraneous dependency") {
        const auto evidence =
            std::ranges::find(manifest.at("artifacts"), digest, [](const auto &artifact) {
                return artifact.at("content_digest").template get<std::string>();
            });
        const auto footprint = std::ranges::find(
            manifest.at("artifacts"), "footprint_definition",
            [](const auto &artifact) { return artifact.at("kind").template get<std::string>(); });
        REQUIRE(evidence != manifest.at("artifacts").end());
        REQUIRE(footprint != manifest.at("artifacts").end());
        evidence->at("depends_on")
            .push_back(OrderedJson{{"artifact", footprint->at("id")},
                                   {"content_digest", footprint->at("content_digest")}});
        reseal_manifest(root, manifest);
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
        return;
    }
    const auto evidence =
        std::ranges::find(manifest.at("artifacts"), digest, [](const auto &artifact) {
            return artifact.at("content_digest").template get<std::string>();
        });
    REQUIRE(evidence != manifest.at("artifacts").end());
    const auto id = evidence->at("id");
    std::filesystem::remove(root / evidence->at("path").get<std::string>());
    manifest.at("artifacts").erase(evidence);
    for (auto &artifact : manifest.at("artifacts")) {
        auto &dependencies = artifact.at("depends_on").get_ref<OrderedJson::array_t &>();
        std::erase_if(dependencies,
                      [&](const auto &dependency) { return dependency.at("artifact") == id; });
    }
    reseal_manifest(root, manifest);
    CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
}

TEST_CASE("ProjectBundle evidence deduplication retains exact library origin") {
    auto temporary = TempDirectory{};
    const auto root = temporary.path() / "project";
    {
        const auto first = electrical_fixture("test.e2.first");
        const auto second = electrical_fixture("test.e2.second");
        auto builder = project_builder();
        builder.add_logical(volt::io::DesignKey{"first"}, *first.circuit, first.library);
        builder.add_logical(volt::io::DesignKey{"second"}, *second.circuit, second.library);
        builder.build().write(root);
    }
    const auto bundle = volt::io::ProjectBundle::open(root);
    const auto graph = bundle.graph();
    const auto artifacts = graph.artifacts();
    CHECK(std::ranges::count_if(artifacts, [](const auto &artifact) {
              return artifact.descriptor().kind() == volt::io::ArtifactKind::EvidenceAsset;
          }) == 6);
    CHECK(graph.dependency_lock().libraries().size() == 2U);
    CHECK(graph.dependency_lock().selected_parts().size() == 2U);
    for (const auto &logical : graph.loaded_project().circuits()) {
        const auto &selected =
            logical.model().get(volt::ComponentId{0}).selected_library_part_ref();
        REQUIRE(selected.has_value());
        const auto id = volt::io::ArtifactId{
            volt::io::ArtifactKind::EvidenceAsset,
            volt::io::LibraryAssetRef{selected->library_namespace(), selected->library_version(),
                                      volt::io::LibraryAssetKind::Evidence,
                                      selected->library_digest(),
                                      volt::sha256_content_hash(shared_evidence)}};
        const auto evidence = graph.artifact(id);
        REQUIRE(evidence.has_value());
        CHECK(evidence->bytes() == shared_evidence);
    }
}
