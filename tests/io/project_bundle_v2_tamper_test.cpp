#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/pcb/compiled_board_consumers.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/io/project_bundle_writer.hpp>

#include "support/compiled_board_export_helpers.hpp"
#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

class EmptyAssetResolver final : public volt::PartAssetResolver {
  public:
    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &) const override {
        return std::nullopt;
    }
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

[[nodiscard]] std::string read_bytes(const std::filesystem::path &path) {
    auto input = std::ifstream{path, std::ios::binary};
    REQUIRE(input);
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
    auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

[[nodiscard]] OrderedJson canonical_build_artifact(const OrderedJson &artifact) {
    return OrderedJson{{"id", artifact.at("id")},
                       {"role", artifact.at("role")},
                       {"kind", artifact.at("kind")},
                       {"schema", artifact.at("schema")},
                       {"media_type", artifact.at("media_type")},
                       {"content_digest", artifact.at("content_digest")},
                       {"depends_on", artifact.at("depends_on")},
                       {"producer_name", artifact.at("producer").at("name")},
                       {"producer_version", artifact.at("producer").at("version")}};
}

void reseal_manifest(const std::filesystem::path &root, OrderedJson &manifest) {
    auto required = OrderedJson::array();
    for (const auto &artifact : manifest.at("artifacts")) {
        if (artifact.at("id").at("owner").at("type") != "export_artifact_identity") {
            required.push_back(canonical_build_artifact(artifact));
        }
    }
    const auto build_identity =
        OrderedJson{{"format", manifest.at("format")},
                    {"schema_version", manifest.at("schema_version")},
                    {"project", manifest.at("project")},
                    {"authoring_inputs_digest", manifest.at("authoring_inputs").at("digest")},
                    {"dependency_lock", manifest.at("dependency_lock")},
                    {"artifacts", std::move(required)}};
    manifest["build_id"] = volt::sha256_content_hash(build_identity.dump()).value();
    for (auto &artifact : manifest.at("artifacts")) {
        if (artifact.at("producer").at("name") == "volt.project-bundle") {
            artifact["producer"]["build"] = manifest.at("build_id");
        }
    }
    const auto core = OrderedJson{{"format", manifest.at("format")},
                                  {"schema_version", manifest.at("schema_version")},
                                  {"project", manifest.at("project")},
                                  {"run", manifest.at("run")},
                                  {"authoring_inputs", manifest.at("authoring_inputs")},
                                  {"build_id", manifest.at("build_id")},
                                  {"dependency_lock", manifest.at("dependency_lock")},
                                  {"export_selection", manifest.at("export_selection")},
                                  {"artifacts", manifest.at("artifacts")}};
    manifest["bundle_digest"] = volt::sha256_content_hash(core.dump()).value();
    write_bytes(root / "manifest.volt.json", manifest.dump());
}

void replace_artifact_payload(const std::filesystem::path &root, OrderedJson &manifest,
                              std::string_view kind, std::string_view bytes) {
    const auto artifact =
        std::ranges::find_if(manifest.at("artifacts"), [&](const auto &candidate) {
            return candidate.at("kind").template get<std::string>() == std::string{kind};
        });
    REQUIRE(artifact != manifest.at("artifacts").end());
    const auto id = artifact->at("id");
    const auto digest = volt::sha256_content_hash(bytes).value();
    write_bytes(root / artifact->at("path").get<std::string>(), bytes);
    (*artifact)["content_digest"] = digest;
    for (auto &candidate : manifest.at("artifacts")) {
        for (auto &dependency : candidate.at("depends_on")) {
            if (dependency.at("artifact") == id) {
                dependency["content_digest"] = digest;
            }
        }
    }
}

[[nodiscard]] OrderedJson warning_diagnostic(std::string stage, std::string source,
                                             std::string report, std::optional<std::string> design,
                                             std::optional<std::string> board,
                                             OrderedJson entities) {
    auto expectation = OrderedJson{{"code", "WARN"},
                                   {"severity", "warning"},
                                   {"stage", stage},
                                   {"source", source},
                                   {"report", report}};
    auto design_value = OrderedJson(nullptr);
    auto board_value = OrderedJson(nullptr);
    if (design.has_value()) {
        expectation["design"] = *design;
        design_value = *design;
    }
    if (board.has_value()) {
        expectation["board"] = *board;
        board_value = *board;
    }
    return OrderedJson{{"stage", std::move(stage)},
                       {"source", source},
                       {"report", std::move(report)},
                       {"severity", "warning"},
                       {"category", "general"},
                       {"code", "WARN"},
                       {"message", "warning"},
                       {"entities", std::move(entities)},
                       {"overlays", OrderedJson::array()},
                       {"measurement", nullptr},
                       {"design", std::move(design_value)},
                       {"board", std::move(board_value)},
                       {"rule", nullptr},
                       {"expect_diagnostic_kwargs", expectation}};
}

[[nodiscard]] OrderedJson warning_diagnostic(std::string design, OrderedJson entities) {
    const auto source = "logical:" + design;
    return warning_diagnostic("design", source, "logical.default", std::move(design), std::nullopt,
                              std::move(entities));
}

[[nodiscard]] OrderedJson warning_report(OrderedJson diagnostic) {
    return OrderedJson{{"status", "expected-diagnostics"},
                       {"summary", {{"errors", 0}, {"warnings", 1}, {"infos", 0}}},
                       {"diagnostics", OrderedJson::array({diagnostic})},
                       {"expected", OrderedJson::array()},
                       {"unexpected", OrderedJson::array({std::move(diagnostic)})},
                       {"missing_expected", OrderedJson::array()}};
}

[[nodiscard]] OrderedJson missing_expectation_report(std::string design) {
    const auto source = "logical:" + design;
    const auto expectation =
        OrderedJson{{"code", "WARN"},
                    {"severity", nullptr},
                    {"stage", nullptr},
                    {"source", source},
                    {"report", nullptr},
                    {"design", design},
                    {"board", nullptr},
                    {"rule", nullptr},
                    {"matched", false},
                    {"expect_diagnostic_kwargs",
                     OrderedJson{{"code", "WARN"}, {"source", source}, {"design", design}}}};
    const auto diagnostic = warning_diagnostic("main", OrderedJson::array());
    return OrderedJson{{"status", "failed"},
                       {"summary", {{"errors", 0}, {"warnings", 1}, {"infos", 0}}},
                       {"diagnostics", OrderedJson::array({diagnostic})},
                       {"expected", OrderedJson::array({expectation})},
                       {"unexpected", OrderedJson::array({diagnostic})},
                       {"missing_expected", OrderedJson::array({expectation})}};
}

[[nodiscard]] std::string library_report_subject(std::string_view library_namespace,
                                                 std::string_view library_version,
                                                 const volt::ContentHash &library_digest) {
    return "library:" + Json{{"namespace", library_namespace},
                             {"version", library_version},
                             {"library_bundle_digest", library_digest.value()}}
                            .dump();
}

[[nodiscard]] std::string library_report_subject(const volt::io::PartLibraryBundle &bundle) {
    return library_report_subject(bundle.identity().namespace_name(), bundle.identity().version(),
                                  bundle.library_digest());
}

[[nodiscard]] std::size_t replace_string_field(OrderedJson &value, std::string_view field,
                                               std::string_view from, std::string_view to) {
    auto replacements = std::size_t{0};
    if (value.is_object()) {
        if (const auto match = value.find(field);
            match != value.end() && match->is_string() && match->get<std::string>() == from) {
            *match = to;
            ++replacements;
        }
        for (auto &[unused, child] : value.items()) {
            static_cast<void>(unused);
            replacements += replace_string_field(child, field, from, to);
        }
    } else if (value.is_array()) {
        for (auto &child : value) {
            replacements += replace_string_field(child, field, from, to);
        }
    }
    return replacements;
}

[[nodiscard]] std::string part_artifact_path(const OrderedJson &id) {
    const auto digest = volt::sha256_content_hash(id.dump()).value();
    return "artifacts/part_definition/" + digest.substr(std::string_view{"sha256:"}.size(), 20U) +
           ".json";
}

void check_open_error(const std::filesystem::path &path,
                      volt::io::ProjectBundleOpenErrorCode expected) {
    try {
        static_cast<void>(volt::io::ProjectBundle::open(path));
        FAIL("ProjectBundle open unexpectedly succeeded");
    } catch (const volt::io::ProjectBundleOpenError &error) {
        INFO(error.what());
        CHECK(error.code() == expected);
    }
}

class LogicalFixture final {
  public:
    LogicalFixture()
        : bundle_{volt::io::PartLibraryBundle::build(
              volt::PartLibraryBuilder{
                  volt::PartLibraryIdentity{"test.empty", "1", volt::PartLibrarySchemaVersion::V1}},
              {}, resolver_)} {}

    [[nodiscard]] volt::io::ProjectBundleBuilder
    builder(std::string source = "project source",
            std::vector<volt::io::AuthoringInput> extra_inputs = {}) const {
        auto inputs = std::vector<volt::io::AuthoringInput>{};
        inputs.emplace_back(volt::io::AuthoringInputKind::ProjectSource,
                            volt::io::LogicalInputName{"project.py"}, std::move(source));
        inputs.insert(inputs.end(), std::make_move_iterator(extra_inputs.begin()),
                      std::make_move_iterator(extra_inputs.end()));
        auto result = volt::io::ProjectBundleBuilder{
            volt::io::ProjectIdentity{"fixture", std::optional{"1.0"},
                                      std::optional{"fixture project"}},
            volt::io::ProjectRunSummary{
                true, volt::io::ProjectStatus::Clean, "default", {"design"}},
            volt::io::LogicalInputName{"project.py"},
            std::move(inputs),
            volt::io::ProjectReport{
                R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
            volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
        result.add_logical(volt::io::DesignKey{"main"}, circuit_, bundle_);
        return result;
    }

    [[nodiscard]] const volt::Circuit &circuit() const noexcept { return circuit_; }

    [[nodiscard]] const volt::io::PartLibraryBundle &bundle() const noexcept { return bundle_; }

  private:
    EmptyAssetResolver resolver_;
    volt::Circuit circuit_;
    volt::io::PartLibraryBundle bundle_;
};

struct BoardFixture {
    std::unique_ptr<volt::Circuit> circuit;
    volt::io::PartLibraryBundle bundle;
    volt::Board board;
    volt::CompiledBoard compiled;
    volt::BoardScene scene;
};

struct TwoPartFixture {
    std::unique_ptr<volt::Circuit> circuit;
    volt::io::PartLibraryBundle bundle;
};

[[nodiscard]] std::string minimal_glb() {
    return std::string{'g', 'l', 'T', 'F', '\x02', '\0', '\0', '\0', '\x0c', '\0', '\0', '\0'};
}

[[nodiscard]] TwoPartFixture two_part_fixture() {
    auto circuit = std::make_unique<volt::Circuit>();
    const auto resistor_spec = volt::ComponentSpec{
        .name = "Project resistor",
        .pins = {volt::PinSpec{.name = "A", .number = "1"}},
        .source = volt::DefinitionSource{"test.project", "resistor", "1"},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.project/resistor@1"},
                .pin_keys = {volt::PinKey{"A"}},
            },
    };
    const auto capacitor_spec = volt::ComponentSpec{
        .name = "Project capacitor",
        .pins = {volt::PinSpec{.name = "A", .number = "1"}},
        .source = volt::DefinitionSource{"test.project", "capacitor", "1"},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.project/capacitor@1"},
                .pin_keys = {volt::PinKey{"A"}},
            },
    };
    const auto resistor_definition = circuit->define_component(resistor_spec);
    const auto capacitor_definition = circuit->define_component(capacitor_spec);
    const auto resistor = circuit->instantiate_component(
        resistor_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto capacitor = circuit->instantiate_component(
        capacitor_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"C1"}});
    const auto resistor_footprint = volt::FootprintDefinition{
        volt::FootprintRef{"test.project", "R1"},
        std::vector{volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd())}};
    const auto capacitor_footprint = volt::FootprintDefinition{
        volt::FootprintRef{"test.project", "C1"},
        std::vector{volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd())}};
    const auto resistor_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Volt", "PROJECT-R1"}, volt::PackageRef{"0603"},
        resistor_footprint.ref(),
        std::vector{volt::PinPadMapping{circuit->get(resistor_definition).pins().front(), "1"}}};
    const auto capacitor_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Volt", "PROJECT-C1"}, volt::PackageRef{"0603"},
        capacitor_footprint.ref(),
        std::vector{volt::PinPadMapping{circuit->get(capacitor_definition).pins().front(), "1"}}};
    auto library = volt::test::make_export_fixture_library(
        {{resistor_spec, resistor_part, resistor_footprint, volt::PartKey{"resistor"}},
         {capacitor_spec, capacitor_part, capacitor_footprint, volt::PartKey{"capacitor"}}});
    circuit->update(resistor, volt::SelectLibraryPart{library.bundle,
                                                      library.bundle.require(library.keys.at(0))});
    circuit->update(capacitor, volt::SelectLibraryPart{library.bundle,
                                                       library.bundle.require(library.keys.at(1))});
    return TwoPartFixture{std::move(circuit), std::move(library.bundle)};
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

