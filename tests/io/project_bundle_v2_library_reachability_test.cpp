#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <ranges>
#include <string>

#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/parts/part_definition_reader.hpp>
#include <volt/io/parts/part_definition_writer.hpp>
#include <volt/io/schematic/schematic_writer.hpp>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using namespace volt::test::project_bundle_v2;

[[nodiscard]] std::string artifact_path(const OrderedJson &id, std::string_view extension) {
    const auto digest = volt::sha256_content_hash(id.dump()).value();
    return "artifacts/" + id.at("kind").get<std::string>() + "/" +
           digest.substr(std::string_view{"sha256:"}.size(), 20U) + std::string{extension};
}

[[nodiscard]] std::size_t replace_value(OrderedJson &value, const OrderedJson &from,
                                        const OrderedJson &to) {
    if (value == from) {
        value = to;
        return 1U;
    }
    auto replacements = std::size_t{0};
    if (value.is_object()) {
        for (auto &[unused, child] : value.items()) {
            static_cast<void>(unused);
            replacements += replace_value(child, from, to);
        }
    } else if (value.is_array()) {
        for (auto &child : value) {
            replacements += replace_value(child, from, to);
        }
    }
    return replacements;
}

class MemoryAssetResolver final : public volt::PartAssetResolver {
  public:
    void add(const volt::PartAssetReference &reference, std::string bytes) {
        assets_.insert_or_assign(std::to_string(static_cast<int>(reference.kind())) + ":" +
                                     reference.key(),
                                 std::move(bytes));
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto match = assets_.find(std::to_string(static_cast<int>(reference.kind())) + ":" +
                                        reference.key());
        return match == assets_.end() ? std::nullopt : std::optional{match->second};
    }

  private:
    std::map<std::string, std::string> assets_;
};

struct SymbolFixture {
    volt::Circuit circuit;
    volt::io::PartLibraryBundle bundle;
};

[[nodiscard]] SymbolFixture symbol_fixture() {
    auto circuit = volt::Circuit{};
    const auto spec = volt::ComponentSpec{
        .name = "Symbol collision resistor",
        .pins = {volt::PinSpec{.name = "A", .number = "1"}},
        .source = volt::DefinitionSource{"test.symbol", "resistor", "1"},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.symbol/resistor@1"},
                .pin_keys = {volt::PinKey{"A"}},
            },
    };
    const auto definition = circuit.define_component(spec);
    const auto component = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto symbol_bytes =
        volt::io::write_symbol_definition(volt::SymbolDefinition{"Variant@Name"});
    const auto symbol_reference =
        volt::PartAssetReference{volt::PartAssetKind::Schematic, "symbol:Variant@Name@original",
                                 volt::sha256_content_hash(symbol_bytes)};
    const auto footprint = volt::FootprintDefinition{
        volt::FootprintRef{"test.symbol", "R1"},
        std::vector{volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd())}};
    const auto footprint_bytes = volt::io::write_footprint_asset(footprint);
    const auto footprint_reference =
        volt::PartAssetReference{volt::PartAssetKind::Footprint, "footprint:test.symbol/R1",
                                 volt::sha256_content_hash(footprint_bytes)};
    auto library_builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.symbol", "1", volt::PartLibrarySchemaVersion::V1}};
    library_builder.add_component(spec).add_part(volt::PartDefinition{
        circuit.get(definition),
        volt::PartIdentity{"test.symbol", "resistor", "1"},
        volt::ElectricalRecordSet{1},
        {volt::PinPackageTerminalMapping{volt::PinKey{"A"}, {volt::PackageTerminalKey{"1"}}}},
        {},
        volt::PartProvenance{},
        {volt::PartSchematicAssetReference{"Variant@Name", "original", symbol_reference.digest()}},
        volt::OrderablePart{
            volt::ManufacturerPart{"Volt", "SYMBOL-R1"},
            volt::PackageRef{"0603"},
            volt::HashedFootprintReference{footprint.ref(), footprint_reference.digest()},
            {volt::PartFootprintPad{"1", 0.0, 0.0, 1.0, 1.0}},
            {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                             {volt::FootprintPadKey{"1"}}}}}});
    auto resolver = MemoryAssetResolver{};
    resolver.add(symbol_reference, symbol_bytes);
    resolver.add(footprint_reference, footprint_bytes);
    const auto key = volt::PartKey{"resistor"};
    auto bundle = volt::io::PartLibraryBundle::build(library_builder, std::vector{key}, resolver);
    circuit.update(component, volt::SelectLibraryPart{bundle, bundle.require(key)});
    return SymbolFixture{std::move(circuit), std::move(bundle)};
}

