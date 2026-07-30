#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/pcb/board_scene.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/io/project_bundle_writer.hpp>
#include <volt/io/schematic/schematic_reader.hpp>
#include <volt/io/schematic/schematic_writer.hpp>

static_assert(!std::is_copy_constructible_v<volt::io::ProjectBundle>);
static_assert(std::is_move_constructible_v<volt::io::ProjectBundle>);
static_assert(!std::is_default_constructible_v<volt::io::ProjectBundleGraphView>);
static_assert(!std::is_copy_constructible_v<volt::io::ProjectBundlePublication>);
static_assert(std::is_move_constructible_v<volt::io::ProjectBundlePublication>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::ProjectBundleArtifactView &>().descriptor()),
                 const volt::io::ArtifactDescriptor &>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::ProjectBundleArtifactView &>().id_json()),
                 std::string>);
static_assert(std::same_as<decltype(std::declval<const volt::io::ProjectBundleArtifactView &>()
                                        .manifest_record_json()),
                           std::string>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::ProjectBundleArtifactView &>().bytes()),
                 std::string>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::LoadedLogicalModelView &>().design()),
                 const volt::io::DesignKey &>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::LoadedLogicalModelView &>().artifact()),
                 volt::io::ProjectBundleArtifactView>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::LoadedLogicalModelView &>().model()),
                 const volt::Circuit &>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::LoadedSchematicView &>().circuit()),
                 volt::io::LoadedLogicalModelView>);
static_assert(std::same_as<decltype(std::declval<const volt::io::LoadedSchematicView &>().model()),
                           const volt::Schematic &>);
static_assert(std::same_as<decltype(std::declval<const volt::io::LoadedBoardView &>().circuit()),
                           volt::io::LoadedLogicalModelView>);
static_assert(std::same_as<decltype(std::declval<const volt::io::LoadedBoardView &>().model()),
                           const volt::Board &>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::LoadedCompiledBoardView &>().identity()),
                 const volt::CompiledBoardIdentity &>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::LoadedCompiledBoardView &>().model()),
                 const volt::CompiledBoard &>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::LoadedBoardSceneView &>().compiled_board()),
                 volt::io::LoadedCompiledBoardView>);
static_assert(std::same_as<decltype(std::declval<const volt::io::LoadedBoardSceneView &>().model()),
                           const volt::BoardScene &>);
static_assert(std::same_as<decltype(std::declval<const volt::io::LoadedProject &>().circuits()),
                           std::vector<volt::io::LoadedLogicalModelView>>);
static_assert(std::same_as<decltype(std::declval<const volt::io::LoadedProject &>().schematics()),
                           std::vector<volt::io::LoadedSchematicView>>);
static_assert(std::same_as<decltype(std::declval<const volt::io::LoadedProject &>().boards()),
                           std::vector<volt::io::LoadedBoardView>>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::LoadedProject &>().compiled_boards()),
                 std::vector<volt::io::LoadedCompiledBoardView>>);
static_assert(std::same_as<decltype(std::declval<const volt::io::LoadedProject &>().board_scenes()),
                           std::vector<volt::io::LoadedBoardSceneView>>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::LoadedProject &>().selected_exports()),
                 std::vector<volt::io::ProjectBundleArtifactView>>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::ProjectBundleGraphView &>().artifacts()),
                 std::vector<volt::io::ProjectBundleArtifactView>>);
static_assert(std::same_as<
              decltype(std::declval<const volt::io::ProjectBundleGraphView &>().loaded_project()),
              volt::io::LoadedProject>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::ProjectBundleGraphView &>().project_name()),
                 std::string>);
static_assert(std::same_as<
              decltype(std::declval<const volt::io::ProjectBundleGraphView &>().project_version()),
              std::optional<std::string>>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::ProjectBundleGraphView &>().build_id()),
                 const volt::io::BuildId &>);
static_assert(
    std::same_as<decltype(std::declval<const volt::io::ProjectBundleGraphView &>().bundle_digest()),
                 const volt::ContentHash &>);
static_assert(std::same_as<
              decltype(std::declval<const volt::io::ProjectBundleGraphView &>().dependency_lock()),
              const volt::io::DependencyLock &>);
static_assert(std::same_as<
              decltype(std::declval<const volt::io::ProjectBundleGraphView &>().export_selection()),
              const volt::io::ExportSelection &>);
static_assert(std::same_as<decltype(std::declval<const volt::io::ProjectBundleGraphView &>()
                                        .artifact(std::declval<const volt::io::ArtifactId &>())),
                           std::optional<volt::io::ProjectBundleArtifactView>>);

namespace {

class EmptyResolver final : public volt::PartAssetResolver {
  public:
    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &) const override {
        return std::nullopt;
    }
};

} // namespace

int main() {
    using volt::io::ProjectBundleOpenError;
    using volt::io::ProjectBundleOpenErrorCode;
    using volt::io::ProjectBundleSchemaVersion;
    const auto error =
        ProjectBundleOpenError{ProjectBundleOpenErrorCode::MissingBundle, "link contract"};
    const auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"link.contract", "1", volt::PartLibrarySchemaVersion::V1}};
    const auto resolver = EmptyResolver{};
    const auto library =
        volt::io::PartLibraryBundle::build_with_component_roots(builder, {}, {}, resolver);
    const auto design = volt::io::DesignKey{"link"};
    const auto layer = volt::io::BoardLayerKey{"F.Cu"};
    const auto path = volt::io::RelativeBundlePath{"models/link.json"};
    const auto build_id = volt::io::BuildId{volt::sha256_content_hash("link")};
    const auto lock = volt::io::DependencyLock{};
    const auto export_schema = volt::io::ExportRequestSchema{};
    const auto circuit = volt::Circuit{};
    auto bundle_builder = volt::io::ProjectBundleBuilder{
        volt::io::ProjectIdentity{"link", std::nullopt, std::nullopt},
        volt::io::ProjectRunSummary{true, volt::io::ProjectStatus::Clean, "default", {"design"}},
        volt::io::LogicalInputName{"project.py"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"project.py"}, "source"}},
        volt::io::ProjectReport{
            R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
    bundle_builder.add_logical(volt::io::DesignKey{"link"}, circuit, library);
    const auto project_bundle = bundle_builder.build();
    auto *volatile write_scene = &volt::io::write_board_scene;
    auto *volatile read_scene = &volt::io::read_board_scene;
    auto *volatile read_symbol_text = &volt::io::read_symbol_definition_text;
    static_cast<void>(write_scene);
    static_cast<void>(read_scene);
    static_cast<void>(read_symbol_text);
    return static_cast<int>(ProjectBundleSchemaVersion::V2) == 2 &&
                   error.code() == ProjectBundleOpenErrorCode::MissingBundle &&
                   library.library().components().empty() && design.value() == "link" &&
                   layer.value() == "F.Cu" && path.value() == "models/link.json" &&
                   build_id.content_hash() == volt::sha256_content_hash("link") &&
                   lock.libraries().empty() &&
                   export_schema.version() == volt::io::ExportRequestSchemaVersion::V1 &&
                   project_bundle.artifacts().size() == 3U &&
                   project_bundle.dependency_lock().libraries().empty() &&
                   !project_bundle.build_id().content_hash().value().empty() &&
                   project_bundle.manifest_bytes().starts_with(
                       R"({"format":"volt.project_result","schema_version":2,)") &&
                   volt::io::artifact_kind_name(volt::io::ArtifactKind::LogicalModel) ==
                       "logical_model" &&
                   volt::io::symbol_definition_format_name() == "volt.symbol-definition"
               ? 0
               : 1;
}
