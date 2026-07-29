#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/pcb/footprints/footprints.hpp>
#include <volt/schematic/symbols.hpp>

#include "project_bundle_source.hpp"
#include "project_bundle_storage.hpp"

namespace volt::io::v2_open {

using Json = nlohmann::ordered_json;
using ArtifactIndex = std::map<std::string, std::size_t>;
using detail::BundleSource;
using detail::CapturedEntry;
using detail::FileIdentity;
using detail::ProjectBundleStorage;

struct ParsedManifest {
    Json root;
    ProjectIdentity project;
    ProjectRunSummary run;
    ContentHash authoring_digest;
    BuildId build_id;
    ContentHash bundle_digest;
    DependencyLock lock;
    ExportSelection exports;
    std::vector<ArtifactDescriptor> artifacts;
};

struct LibraryDecoded {
    std::map<std::string, std::unique_ptr<Circuit>> component_documents;
    std::map<std::string, std::unique_ptr<PartDefinition>> parts;
    std::map<std::string, FootprintDefinition> footprints;
    std::map<std::string, SymbolDefinition> symbols;
};

[[noreturn]] void fail(ProjectBundleOpenErrorCode code, std::string message);
void require(bool condition, ProjectBundleOpenErrorCode code, std::string message);

[[nodiscard]] std::string canonical(const Json &value);
[[nodiscard]] Json parse_json(std::string_view bytes, std::string_view label,
                              ProjectBundleOpenErrorCode code);
void require_key_set(const Json &object, std::initializer_list<std::string_view> expected,
                     std::string_view label);
[[nodiscard]] const Json &field(const Json &object, std::string_view key, std::string_view label);
[[nodiscard]] std::string string_field(const Json &object, std::string_view key,
                                       std::string_view label);

[[nodiscard]] bool is_export_artifact(const ArtifactDescriptor &artifact);
[[nodiscard]] ParsedManifest parse_manifest(std::string_view bytes);
void verify_build_and_bundle_digests(const ParsedManifest &manifest);

void verify_descriptors_and_capture(const detail::BundleSource &source,
                                    const ParsedManifest &manifest,
                                    detail::ProjectBundleStorage &storage,
                                    std::optional<detail::FileIdentity> manifest_identity);
[[nodiscard]] ArtifactIndex artifact_index(const detail::ProjectBundleStorage &storage);
[[nodiscard]] std::set<std::string> dependency_keys(const ArtifactDescriptor &artifact);
void verify_graph_references_and_dag(const detail::ProjectBundleStorage &storage,
                                     const ArtifactIndex &index);
[[nodiscard]] std::size_t unique_artifact(const detail::ProjectBundleStorage &storage,
                                          ArtifactKind kind);
void verify_dependency_lock(const detail::ProjectBundleStorage &storage, const DependencyLock &lock,
                            const ArtifactIndex &index);
void verify_reports(const detail::ProjectBundleStorage &storage, const LibraryDecoded &library,
                    std::string_view diagnostics, std::string_view tests,
                    const ProjectRunSummary &run);

void decode_library_artifacts(detail::ProjectBundleStorage &storage, LibraryDecoded &decoded);
void decode_project_models(detail::ProjectBundleStorage &storage);
void decode_compiled_and_scenes(detail::ProjectBundleStorage &storage);
void verify_owner_graph(detail::ProjectBundleStorage &storage, const LibraryDecoded &decoded,
                        const ArtifactIndex &index);
[[nodiscard]] std::string write_decoded_bom(const Circuit &circuit, const LibraryDecoded &library);
void verify_exports(detail::ProjectBundleStorage &storage, const LibraryDecoded &library,
                    const ExportSelection &selection);

[[nodiscard]] std::shared_ptr<const detail::ProjectBundleStorage>
open_verified(const detail::BundleSource &source, detail::CapturedEntry manifest_capture);

} // namespace volt::io::v2_open
