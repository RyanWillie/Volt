#include "project_bundle_v2.hpp"

#include <exception>
#include <memory>
#include <string>
#include <utility>

#include "project_bundle_v2_internal.hpp"

namespace volt::io::v2_open {

[[nodiscard]] std::shared_ptr<const detail::ProjectBundleStorage>
open_verified(const detail::BundleSource &source, detail::CapturedEntry manifest_capture) {
    auto manifest = parse_manifest(manifest_capture.bytes);
    verify_build_and_bundle_digests(manifest);
    const auto manifest_identity = manifest_capture.identity;

    auto storage = std::make_shared<detail::ProjectBundleStorage>();
    storage->schema = ProjectBundleSchemaVersion::V2;
    storage->storage_kind = source.kind();
    storage->project_name = manifest.project.name;
    storage->project_version = manifest.project.version;
    storage->project_description = manifest.project.description;
    storage->manifest_bytes = std::move(manifest_capture.bytes);
    storage->v2_run = manifest.run;
    storage->v2_build_id = manifest.build_id;
    storage->v2_bundle_digest = manifest.bundle_digest;
    storage->v2_authoring_inputs_digest = manifest.authoring_digest;
    storage->v2_dependency_lock = manifest.lock;
    storage->v2_export_selection = manifest.exports;

    verify_descriptors_and_capture(source, manifest, *storage, manifest_identity);
    const auto index = artifact_index(*storage);
    verify_graph_references_and_dag(*storage, index);

    auto library = LibraryDecoded{};
    decode_library_artifacts(*storage, library);
    decode_project_models(*storage);
    verify_dependency_lock(*storage, manifest.lock, index);
    decode_compiled_and_scenes(*storage);
    verify_owner_graph(*storage, library, index);

    storage->v2_diagnostics = unique_artifact(*storage, ArtifactKind::Diagnostics);
    storage->v2_tests = unique_artifact(*storage, ArtifactKind::ProjectTests);
    verify_reports(storage->v2_artifacts[storage->v2_diagnostics].bytes,
                   storage->v2_artifacts[storage->v2_tests].bytes, manifest.run);
    verify_exports(*storage, library, manifest.exports);
    return storage;
}

} // namespace volt::io::v2_open

namespace volt::io {

namespace detail {

std::shared_ptr<const ProjectBundleStorage> open_project_bundle_v2(const BundleSource &source,
                                                                   CapturedEntry manifest_capture) {
    try {
        return v2_open::open_verified(source, std::move(manifest_capture));
    } catch (const ProjectBundleOpenError &) {
        throw;
    } catch (const std::exception &error) {
        v2_open::fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                      "native typed reopen failed: " + std::string{error.what()});
    }
}

} // namespace detail
} // namespace volt::io
