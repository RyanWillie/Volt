#include <optional>
#include <type_traits>

#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/io/project_bundle_v2_writer.hpp>
#include <volt/io/schematic/schematic_writer.hpp>

static_assert(!std::is_copy_constructible_v<volt::io::ProjectBundle>);
static_assert(std::is_move_constructible_v<volt::io::ProjectBundle>);
static_assert(!std::is_default_constructible_v<volt::io::ProjectBundleGraphV2View>);
static_assert(!std::is_copy_constructible_v<volt::io::ProjectBundleV2>);
static_assert(std::is_move_constructible_v<volt::io::ProjectBundleV2>);

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
    using volt::io::BundleIntegrityStatus;
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
    auto bundle_builder = volt::io::ProjectBundleV2Builder{
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
    return static_cast<int>(ProjectBundleSchemaVersion::V1) == 1 &&
                   BundleIntegrityStatus::LegacyUnverified != BundleIntegrityStatus::VerifiedV2 &&
                   error.code() == ProjectBundleOpenErrorCode::MissingBundle &&
                   library.library().components().empty() && design.value() == "link" &&
                   layer.value() == "F.Cu" && path.value() == "models/link.json" &&
                   build_id.content_hash() == volt::sha256_content_hash("link") &&
                   lock.libraries().empty() &&
                   export_schema.version() == volt::io::ExportRequestSchemaVersion::V1 &&
                   project_bundle.artifacts().size() == 3U &&
                   project_bundle.dependency_lock().libraries().empty() &&
                   !project_bundle.build_id().content_hash().value().empty() &&
                   volt::io::artifact_kind_name(volt::io::ArtifactKind::LogicalModel) ==
                       "logical_model" &&
                   volt::io::symbol_definition_format_name() == "volt.symbol-definition"
               ? 0
               : 1;
}