[[nodiscard]] volt::io::ProjectBundleBuilder project_builder() {
    return volt::io::ProjectBundleBuilder{
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

} // namespace

TEST_CASE("ProjectBundle v2 open fails closed for digest path lock and exact edge tampering") {
    const auto fixture = LogicalFixture{};
    const auto original = fixture.builder().build();
    const auto temporary = TempDirectory{};

    SECTION("only the current project schema is accepted") {
        const auto root = temporary.path() / "schema.volt";
        original.write(root);
        const auto baseline = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        for (const auto version : {1, 2, 4}) {
            auto manifest = baseline;
            manifest["schema_version"] = version;
            reseal_manifest(root, manifest);
            try {
                static_cast<void>(volt::io::ProjectBundle::open(root));
                FAIL("Expected a non-current project schema to reject");
            } catch (const volt::io::ProjectBundleOpenError &error) {
                CHECK(error.code() == volt::io::ProjectBundleOpenErrorCode::UnsupportedSchema);
            }
        }
    }

    SECTION("payload digest") {
        const auto root = temporary.path() / "payload.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        const auto &logical =
            *std::ranges::find(manifest.at("artifacts"), "logical_model",
                               [](const auto &artifact) { return artifact.at("kind"); });
        write_bytes(root / logical.at("path").get<std::string>(), "tampered");
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
    }

    SECTION("path and ArtifactId") {
        const auto root = temporary.path() / "path.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        manifest["artifacts"][0]["path"] = "artifacts/logical_model/wrong.json";
        reseal_manifest(root, manifest);
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
    }

    SECTION("dependency lock") {
        const auto root = temporary.path() / "lock.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        manifest["dependency_lock"]["libraries"].push_back(
            OrderedJson{{"library", "foreign"},
                        {"version", "1"},
                        {"library_bundle_digest", volt::sha256_content_hash("foreign").value()}});
        reseal_manifest(root, manifest);
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
    }

    SECTION("owner codec") {
        const auto root = temporary.path() / "owner-codec.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        replace_artifact_payload(root, manifest, "logical_model", "{}");
        reseal_manifest(root, manifest);
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
    }

    SECTION("project tests nested codec") {
        const auto root = temporary.path() / "project-tests-codec.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        replace_artifact_payload(
            root, manifest, "project_tests",
            R"({"summary":{"failed":0,"passed":999,"extra":true},"tests":"not-an-array"})");
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("project tests duplicate JSON object key") {
        const auto root = temporary.path() / "project-tests-duplicate-key.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        replace_artifact_payload(root, manifest, "project_tests",
                                 R"({"summary":{"failed":0,"passed":0,"passed":0},"tests":[]})");
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("project tests summary") {
        const auto root = temporary.path() / "project-tests-summary.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        replace_artifact_payload(
            root, manifest, "project_tests",
            R"({"summary":{"failed":0,"passed":0},"tests":[{"stage":"design","name":"truth","ok":true,"message":""}]})");
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("diagnostics nested codec") {
        const auto root = temporary.path() / "diagnostics-codec.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        replace_artifact_payload(
            root, manifest, "diagnostics",
            R"({"status":"clean","summary":{"errors":1,"warnings":0,"infos":0},"diagnostics":[{"severity":"error"}],"expected":[],"unexpected":[],"missing_expected":[]})");
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("diagnostics duplicate JSON object key") {
        const auto root = temporary.path() / "diagnostics-duplicate-key.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        replace_artifact_payload(
            root, manifest, "diagnostics",
            R"({"status":"clean","status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})");
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("diagnostics summary") {
        const auto root = temporary.path() / "diagnostics-summary.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        replace_artifact_payload(
            root, manifest, "diagnostics",
            R"({"status":"clean","summary":{"errors":1,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})");
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("diagnostics status derived from decoded evidence") {
        const auto root = temporary.path() / "diagnostics-derived-status.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        replace_artifact_payload(
            root, manifest, "diagnostics",
            R"({"status":"clean","summary":{"errors":0,"warnings":1,"infos":0},"diagnostics":[{"stage":"design","source":"test","report":"logical.default","severity":"warning","category":"general","code":"WARN","message":"warning","entities":[],"overlays":[],"measurement":null,"design":"main","board":null,"rule":null,"expect_diagnostic_kwargs":{"code":"WARN","severity":"warning","stage":"design","source":"test","report":"logical.default","design":"main"}}],"expected":[],"unexpected":[{"stage":"design","source":"test","report":"logical.default","severity":"warning","category":"general","code":"WARN","message":"warning","entities":[],"overlays":[],"measurement":null,"design":"main","board":null,"rule":null,"expect_diagnostic_kwargs":{"code":"WARN","severity":"warning","stage":"design","source":"test","report":"logical.default","design":"main"}}],"missing_expected":[]})");
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("missing expected diagnostic derived as not ok") {
        const auto root = temporary.path() / "missing-expected-status.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        replace_artifact_payload(
            root, manifest, "diagnostics",
            R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[{"code":"EXPECTED","severity":"warning","stage":"design","source":"test","report":"logical.default","design":"main","board":null,"rule":null,"matched":false,"expect_diagnostic_kwargs":{"code":"EXPECTED","severity":"warning","stage":"design","source":"test","report":"logical.default","design":"main"}}],"unexpected":[],"missing_expected":[{"code":"EXPECTED","severity":"warning","stage":"design","source":"test","report":"logical.default","design":"main","board":null,"rule":null,"matched":false,"expect_diagnostic_kwargs":{"code":"EXPECTED","severity":"warning","stage":"design","source":"test","report":"logical.default","design":"main"}}]})");
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
    }

    SECTION("diagnostic references a missing logical entity") {
        const auto root = temporary.path() / "diagnostic-missing-entity.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        const auto report = warning_report(warning_diagnostic(
            "main", OrderedJson::array({{{"kind", "component"}, {"index", 999}}})));
        replace_artifact_payload(root, manifest, "diagnostics", report.dump());
        manifest["run"]["status"] = "expected-diagnostics";
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
    }

    SECTION("diagnostic names a foreign logical design") {
        const auto root = temporary.path() / "diagnostic-foreign-design.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        const auto report = warning_report(warning_diagnostic("foreign", OrderedJson::array()));
        replace_artifact_payload(root, manifest, "diagnostics", report.dump());
        manifest["run"]["status"] = "expected-diagnostics";
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
    }

    SECTION("undeclared empty directory") {
        const auto root = temporary.path() / "empty-directory.volt";
        original.write(root);
        std::filesystem::create_directories(root / "undeclared" / "empty");
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::MalformedArchive);
    }

    SECTION("missing direct edge") {
        const auto root = temporary.path() / "missing-edge.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        auto diagnostics =
            std::ranges::find(manifest.at("artifacts"), "diagnostics",
                              [](const auto &artifact) { return artifact.at("kind"); });
        diagnostics->at("depends_on").clear();
        reseal_manifest(root, manifest);
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
    }

    SECTION("non-adjacent duplicate run stage") {
        const auto root = temporary.path() / "duplicate-stage.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        manifest["run"]["stages"] = {"design", "board", "design"};
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::DuplicateIdentity);
    }

    SECTION("duplicate authoring kind and name with a different digest") {
        const auto root = temporary.path() / "duplicate-input.volt";
        const auto with_input =
            fixture
                .builder("project source",
                         {volt::io::AuthoringInput{volt::io::AuthoringInputKind::DeclaredInput,
                                                   volt::io::LogicalInputName{"inputs/config.json"},
                                                   "first"}})
                .build();
        with_input.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        auto duplicate = manifest.at("authoring_inputs").at("records").at(0);
        duplicate["content_digest"] = volt::sha256_content_hash("second").value();
        manifest["authoring_inputs"]["records"].push_back(std::move(duplicate));
        auto &records = manifest["authoring_inputs"]["records"];
        auto ordered_records = std::vector<OrderedJson>{};
        for (const auto &record : records) {
            ordered_records.push_back(record);
        }
        std::ranges::sort(ordered_records, [](const auto &left, const auto &right) {
            return std::tuple{left.at("kind").template get<std::string>(),
                              left.at("name").template get<std::string>(),
                              left.at("content_digest").template get<std::string>()} <
                   std::tuple{right.at("kind").template get<std::string>(),
                              right.at("name").template get<std::string>(),
                              right.at("content_digest").template get<std::string>()};
        });
        records = OrderedJson::array();
        for (auto &record : ordered_records) {
            records.push_back(std::move(record));
        }
        const auto authoring_core = OrderedJson{
            {"entrypoint", manifest.at("authoring_inputs").at("entrypoint")}, {"records", records}};
        manifest["authoring_inputs"]["digest"] =
            volt::sha256_content_hash(authoring_core.dump()).value();
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::DuplicateIdentity);
    }

    SECTION("forbidden extra and source sentinels") {
        const auto root = temporary.path() / "source.volt";
        original.write(root);
        const auto sentinel = temporary.path() / "source-executed";
        write_bytes(root / "project.py", "from pathlib import Path\nPath('" + sentinel.string() +
                                             "').write_text('executed')\n");
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
        CHECK_FALSE(std::filesystem::exists(sentinel));
    }
}