void replace_selected_part(const std::filesystem::path &root, OrderedJson &manifest,
                           const volt::PartDefinition &altered_part) {
    const auto part = std::ranges::find(manifest.at("artifacts"), "part_definition",
                                        [](const auto &artifact) { return artifact.at("kind"); });
    const auto logical =
        std::ranges::find(manifest.at("artifacts"), "logical_model",
                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(part != manifest.at("artifacts").end());
    REQUIRE(logical != manifest.at("artifacts").end());

    const auto altered_part_bytes = volt::io::write_part_definition(altered_part);
    const auto old_part_id = part->at("id");
    auto new_part_id = old_part_id;
    const auto old_part_digest =
        old_part_id.at("owner").at("value").at("part_digest").get<std::string>();
    const auto new_part_digest = altered_part.content_identity().value();
    new_part_id.at("owner").at("value")["part_digest"] = new_part_digest;
    const auto old_part_content_digest = part->at("content_digest").get<std::string>();
    const auto new_part_content_digest = volt::sha256_content_hash(altered_part_bytes).value();
    const auto old_part_path = part->at("path").get<std::string>();
    const auto new_part_path = artifact_path(new_part_id, ".json");
    const auto logical_path = logical->at("path").get<std::string>();
    auto logical_document = OrderedJson::parse(read_bytes(root / logical_path));
    REQUIRE(replace_value(logical_document, old_part_digest, new_part_digest) == 1U);
    const auto altered_logical = volt::io::read_logical_circuit_text(logical_document.dump());
    const auto altered_logical_bytes = volt::io::write_logical_circuit(altered_logical);
    const auto old_logical_content_digest = logical->at("content_digest").get<std::string>();
    const auto new_logical_content_digest =
        volt::sha256_content_hash(altered_logical_bytes).value();

    REQUIRE(replace_value(manifest, old_part_id, new_part_id) >= 2U);
    REQUIRE(replace_value(manifest, old_part_digest, new_part_digest) >= 1U);
    REQUIRE(replace_value(manifest, old_part_content_digest, new_part_content_digest) >= 2U);
    REQUIRE(replace_value(manifest, old_logical_content_digest, new_logical_content_digest) >= 2U);
    const auto updated_part =
        std::ranges::find(manifest.at("artifacts"), "part_definition",
                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(updated_part != manifest.at("artifacts").end());
    updated_part->at("path") = new_part_path;
    std::filesystem::rename(root / old_part_path, root / new_part_path);
    write_bytes(root / new_part_path, altered_part_bytes);
    write_bytes(root / logical_path, altered_logical_bytes);
    reseal_manifest(root, manifest);
}

} // namespace

TEST_CASE("ProjectBundle v2 rejects an unreachable vendored library artifact") {
    const auto fixture = board_fixture();
    const auto original = board_builder(fixture).build();
    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "orphan-library-artifact.volt";
    original.write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
    const auto component =
        std::ranges::find(manifest.at("artifacts"), "component_definition",
                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(component != manifest.at("artifacts").end());
    auto orphan = *component;
    const auto orphan_digest = volt::sha256_content_hash("orphan library").value();
    orphan["id"]["owner"]["value"]["library_namespace"] = "orphan.library";
    orphan["id"]["owner"]["value"]["library_version"] = "1";
    orphan["id"]["owner"]["value"]["library_bundle_digest"] = orphan_digest;
    orphan["producer"]["build"] = orphan_digest;
    const auto orphan_path = artifact_path(orphan.at("id"), ".json");
    std::filesystem::copy_file(root / component->at("path").get<std::string>(), root / orphan_path);
    orphan["path"] = orphan_path;
    manifest["artifacts"].push_back(std::move(orphan));
    auto libraries = OrderedJson::array({OrderedJson{{"library", "orphan.library"},
                                                     {"version", "1"},
                                                     {"library_bundle_digest", orphan_digest}}});
    for (const auto &library : manifest.at("dependency_lock").at("libraries")) {
        libraries.push_back(library);
    }
    manifest["dependency_lock"]["libraries"] = std::move(libraries);
    auto ordered_artifacts = std::vector<OrderedJson>{};
    for (const auto &artifact : manifest.at("artifacts")) {
        ordered_artifacts.push_back(artifact);
    }
    std::ranges::sort(ordered_artifacts, [](const auto &left, const auto &right) {
        return left.at("id").dump() < right.at("id").dump();
    });
    manifest["artifacts"] = OrderedJson::array();
    for (auto &artifact : ordered_artifacts) {
        manifest["artifacts"].push_back(std::move(artifact));
    }
    reseal_manifest(root, manifest);

    try {
        static_cast<void>(volt::io::ProjectBundle::open(root));
        FAIL("ProjectBundle open unexpectedly succeeded");
    } catch (const volt::io::ProjectBundleOpenError &error) {
        CHECK(error.code() == volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
    }
}

TEST_CASE("ProjectBundle v2 binds a footprint owner key to its decoded reference") {
    const auto fixture = board_fixture();
    const auto original = board_builder(fixture).build();
    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "footprint-owner.volt";
    original.write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
    const auto footprint =
        std::ranges::find(manifest.at("artifacts"), "footprint_definition",
                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(footprint != manifest.at("artifacts").end());
    const auto old_id = footprint->at("id");
    const auto old_path = footprint->at("path").get<std::string>();
    footprint->at("id").at("owner").at("value").at("key") = "footprint:test.project/contradictory";
    const auto new_id = footprint->at("id");
    const auto new_path = artifact_path(new_id, ".json");
    std::filesystem::rename(root / old_path, root / new_path);
    footprint->at("path") = new_path;
    for (auto &artifact : manifest.at("artifacts")) {
        for (auto &dependency : artifact.at("depends_on")) {
            if (dependency.at("artifact") == old_id) {
                dependency["artifact"] = new_id;
            }
        }
    }
    reseal_manifest(root, manifest);

    try {
        static_cast<void>(volt::io::ProjectBundle::open(root));
        FAIL("ProjectBundle open unexpectedly succeeded");
    } catch (const volt::io::ProjectBundleOpenError &error) {
        CHECK(error.code() == volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
        CHECK(std::string{error.what()}.contains(
            "footprint payload identity disagrees with its owner"));
    }
}

TEST_CASE("ProjectBundle v2 binds a footprint payload to its selected typed reference") {
    const auto fixture = board_fixture(30.0, volt::FootprintRef{"test.project", "nested/R1"});
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
    const auto original = builder.build();
    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "footprint-ref-collision.volt";
    original.write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));

    const auto component =
        std::ranges::find(manifest.at("artifacts"), "component_definition",
                          [](const auto &artifact) { return artifact.at("kind"); });
    const auto part = std::ranges::find(manifest.at("artifacts"), "part_definition",
                                        [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(component != manifest.at("artifacts").end());
    REQUIRE(part != manifest.at("artifacts").end());

    const auto component_document = volt::io::read_logical_circuit_text(
        read_bytes(root / component->at("path").get<std::string>()));
    const auto &component_definition = component_document.get(volt::ComponentDefId{0});
    const auto original_part = volt::io::read_part_definition_text(
        read_bytes(root / part->at("path").get<std::string>()), component_definition);
    const auto &physical = original_part.orderable_part();
    const auto altered_part = volt::PartDefinition{
        component_definition,
        original_part.identity(),
        original_part.electrical_records(),
        original_part.pin_terminal_mappings(),
        original_part.terminal_dispositions(),
        original_part.provenance(),
        original_part.schematic_assets(),
        volt::OrderablePart{
            physical.manufacturer_part(), physical.package(),
            volt::HashedFootprintReference{volt::FootprintRef{"test.project/nested", "R1"},
                                           physical.footprint().hash()},
            physical.footprint_pads(), physical.terminal_pad_mappings(),
            physical.approved_alternate_mpns(), physical.model_3d(), physical.footprint_courtyard(),
            physical.footprint_body(), physical.footprint_fabrication_outline(),
            physical.footprint_assembly_outline(), physical.footprint_markings()}};
    replace_selected_part(root, manifest, altered_part);

    try {
        static_cast<void>(volt::io::ProjectBundle::open(root));
        FAIL("ProjectBundle open unexpectedly succeeded");
    } catch (const volt::io::ProjectBundleOpenError &error) {
        CHECK(error.code() == volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
        CHECK(std::string{error.what()}.contains(
            "footprint payload reference disagrees with its selected part"));
    }
}

TEST_CASE("ProjectBundle v2 binds a symbol payload to its selected typed reference") {
    const auto fixture = symbol_fixture();
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"main"}, fixture.circuit, fixture.bundle);
    const auto original = builder.build();
    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "symbol-ref-collision.volt";
    original.write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));

    const auto component =
        std::ranges::find(manifest.at("artifacts"), "component_definition",
                          [](const auto &artifact) { return artifact.at("kind"); });
    const auto part = std::ranges::find(manifest.at("artifacts"), "part_definition",
                                        [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(component != manifest.at("artifacts").end());
    REQUIRE(part != manifest.at("artifacts").end());
    const auto component_document = volt::io::read_logical_circuit_text(
        read_bytes(root / component->at("path").get<std::string>()));
    const auto &component_definition = component_document.get(volt::ComponentDefId{0});
    const auto original_part = volt::io::read_part_definition_text(
        read_bytes(root / part->at("path").get<std::string>()), component_definition);
    REQUIRE(original_part.schematic_assets().size() == 1U);
    const auto altered_part = volt::PartDefinition{
        component_definition,
        original_part.identity(),
        original_part.electrical_records(),
        original_part.pin_terminal_mappings(),
        original_part.terminal_dispositions(),
        original_part.provenance(),
        {volt::PartSchematicAssetReference{"Variant", "Name@original",
                                           original_part.schematic_assets().front().hash()}},
        original_part.orderable_part()};
    replace_selected_part(root, manifest, altered_part);

    try {
        static_cast<void>(volt::io::ProjectBundle::open(root));
        FAIL("ProjectBundle open unexpectedly succeeded");
    } catch (const volt::io::ProjectBundleOpenError &error) {
        CHECK(error.code() == volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
        CHECK(std::string{error.what()}.contains(
            "symbol payload name disagrees with its selected part"));
    }
}
