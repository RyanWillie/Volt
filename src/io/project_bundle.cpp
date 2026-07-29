#include <volt/io/project_bundle.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "project_bundle_source.hpp"
#include "project_bundle_storage.hpp"
#include "project_bundle_v2.hpp"
#include "project_bundle_v2_contract.hpp"

namespace volt::io {
namespace {

void require_current_schema(std::string_view bytes) {
    try {
        const auto root = nlohmann::json::parse(bytes);
        if (!root.is_object() || !root.contains("format") || !root.at("format").is_string() ||
            root.at("format").get<std::string>() != "volt.project_result") {
            throw ProjectBundleOpenError{ProjectBundleOpenErrorCode::UnsupportedFormat,
                                         "ProjectBundle manifest format is unsupported"};
        }
        if (!root.contains("schema_version") || !root.at("schema_version").is_number_unsigned()) {
            throw ProjectBundleOpenError{ProjectBundleOpenErrorCode::MalformedManifest,
                                         "ProjectBundle manifest schema_version is invalid"};
        }
        const auto schema = root.at("schema_version").get<std::uint32_t>();
        if (schema != static_cast<std::uint32_t>(ProjectBundleSchemaVersion::V2)) {
            throw ProjectBundleOpenError{ProjectBundleOpenErrorCode::UnsupportedSchema,
                                         "ProjectBundle manifest schema is unsupported: " +
                                             std::to_string(schema)};
        }
    } catch (const ProjectBundleOpenError &) {
        throw;
    } catch (const nlohmann::json::exception &error) {
        throw ProjectBundleOpenError{ProjectBundleOpenErrorCode::MalformedManifest,
                                     "ProjectBundle manifest is malformed JSON: " +
                                         std::string{error.what()}};
    }
}

} // namespace

ProjectBundleOpenError::ProjectBundleOpenError(ProjectBundleOpenErrorCode code, std::string message)
    : std::runtime_error{message}, code_{code} {}

ProjectBundleArtifactView::ProjectBundleArtifactView(
    std::shared_ptr<const detail::ProjectBundleStorage> storage, std::size_t index)
    : storage_{std::move(storage)}, index_{index} {}

const ArtifactDescriptor &ProjectBundleArtifactView::descriptor() const & {
    return storage_->v2_artifacts.at(index_).descriptor;
}

std::string ProjectBundleArtifactView::id_json() const {
    return detail::project_bundle_v2_artifact_id_json(
        storage_->v2_artifacts.at(index_).descriptor.id());
}

std::string ProjectBundleArtifactView::manifest_record_json() const {
    return storage_->v2_artifacts.at(index_).manifest_record_json;
}

std::string ProjectBundleArtifactView::bytes() const {
    return storage_->v2_artifacts.at(index_).bytes;
}

LoadedLogicalModelView::LoadedLogicalModelView(
    std::shared_ptr<const detail::ProjectBundleStorage> storage, std::size_t index)
    : storage_{std::move(storage)}, index_{index} {}

const DesignKey &LoadedLogicalModelView::design() const & {
    return storage_->v2_circuits.at(index_).design;
}

ProjectBundleArtifactView LoadedLogicalModelView::artifact() const {
    return {storage_, storage_->v2_circuits.at(index_).artifact};
}

const Circuit &LoadedLogicalModelView::model() const & {
    return *storage_->v2_circuits.at(index_).model;
}

LoadedSchematicView::LoadedSchematicView(
    std::shared_ptr<const detail::ProjectBundleStorage> storage, std::size_t index)
    : storage_{std::move(storage)}, index_{index} {}

const DesignKey &LoadedSchematicView::design() const & {
    return storage_->v2_schematics.at(index_).design;
}

const SchematicKey &LoadedSchematicView::schematic() const & {
    return storage_->v2_schematics.at(index_).schematic;
}

ProjectBundleArtifactView LoadedSchematicView::artifact() const {
    return {storage_, storage_->v2_schematics.at(index_).artifact};
}

LoadedLogicalModelView LoadedSchematicView::circuit() const {
    return {storage_, storage_->v2_schematics.at(index_).circuit};
}

const Schematic &LoadedSchematicView::model() const & {
    return *storage_->v2_schematics.at(index_).model;
}

LoadedBoardView::LoadedBoardView(std::shared_ptr<const detail::ProjectBundleStorage> storage,
                                 std::size_t index)
    : storage_{std::move(storage)}, index_{index} {}

const DesignKey &LoadedBoardView::design() const & { return storage_->v2_boards.at(index_).design; }

const BoardName &LoadedBoardView::board() const & { return storage_->v2_boards.at(index_).board; }

ProjectBundleArtifactView LoadedBoardView::artifact() const {
    return {storage_, storage_->v2_boards.at(index_).artifact};
}

LoadedLogicalModelView LoadedBoardView::circuit() const {
    return {storage_, storage_->v2_boards.at(index_).circuit};
}

const Board &LoadedBoardView::model() const & { return *storage_->v2_boards.at(index_).model; }

LoadedCompiledBoardView::LoadedCompiledBoardView(
    std::shared_ptr<const detail::ProjectBundleStorage> storage, std::size_t index)
    : storage_{std::move(storage)}, index_{index} {}

const CompiledBoardIdentity &LoadedCompiledBoardView::identity() const & {
    return storage_->v2_compiled_boards.at(index_).model->identity();
}

ProjectBundleArtifactView LoadedCompiledBoardView::artifact() const {
    return {storage_, storage_->v2_compiled_boards.at(index_).artifact};
}

const CompiledBoard &LoadedCompiledBoardView::model() const & {
    return *storage_->v2_compiled_boards.at(index_).model;
}

LoadedBoardSceneView::LoadedBoardSceneView(
    std::shared_ptr<const detail::ProjectBundleStorage> storage, std::size_t index)
    : storage_{std::move(storage)}, index_{index} {}

ProjectBundleArtifactView LoadedBoardSceneView::artifact() const {
    return {storage_, storage_->v2_board_scenes.at(index_).artifact};
}

LoadedCompiledBoardView LoadedBoardSceneView::compiled_board() const {
    return {storage_, storage_->v2_board_scenes.at(index_).compiled_board};
}

const BoardScene &LoadedBoardSceneView::model() const & {
    return *storage_->v2_board_scenes.at(index_).model;
}

LoadedProject::LoadedProject(std::shared_ptr<const detail::ProjectBundleStorage> storage)
    : storage_{std::move(storage)} {}

std::vector<LoadedLogicalModelView> LoadedProject::circuits() const {
    auto result = std::vector<LoadedLogicalModelView>{};
    result.reserve(storage_->v2_circuits.size());
    for (auto index = std::size_t{0}; index < storage_->v2_circuits.size(); ++index) {
        result.emplace_back(storage_, index);
    }
    return result;
}

std::vector<LoadedSchematicView> LoadedProject::schematics() const {
    auto result = std::vector<LoadedSchematicView>{};
    result.reserve(storage_->v2_schematics.size());
    for (auto index = std::size_t{0}; index < storage_->v2_schematics.size(); ++index) {
        result.emplace_back(storage_, index);
    }
    return result;
}

std::vector<LoadedBoardView> LoadedProject::boards() const {
    auto result = std::vector<LoadedBoardView>{};
    result.reserve(storage_->v2_boards.size());
    for (auto index = std::size_t{0}; index < storage_->v2_boards.size(); ++index) {
        result.emplace_back(storage_, index);
    }
    return result;
}

std::vector<LoadedCompiledBoardView> LoadedProject::compiled_boards() const {
    auto result = std::vector<LoadedCompiledBoardView>{};
    result.reserve(storage_->v2_compiled_boards.size());
    for (auto index = std::size_t{0}; index < storage_->v2_compiled_boards.size(); ++index) {
        result.emplace_back(storage_, index);
    }
    return result;
}

std::vector<LoadedBoardSceneView> LoadedProject::board_scenes() const {
    auto result = std::vector<LoadedBoardSceneView>{};
    result.reserve(storage_->v2_board_scenes.size());
    for (auto index = std::size_t{0}; index < storage_->v2_board_scenes.size(); ++index) {
        result.emplace_back(storage_, index);
    }
    return result;
}

ProjectBundleArtifactView LoadedProject::diagnostics() const {
    return {storage_, storage_->v2_diagnostics};
}

ProjectBundleArtifactView LoadedProject::tests() const { return {storage_, storage_->v2_tests}; }

std::vector<ProjectBundleArtifactView> LoadedProject::selected_exports() const {
    auto result = std::vector<ProjectBundleArtifactView>{};
    result.reserve(storage_->v2_selected_exports.size());
    for (const auto index : storage_->v2_selected_exports) {
        result.emplace_back(storage_, index);
    }
    return result;
}

ProjectBundleGraphView::ProjectBundleGraphView(
    std::shared_ptr<const detail::ProjectBundleStorage> storage)
    : storage_{std::move(storage)} {}

std::string ProjectBundleGraphView::project_name() const { return storage_->project_name; }

std::optional<std::string> ProjectBundleGraphView::project_version() const {
    return storage_->project_version;
}

std::optional<std::string> ProjectBundleGraphView::project_description() const {
    return storage_->project_description;
}

std::string ProjectBundleGraphView::manifest_bytes() const { return storage_->manifest_bytes; }

const BuildId &ProjectBundleGraphView::build_id() const & { return storage_->v2_build_id.value(); }

const ContentHash &ProjectBundleGraphView::bundle_digest() const & {
    return storage_->v2_bundle_digest.value();
}

const ContentHash &ProjectBundleGraphView::authoring_inputs_digest() const & {
    return storage_->v2_authoring_inputs_digest.value();
}

const DependencyLock &ProjectBundleGraphView::dependency_lock() const & {
    return storage_->v2_dependency_lock.value();
}

const ExportSelection &ProjectBundleGraphView::export_selection() const & {
    return storage_->v2_export_selection;
}

std::vector<ProjectBundleArtifactView> ProjectBundleGraphView::artifacts() const {
    auto result = std::vector<ProjectBundleArtifactView>{};
    result.reserve(storage_->v2_artifacts.size());
    for (auto index = std::size_t{0}; index < storage_->v2_artifacts.size(); ++index) {
        result.emplace_back(storage_, index);
    }
    return result;
}

std::optional<ProjectBundleArtifactView>
ProjectBundleGraphView::artifact(const ArtifactId &id) const {
    for (auto index = std::size_t{0}; index < storage_->v2_artifacts.size(); ++index) {
        if (storage_->v2_artifacts[index].descriptor.id() == id) {
            return ProjectBundleArtifactView{storage_, index};
        }
    }
    return std::nullopt;
}

LoadedProject ProjectBundleGraphView::loaded_project() const { return LoadedProject{storage_}; }

ProjectBundle::ProjectBundle(std::shared_ptr<const detail::ProjectBundleStorage> storage)
    : storage_{std::move(storage)} {}

ProjectBundle::ProjectBundle(ProjectBundle &&other) noexcept = default;
ProjectBundle &ProjectBundle::operator=(ProjectBundle &&other) noexcept = default;
ProjectBundle::~ProjectBundle() = default;

ProjectBundle ProjectBundle::open(const std::filesystem::path &path) {
    auto source = detail::open_project_bundle_source(path);
    auto manifest_capture = source->read(detail::project_bundle_manifest_path);
    require_current_schema(manifest_capture.bytes);
    return ProjectBundle{detail::open_project_bundle_v2(*source, std::move(manifest_capture))};
}

ProjectBundleSchemaVersion ProjectBundle::schema_version() const noexcept {
    return storage_->schema;
}

ProjectBundleStorageKind ProjectBundle::storage_kind() const noexcept {
    return storage_->storage_kind;
}

ProjectBundleGraphView ProjectBundle::graph() const { return ProjectBundleGraphView{storage_}; }

} // namespace volt::io