TEST_CASE("ProjectBundle v2 binds dependency lock and part owners to decoded logical meaning") {
    const auto fixture = board_fixture();
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
    const auto baseline = builder.build();
    const auto baseline_logical =
        std::ranges::find(baseline.artifacts(), volt::io::ArtifactKind::LogicalModel,
                          &volt::io::ArtifactDescriptor::kind);
    REQUIRE(baseline_logical != baseline.artifacts().end());
    builder.select_exports(
        {volt::io::ExportRequest{volt::io::ExportKind::Bom,
                                 volt::io::ModelExportTarget{volt::io::ArtifactRef{
                                     baseline_logical->id(), baseline_logical->content_digest()}},
                                 volt::io::ExportRequestSchema{}, volt::io::BomParameters{}}});
    const auto original = builder.build();
    const auto temporary = TempDirectory{};

    SECTION("exact selected part resolves from the vendored closure for BOM projection") {
        const auto root = temporary.path() / "exact-bom-part.volt";
        original.write(root);
        CHECK(volt::io::ProjectBundle::open(root)
                  .graph()
                  .loaded_project()
                  .selected_exports()
                  .size() == 1U);
    }

    SECTION("BOM exact reference with a foreign library origin is rejected") {
        const auto root = temporary.path() / "foreign-bom-origin.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        const auto logical_artifact =
            std::ranges::find(manifest.at("artifacts"), "logical_model",
                              [](const auto &artifact) { return artifact.at("kind"); });
        REQUIRE(logical_artifact != manifest.at("artifacts").end());
        auto payload =
            OrderedJson::parse(read_bytes(root / logical_artifact->at("path").get<std::string>()));
        REQUIRE(replace_string_field(payload, "library_namespace",
                                     fixture.bundle.identity().namespace_name(),
                                     "foreign.project") == 1U);
        replace_artifact_payload(root, manifest, "logical_model", payload.dump());
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("BOM exact reference with a foreign library digest is rejected") {
        const auto root = temporary.path() / "foreign-bom-digest.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        const auto logical_artifact =
            std::ranges::find(manifest.at("artifacts"), "logical_model",
                              [](const auto &artifact) { return artifact.at("kind"); });
        REQUIRE(logical_artifact != manifest.at("artifacts").end());
        auto payload =
            OrderedJson::parse(read_bytes(root / logical_artifact->at("path").get<std::string>()));
        REQUIRE(replace_string_field(payload, "library_digest",
                                     fixture.bundle.reference_digest().value(),
                                     volt::sha256_content_hash("foreign").value()) == 1U);
        replace_artifact_payload(root, manifest, "logical_model", payload.dump());
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("BOM exact reference without its vendored part is rejected") {
        const auto root = temporary.path() / "missing-bom-part.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        auto retained = OrderedJson::array();
        auto removed = false;
        for (auto &artifact : manifest.at("artifacts")) {
            if (artifact.at("kind") == "part_definition") {
                std::filesystem::remove(root / artifact.at("path").get<std::string>());
                removed = true;
            } else {
                retained.push_back(std::move(artifact));
            }
        }
        REQUIRE(removed);
        manifest["artifacts"] = std::move(retained);
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::MalformedArchive);
    }

    SECTION("unreferenced vendored selected part") {
        const auto root = temporary.path() / "orphan-part.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        const auto logical =
            std::ranges::find(manifest.at("artifacts"), "logical_model",
                              [](const auto &artifact) { return artifact.at("kind"); });
        REQUIRE(logical != manifest.at("artifacts").end());
        auto logical_payload =
            OrderedJson::parse(read_bytes(root / logical->at("path").get<std::string>()));
        auto removed = false;
        for (auto &component : logical_payload.at("components")) {
            removed = component.erase("selected_library_part") != 0U || removed;
        }
        REQUIRE(removed);
        replace_artifact_payload(root, manifest, "logical_model", logical_payload.dump());

        const auto updated_logical =
            std::ranges::find(manifest.at("artifacts"), "logical_model",
                              [](const auto &artifact) { return artifact.at("kind"); });
        auto retained_dependencies = OrderedJson::array();
        for (const auto &dependency : updated_logical->at("depends_on")) {
            if (dependency.at("artifact").at("kind") != "part_definition") {
                retained_dependencies.push_back(dependency);
            }
        }
        updated_logical->at("depends_on") = std::move(retained_dependencies);
        reseal_manifest(root, manifest);
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
    }

    SECTION("part ArtifactId owner disagrees with canonical payload identity") {
        const auto root = temporary.path() / "part-owner.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        const auto logical =
            std::ranges::find(manifest.at("artifacts"), "logical_model",
                              [](const auto &artifact) { return artifact.at("kind"); });
        REQUIRE(logical != manifest.at("artifacts").end());
        auto logical_payload =
            OrderedJson::parse(read_bytes(root / logical->at("path").get<std::string>()));
        REQUIRE(replace_string_field(logical_payload, "part_key", "resistor", "relabeled") == 1U);
        replace_artifact_payload(root, manifest, "logical_model", logical_payload.dump());

        const auto part =
            std::ranges::find(manifest.at("artifacts"), "part_definition",
                              [](const auto &artifact) { return artifact.at("kind"); });
        REQUIRE(part != manifest.at("artifacts").end());
        const auto old_path = part->at("path").get<std::string>();
        REQUIRE(replace_string_field(manifest, "part_key", "resistor", "relabeled") > 1U);
        const auto updated_part =
            std::ranges::find(manifest.at("artifacts"), "part_definition",
                              [](const auto &artifact) { return artifact.at("kind"); });
        const auto new_path = part_artifact_path(updated_part->at("id"));
        std::filesystem::rename(root / old_path, root / new_path);
        updated_part->at("path") = new_path;
        reseal_manifest(root, manifest);
        CHECK_THROWS_AS(volt::io::ProjectBundle::open(root), volt::io::ProjectBundleOpenError);
    }

    SECTION("diagnostic part reference resolves its exact vendored part") {
        const auto root = temporary.path() / "diagnostic-exact-part.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        const auto report = warning_report(warning_diagnostic(
            "library", "part:resistor", library_report_subject(fixture.bundle), std::nullopt,
            std::nullopt, OrderedJson::array({{{"kind", "part_definition"}, {"index", 0}}})));
        replace_artifact_payload(root, manifest, "diagnostics", report.dump());
        manifest["run"]["status"] = "expected-diagnostics";
        reseal_manifest(root, manifest);
        CHECK_NOTHROW(volt::io::ProjectBundle::open(root));
    }

    SECTION("diagnostic part reference names a foreign part") {
        const auto root = temporary.path() / "diagnostic-foreign-part.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        const auto report = warning_report(warning_diagnostic(
            "library", "part:foreign", library_report_subject(fixture.bundle), std::nullopt,
            std::nullopt, OrderedJson::array({{{"kind", "part_definition"}, {"index", 0}}})));
        replace_artifact_payload(root, manifest, "diagnostics", report.dump());
        manifest["run"]["status"] = "expected-diagnostics";
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
    }

    SECTION("diagnostic part reference names another library release") {
        const auto root = temporary.path() / "diagnostic-foreign-release.volt";
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        const auto report = warning_report(
            warning_diagnostic("library", "part:resistor",
                               library_report_subject(fixture.bundle.identity().namespace_name(),
                                                      "foreign", fixture.bundle.library_digest()),
                               std::nullopt, std::nullopt,
                               OrderedJson::array({{{"kind", "part_definition"}, {"index", 0}}})));
        replace_artifact_payload(root, manifest, "diagnostics", report.dump());
        manifest["run"]["status"] = "expected-diagnostics";
        reseal_manifest(root, manifest);
        check_open_error(root, volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
    }
}

