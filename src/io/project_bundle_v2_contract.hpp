#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <volt/io/project_bundle_v2_writer.hpp>

namespace volt::io::detail {

inline constexpr auto project_bundle_v2_format = std::string_view{"volt.project_result"};
inline constexpr auto project_bundle_v2_producer = std::string_view{"volt.project-bundle"};
inline constexpr auto project_bundle_v2_library_producer =
    std::string_view{"volt.part-library-bundle"};
inline constexpr auto project_bundle_v2_producer_version = std::uint32_t{1};

struct ProjectBundleV2SchemaInfo {
    std::string format;
    std::uint32_t version;
    std::string media_type;
};

[[nodiscard]] std::string project_bundle_v2_artifact_id_json(const ArtifactId &id);
[[nodiscard]] std::string project_bundle_v2_artifact_ref_json(const ArtifactRef &reference);
[[nodiscard]] std::string project_bundle_v2_artifact_key(const ArtifactId &id);
[[nodiscard]] std::string project_bundle_v2_artifact_path(const ArtifactId &id);
[[nodiscard]] bool project_bundle_v2_safe_path(std::string_view path);
[[nodiscard]] ArtifactRole project_bundle_v2_role_for_kind(ArtifactKind kind);
[[nodiscard]] ProjectBundleV2SchemaInfo project_bundle_v2_schema_for_kind(ArtifactKind kind);
[[nodiscard]] std::string project_bundle_v2_producer_for_kind(ArtifactKind kind);
[[nodiscard]] std::string project_bundle_v2_export_request_json(const ExportRequest &request);

} // namespace volt::io::detail
