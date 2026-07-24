#include <volt/io/project_bundle.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "project_bundle_source.hpp"
#include "project_bundle_storage.hpp"
#include "project_bundle_v1.hpp"

namespace volt::io {
namespace {

[[nodiscard]] std::string_view meaning_name(ProjectBundleV2Meaning meaning) noexcept {
    switch (meaning) {
    case ProjectBundleV2Meaning::ArtifactGraph:
        return "artifact_graph";
    case ProjectBundleV2Meaning::ArtifactRoles:
        return "artifact_roles";
    case ProjectBundleV2Meaning::ArtifactKinds:
        return "artifact_kinds";
    case ProjectBundleV2Meaning::DependencyEdges:
        return "dependency_edges";
    case ProjectBundleV2Meaning::DependencyLock:
        return "dependency_lock";
    case ProjectBundleV2Meaning::BuildId:
        return "build_id";
    case ProjectBundleV2Meaning::AuthoringInputsDigest:
        return "authoring_inputs_digest";
    case ProjectBundleV2Meaning::BundleDigest:
        return "bundle_digest";
    case ProjectBundleV2Meaning::CompleteArtifactDigests:
        return "complete_artifact_digests";
    case ProjectBundleV2Meaning::CompiledBoards:
        return "compiled_boards";
    case ProjectBundleV2Meaning::BoardScenes:
        return "board_scenes";
    case ProjectBundleV2Meaning::SelectedClosure:
        return "selected_closure";
    case ProjectBundleV2Meaning::CapabilityRecords:
        return "capability_records";
    case ProjectBundleV2Meaning::VerifiedProvenance:
        return "verified_provenance";
    }
    return "unknown";
}

[[nodiscard]] std::string unavailable_message(ProjectBundleSchemaVersion schema,
                                              ProjectBundleV2Meaning meaning) {
    return "ProjectBundle schema " + std::to_string(static_cast<std::uint32_t>(schema)) +
           " does not contain requested v2 meaning " + std::string{meaning_name(meaning)};
}

} // namespace

ProjectBundleOpenError::ProjectBundleOpenError(ProjectBundleOpenErrorCode code, std::string message)
    : std::runtime_error{message}, code_{code} {}

ProjectBundleUnavailableError::ProjectBundleUnavailableError(ProjectBundleSchemaVersion schema,
                                                             ProjectBundleV2Meaning meaning)
    : std::logic_error{unavailable_message(schema, meaning)}, schema_{schema}, meaning_{meaning} {}

LegacyProjectBundleV1ArtifactView::LegacyProjectBundleV1ArtifactView(
    std::shared_ptr<const detail::ProjectBundleStorage> storage, std::size_t index)
    : storage_{std::move(storage)}, index_{index} {}

std::string LegacyProjectBundleV1ArtifactView::kind() const {
    return storage_->artifacts.at(index_).kind;
}

std::string LegacyProjectBundleV1ArtifactView::name() const {
    return storage_->artifacts.at(index_).name;
}

std::string LegacyProjectBundleV1ArtifactView::path() const {
    return storage_->artifacts.at(index_).path;
}

std::string LegacyProjectBundleV1ArtifactView::media_type() const {
    return storage_->artifacts.at(index_).media_type;
}

std::optional<std::string>
LegacyProjectBundleV1ArtifactView::group_value(const std::string &key) const {
    const auto &group = storage_->artifacts.at(index_).group;
    const auto match = group.find(key);
    return match == group.end() ? std::nullopt : std::optional{match->second};
}

std::optional<std::string> LegacyProjectBundleV1ArtifactView::recorded_sha256() const {
    return storage_->artifacts.at(index_).sha256;
}

std::string LegacyProjectBundleV1ArtifactView::manifest_record_json() const {
    return storage_->artifacts.at(index_).manifest_record_json;
}

std::string LegacyProjectBundleV1ArtifactView::bytes() const {
    return storage_->artifacts.at(index_).bytes;
}

LegacyProjectBundleV1LogicalDocumentView::LegacyProjectBundleV1LogicalDocumentView(
    std::shared_ptr<const detail::ProjectBundleStorage> storage, std::size_t index)
    : storage_{std::move(storage)}, index_{index} {}

std::string LegacyProjectBundleV1LogicalDocumentView::design() const {
    return storage_->circuits.at(index_).design;
}

LegacyProjectBundleV1ArtifactView LegacyProjectBundleV1LogicalDocumentView::artifact() const {
    return LegacyProjectBundleV1ArtifactView{storage_, storage_->circuits.at(index_).artifact};
}

LegacyProjectBundleV1SchematicView::LegacyProjectBundleV1SchematicView(
    std::shared_ptr<const detail::ProjectBundleStorage> storage, std::size_t index)
    : storage_{std::move(storage)}, index_{index} {}

std::string LegacyProjectBundleV1SchematicView::design() const {
    return storage_->schematics.at(index_).design;
}

std::string LegacyProjectBundleV1SchematicView::schematic() const {
    return storage_->schematics.at(index_).schematic;
}

LegacyProjectBundleV1ArtifactView LegacyProjectBundleV1SchematicView::artifact() const {
    return LegacyProjectBundleV1ArtifactView{storage_, storage_->schematics.at(index_).artifact};
}

LegacyProjectBundleV1LogicalDocumentView LegacyProjectBundleV1SchematicView::circuit() const {
    return LegacyProjectBundleV1LogicalDocumentView{storage_,
                                                    storage_->schematics.at(index_).circuit};
}

LegacyProjectBundleV1BoardView::LegacyProjectBundleV1BoardView(
    std::shared_ptr<const detail::ProjectBundleStorage> storage, std::size_t index)
    : storage_{std::move(storage)}, index_{index} {}

std::string LegacyProjectBundleV1BoardView::design() const {
    return storage_->boards.at(index_).design;
}

std::string LegacyProjectBundleV1BoardView::board() const {
    return storage_->boards.at(index_).board;
}

LegacyProjectBundleV1ArtifactView LegacyProjectBundleV1BoardView::artifact() const {
    return LegacyProjectBundleV1ArtifactView{storage_, storage_->boards.at(index_).artifact};
}

LegacyProjectBundleV1LogicalDocumentView LegacyProjectBundleV1BoardView::circuit() const {
    return LegacyProjectBundleV1LogicalDocumentView{storage_, storage_->boards.at(index_).circuit};
}

LegacyProjectBundleV1View::LegacyProjectBundleV1View(
    std::shared_ptr<const detail::ProjectBundleStorage> storage)
    : storage_{std::move(storage)} {}

std::string LegacyProjectBundleV1View::project_name() const { return storage_->project_name; }

std::optional<std::string> LegacyProjectBundleV1View::project_version() const {
    return storage_->project_version;
}

std::optional<std::string> LegacyProjectBundleV1View::project_description() const {
    return storage_->project_description;
}

std::string LegacyProjectBundleV1View::manifest_bytes() const { return storage_->manifest_bytes; }

std::vector<LegacyProjectBundleV1ArtifactView> LegacyProjectBundleV1View::artifacts() const {
    auto result = std::vector<LegacyProjectBundleV1ArtifactView>{};
    result.reserve(storage_->artifacts.size());
    for (auto index = std::size_t{0}; index < storage_->artifacts.size(); ++index) {
        result.emplace_back(storage_, index);
    }
    return result;
}

std::vector<LegacyProjectBundleV1LogicalDocumentView> LegacyProjectBundleV1View::circuits() const {
    auto result = std::vector<LegacyProjectBundleV1LogicalDocumentView>{};
    result.reserve(storage_->circuits.size());
    for (auto index = std::size_t{0}; index < storage_->circuits.size(); ++index) {
        result.emplace_back(storage_, index);
    }
    return result;
}

std::vector<LegacyProjectBundleV1SchematicView> LegacyProjectBundleV1View::schematics() const {
    auto result = std::vector<LegacyProjectBundleV1SchematicView>{};
    result.reserve(storage_->schematics.size());
    for (auto index = std::size_t{0}; index < storage_->schematics.size(); ++index) {
        result.emplace_back(storage_, index);
    }
    return result;
}

std::vector<LegacyProjectBundleV1BoardView> LegacyProjectBundleV1View::boards() const {
    auto result = std::vector<LegacyProjectBundleV1BoardView>{};
    result.reserve(storage_->boards.size());
    for (auto index = std::size_t{0}; index < storage_->boards.size(); ++index) {
        result.emplace_back(storage_, index);
    }
    return result;
}

ProjectBundleMeaningAvailability
LegacyProjectBundleV1View::availability(ProjectBundleV2Meaning) const noexcept {
    return ProjectBundleMeaningAvailability::UnavailableInV1;
}

[[noreturn]] void LegacyProjectBundleV1View::require(ProjectBundleV2Meaning meaning) const {
    throw ProjectBundleUnavailableError{ProjectBundleSchemaVersion::V1, meaning};
}

ProjectBundle::ProjectBundle(std::shared_ptr<const detail::ProjectBundleStorage> storage)
    : storage_{std::move(storage)} {}

ProjectBundle::ProjectBundle(ProjectBundle &&other) noexcept = default;
ProjectBundle &ProjectBundle::operator=(ProjectBundle &&other) noexcept = default;
ProjectBundle::~ProjectBundle() = default;

ProjectBundle ProjectBundle::open(const std::filesystem::path &path) {
    auto source = detail::open_project_bundle_source(path);
    auto manifest_capture = source->read(detail::project_bundle_manifest_path);
    return ProjectBundle{detail::open_project_bundle_v1(*source, std::move(manifest_capture))};
}

ProjectBundleSchemaVersion ProjectBundle::schema_version() const noexcept {
    return storage_->schema;
}

ProjectBundleStorageKind ProjectBundle::storage_kind() const noexcept {
    return storage_->storage_kind;
}

BundleIntegrityStatus ProjectBundle::integrity_status() const noexcept {
    return BundleIntegrityStatus::LegacyUnverified;
}

ProjectBundleContentsView ProjectBundle::view() const {
    return LegacyProjectBundleV1View{storage_};
}

LegacyProjectBundleV1View ProjectBundle::legacy_v1() const {
    if (storage_->schema != ProjectBundleSchemaVersion::V1) {
        throw ProjectBundleUnavailableError{storage_->schema,
                                            ProjectBundleV2Meaning::ArtifactGraph};
    }
    return LegacyProjectBundleV1View{storage_};
}

ProjectBundleGraphV2View ProjectBundle::require_v2() const {
    // This operation requests the v2 graph view itself, so ArtifactGraph is the exact missing
    // meaning when the opened schema is v1.
    throw ProjectBundleUnavailableError{storage_->schema, ProjectBundleV2Meaning::ArtifactGraph};
}

} // namespace volt::io