TEST_CASE("ProjectBundle v2 rejects selected parts that implement another component") {
    const auto fixture = two_part_fixture();
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);

    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "swapped-selected-parts.volt";
    builder.build().write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
    const auto logical_artifact =
        std::ranges::find(manifest.at("artifacts"), "logical_model",
                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(logical_artifact != manifest.at("artifacts").end());
    auto payload =
        OrderedJson::parse(read_bytes(root / logical_artifact->at("path").get<std::string>()));
    REQUIRE(payload.at("components").size() == 2U);
    auto first_selected = payload.at("components").at(0).at("selected_library_part");
    payload.at("components").at(0).at("selected_library_part") =
        payload.at("components").at(1).at("selected_library_part");
    payload.at("components").at(1).at("selected_library_part") = std::move(first_selected);
    const auto canonical =
        volt::io::write_logical_circuit(volt::io::read_logical_circuit_text(payload.dump()));
    replace_artifact_payload(root, manifest, "logical_model", canonical);
    reseal_manifest(root, manifest);

    check_open_error(root, volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
}

TEST_CASE("ProjectBundle v2 binds footprint pad references to their paired placements") {
    const auto fixture = volt::test::project_bundle_v2::mixed_footprint_board_fixture();
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
    builder.add_board(volt::io::DesignKey{"main"}, fixture.board, fixture.compiled, fixture.scene,
                      fixture.bundle);
    const auto original = builder.build();
    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "diagnostic-foreign-pad.volt";
    original.write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
    const auto report = warning_report(
        warning_diagnostic("board", "pcb:Main", "pcb.board", std::optional<std::string>{"main"},
                           std::optional<std::string>{"Main"},
                           OrderedJson::array({{{"kind", "component_placement"}, {"index", 0}},
                                               {{"kind", "footprint_pad"}, {"index", 1}}})));
    replace_artifact_payload(root, manifest, "diagnostics", report.dump());
    manifest["run"]["status"] = "expected-diagnostics";
    reseal_manifest(root, manifest);
    check_open_error(root, volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
}

TEST_CASE("ProjectBundle v2 rejects unmatched expectation references") {
    const auto fixture = LogicalFixture{};
    const auto original = fixture.builder().build();
    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "foreign-expectation.volt";
    original.write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
    replace_artifact_payload(root, manifest, "diagnostics",
                             missing_expectation_report("foreign").dump());
    manifest["run"]["ok"] = false;
    manifest["run"]["status"] = "failed";
    reseal_manifest(root, manifest);
    check_open_error(root, volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
}

TEST_CASE("ProjectBundle v2 rejects extraneous direct edges and cycles after valid resealing") {
    const auto fixture = LogicalFixture{};
    auto secondary = volt::Circuit{};
    auto builder = fixture.builder();
    builder.add_logical(volt::io::DesignKey{"secondary"}, secondary, fixture.bundle());
    const auto original = builder.build();
    const auto temporary = TempDirectory{};

    const auto mutate = [&](const std::filesystem::path &root, bool cycle) {
        original.write(root);
        auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
        auto logicals = std::vector<OrderedJson *>{};
        for (auto &artifact : manifest.at("artifacts")) {
            if (artifact.at("kind") == "logical_model") {
                logicals.push_back(&artifact);
            }
        }
        REQUIRE(logicals.size() == 2U);
        logicals[0]
            ->at("depends_on")
            .push_back(OrderedJson{{"artifact", logicals[1]->at("id")},
                                   {"content_digest", logicals[1]->at("content_digest")}});
        if (cycle) {
            logicals[1]
                ->at("depends_on")
                .push_back(OrderedJson{{"artifact", logicals[0]->at("id")},
                                       {"content_digest", logicals[0]->at("content_digest")}});
        }
        reseal_manifest(root, manifest);
    };

    const auto extra = temporary.path() / "extra-edge.volt";
    mutate(extra, false);
    CHECK_THROWS_AS(volt::io::ProjectBundle::open(extra), volt::io::ProjectBundleOpenError);

    const auto cycle = temporary.path() / "cycle.volt";
    mutate(cycle, true);
    CHECK_THROWS_AS(volt::io::ProjectBundle::open(cycle), volt::io::ProjectBundleOpenError);

    const auto alias = temporary.path() / "alias.volt";
    original.write(alias);
    const auto manifest = OrderedJson::parse(read_bytes(alias / "manifest.volt.json"));
    auto logical_paths = std::vector<std::filesystem::path>{};
    for (const auto &artifact : manifest.at("artifacts")) {
        if (artifact.at("kind") == "logical_model") {
            logical_paths.push_back(alias / artifact.at("path").get<std::string>());
        }
    }
    REQUIRE(logical_paths.size() == 2U);
    CHECK(read_bytes(logical_paths[0]) == read_bytes(logical_paths[1]));
    std::filesystem::remove(logical_paths[1]);
    std::filesystem::create_hard_link(logical_paths[0], logical_paths[1]);
    CHECK_THROWS_AS(volt::io::ProjectBundle::open(alias), volt::io::ProjectBundleOpenError);
}
