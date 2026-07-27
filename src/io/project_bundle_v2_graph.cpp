#include "project_bundle_v2_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "project_bundle_v2_contract.hpp"

namespace volt::io::v2_open {

[[nodiscard]] std::string expected_library_build(const ArtifactDescriptor &artifact,
                                                 const ExportSelection &exports) {
    return std::visit(
        [&](const auto &owner) -> std::string {
            using T = std::decay_t<decltype(owner)>;
            if constexpr (std::same_as<T, LibraryComponentRef> ||
                          std::same_as<T, LibraryAttachmentRef> ||
                          std::same_as<T, LibraryAssetRef>) {
                return owner.library_bundle_digest.value();
            } else if constexpr (std::same_as<T, LibraryPartRef>) {
                return owner.library_digest().value();
            } else if constexpr (std::same_as<T, ExportArtifactIdentity>) {
                for (const auto &request : exports) {
                    const auto digest =
                        sha256_content_hash(detail::project_bundle_v2_export_request_json(request));
                    if (digest != owner.request_digest ||
                        !std::holds_alternative<LibraryAssetExportTarget>(request.target)) {
                        continue;
                    }
                    return std::get<LibraryAssetExportTarget>(request.target)
                        .asset.library_bundle_digest.value();
                }
                fail(ProjectBundleOpenErrorCode::OwnershipViolation,
                     "library export producer has no matching export request");
            } else {
                fail(ProjectBundleOpenErrorCode::OwnershipViolation,
                     "library-produced artifact has a non-library owner");
            }
        },
        artifact.id().owner());
}

void verify_descriptors_and_capture(const BundleSource &source, const ParsedManifest &manifest,
                                    ProjectBundleStorage &storage,
                                    std::optional<FileIdentity> manifest_identity) {
    auto expected_paths = std::set<std::string>{std::string{detail::project_bundle_manifest_path}};
    auto folded_paths = std::set<std::string>{};
    auto identities = std::set<FileIdentity>{};
    if (manifest_identity.has_value()) {
        identities.insert(*manifest_identity);
    }
    for (auto index = std::size_t{0}; index < manifest.artifacts.size(); ++index) {
        const auto &artifact = manifest.artifacts[index];
        const auto expected_schema = detail::project_bundle_v2_schema_for_kind(artifact.kind());
        require(artifact.role() == detail::project_bundle_v2_role_for_kind(artifact.kind()) &&
                    artifact.schema_format() == expected_schema.format &&
                    artifact.schema_version() == expected_schema.version &&
                    artifact.media_type() == expected_schema.media_type,
                ProjectBundleOpenErrorCode::MalformedManifest,
                "artifact closed role/schema/media mapping is invalid");
        require(artifact.path().value() == detail::project_bundle_v2_artifact_path(artifact.id()),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "artifact path does not match its exact ArtifactId");
        require(detail::project_bundle_v2_safe_path(artifact.path().value()),
                ProjectBundleOpenErrorCode::UnsafePath,
                "artifact path is not canonical and portable");
        require(expected_paths.insert(artifact.path().value()).second,
                ProjectBundleOpenErrorCode::DuplicateIdentity, "artifact path is duplicated");
        auto folded = artifact.path().value();
        std::ranges::transform(folded, folded.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        require(folded_paths.insert(std::move(folded)).second,
                ProjectBundleOpenErrorCode::DuplicateIdentity,
                "artifact paths collide under ASCII case folding");

        const auto expected_producer = detail::project_bundle_v2_producer_for_kind(artifact.kind());
        require(artifact.producer_name() == expected_producer &&
                    artifact.producer_version() == detail::project_bundle_v2_producer_version,
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "artifact producer name or version is invalid");
        const auto expected_build = expected_producer == detail::project_bundle_v2_producer
                                        ? manifest.build_id.content_hash().value()
                                        : expected_library_build(artifact, manifest.exports);
        require(artifact.producer_build().value() == expected_build,
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "artifact producer build does not match its exact owner");

        auto captured = source.read(artifact.path().value());
        require(sha256_content_hash(captured.bytes) == artifact.content_digest(),
                ProjectBundleOpenErrorCode::DigestMismatch,
                "artifact payload digest mismatch: " + artifact.path().value());
        if (captured.identity.has_value()) {
            require(identities.insert(*captured.identity).second,
                    ProjectBundleOpenErrorCode::DuplicateIdentity,
                    "two artifact paths alias one physical file");
        }
        storage.v2_artifacts.push_back(ProjectBundleStorage::V2Artifact{
            artifact, canonical(manifest.root.at("artifacts").at(index)),
            std::move(captured.bytes)});
    }
    const auto actual_paths = source.paths();
    require(std::set<std::string>{actual_paths.begin(), actual_paths.end()} == expected_paths,
            ProjectBundleOpenErrorCode::MalformedArchive,
            "bundle contains missing or forbidden extra files");
}

[[nodiscard]] std::map<std::string, std::size_t>
artifact_index(const ProjectBundleStorage &storage) {
    auto result = std::map<std::string, std::size_t>{};
    for (auto index = std::size_t{0}; index < storage.v2_artifacts.size(); ++index) {
        result.emplace(
            detail::project_bundle_v2_artifact_key(storage.v2_artifacts[index].descriptor.id()),
            index);
    }
    return result;
}

[[nodiscard]] std::set<std::string> dependency_keys(const ArtifactDescriptor &artifact) {
    auto result = std::set<std::string>{};
    for (const auto &dependency : artifact.dependencies()) {
        result.insert(detail::project_bundle_v2_artifact_key(dependency.artifact()));
    }
    return result;
}

void verify_graph_references_and_dag(const ProjectBundleStorage &storage,
                                     const std::map<std::string, std::size_t> &index) {
    for (const auto &artifact : storage.v2_artifacts) {
        for (const auto &dependency : artifact.descriptor.dependencies()) {
            const auto key = detail::project_bundle_v2_artifact_key(dependency.artifact());
            const auto target = index.find(key);
            require(target != index.end(), ProjectBundleOpenErrorCode::OwnershipViolation,
                    "artifact direct edge points to a missing target");
            require(storage.v2_artifacts[target->second].descriptor.content_digest() ==
                        dependency.content_digest(),
                    ProjectBundleOpenErrorCode::DigestMismatch,
                    "artifact direct edge carries a stale target digest");
            require(!is_export_artifact(artifact.descriptor) ||
                        !is_export_artifact(storage.v2_artifacts[target->second].descriptor),
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "selected export depends on another selected export");
            require(!(!is_export_artifact(artifact.descriptor) &&
                      is_export_artifact(storage.v2_artifacts[target->second].descriptor)),
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "required-default artifact depends on a selected export");
        }
    }
    auto state = std::map<std::string, std::uint8_t>{};
    auto visit = std::function<void(const std::string &)>{};
    visit = [&](const std::string &key) {
        require(state[key] != 1U, ProjectBundleOpenErrorCode::OwnershipViolation,
                "artifact graph contains a cycle");
        if (state[key] == 2U) {
            return;
        }
        state[key] = 1U;
        for (const auto &dependency :
             storage.v2_artifacts[index.at(key)].descriptor.dependencies()) {
            visit(detail::project_bundle_v2_artifact_key(dependency.artifact()));
        }
        state[key] = 2U;
    };
    for (const auto &[key, unused] : index) {
        static_cast<void>(unused);
        visit(key);
    }
}

[[nodiscard]] std::size_t unique_artifact(const ProjectBundleStorage &storage, ArtifactKind kind) {
    auto result = std::optional<std::size_t>{};
    for (auto index = std::size_t{0}; index < storage.v2_artifacts.size(); ++index) {
        if (storage.v2_artifacts[index].descriptor.kind() != kind) {
            continue;
        }
        require(!result.has_value(), ProjectBundleOpenErrorCode::DuplicateIdentity,
                "required singleton artifact is duplicated");
        result = index;
    }
    require(result.has_value(), ProjectBundleOpenErrorCode::MissingEntry,
            "required singleton artifact is missing");
    return *result;
}

void verify_dependency_lock(const ProjectBundleStorage &storage, const DependencyLock &lock,
                            const std::map<std::string, std::size_t> &index) {
    auto origins = std::set<std::tuple<std::string, std::string, std::string>>{};
    for (const auto &artifact : storage.v2_artifacts) {
        std::visit(
            [&](const auto &owner) {
                using T = std::decay_t<decltype(owner)>;
                if constexpr (std::same_as<T, LibraryComponentRef> ||
                              std::same_as<T, LibraryAttachmentRef> ||
                              std::same_as<T, LibraryAssetRef>) {
                    origins.emplace(owner.library_namespace, owner.library_version,
                                    owner.library_bundle_digest.value());
                } else if constexpr (std::same_as<T, LibraryPartRef>) {
                    origins.emplace(owner.library_namespace(), owner.library_version(),
                                    owner.library_digest().value());
                }
            },
            artifact.descriptor.id().owner());
    }
    auto locked_origins = std::set<std::tuple<std::string, std::string, std::string>>{};
    for (const auto &library : lock.libraries()) {
        locked_origins.emplace(library.library_namespace, library.library_version,
                               library.library_bundle_digest.value());
    }
    require(origins == locked_origins, ProjectBundleOpenErrorCode::OwnershipViolation,
            "dependency lock does not equal the vendored library origin closure");

    auto vendored_parts = std::set<std::string>{};
    for (const auto &selection : lock.selected_parts()) {
        const auto key = detail::project_bundle_v2_artifact_key(selection.vendored_part.artifact());
        const auto match = index.find(key);
        require(match != index.end() &&
                    storage.v2_artifacts[match->second].descriptor.content_digest() ==
                        selection.vendored_part.content_digest(),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "dependency lock selected part is missing or stale");
        vendored_parts.insert(key);
    }
    auto actual_parts = std::set<std::string>{};
    for (const auto &artifact : storage.v2_artifacts) {
        if (artifact.descriptor.kind() == ArtifactKind::PartDefinition) {
            actual_parts.insert(detail::project_bundle_v2_artifact_key(artifact.descriptor.id()));
        }
    }
    require(vendored_parts == actual_parts, ProjectBundleOpenErrorCode::OwnershipViolation,
            "dependency lock selected parts do not equal the vendored part closure");
}

void verify_report(std::string_view bytes, ArtifactKind kind, const ProjectRunSummary &run) {
    const auto report =
        parse_json(bytes, kind == ArtifactKind::Diagnostics ? "diagnostics" : "project tests",
                   ProjectBundleOpenErrorCode::ModelDecodeFailure);
    if (kind == ArtifactKind::Diagnostics) {
        require_key_set(
            report,
            {"status", "summary", "diagnostics", "expected", "unexpected", "missing_expected"},
            "diagnostics report");
        const auto expected_status = run.status == ProjectStatus::Clean ? "clean"
                                     : run.status == ProjectStatus::ExpectedDiagnostics
                                         ? "expected-diagnostics"
                                         : "failed";
        require(string_field(report, "status", "diagnostics report") == expected_status,
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "diagnostics status disagrees with the run");
    } else {
        require_key_set(report, {"summary", "tests"}, "project tests");
        const auto &summary = field(report, "summary", "project tests");
        require(summary.is_object() && summary.contains("failed") &&
                    summary.at("failed").is_number_unsigned(),
                ProjectBundleOpenErrorCode::ModelDecodeFailure,
                "project tests summary does not carry failed count");
        require(summary.at("failed").get<std::uint64_t>() == 0U ||
                    run.status == ProjectStatus::Failed,
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "project tests disagree with the run status");
    }
}

} // namespace volt::io::v2_open
