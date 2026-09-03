#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/compiled/board_consumers.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt::io::detail {

class ProjectBundleStorage final {
  public:
    struct V2Artifact {
        ArtifactDescriptor descriptor;
        std::string manifest_record_json;
        std::string bytes;
    };

    struct V2Circuit {
        DesignKey design;
        std::size_t artifact;
        std::unique_ptr<Circuit> model;
    };

    struct V2Schematic {
        DesignKey design;
        SchematicKey schematic;
        std::size_t artifact;
        std::size_t circuit;
        std::unique_ptr<Schematic> model;
    };

    struct V2Board {
        DesignKey design;
        BoardName board;
        std::size_t artifact;
        std::size_t circuit;
        std::unique_ptr<Board> model;
    };

    struct V2CompiledBoard {
        std::size_t artifact;
        std::unique_ptr<CompiledBoard> model;
    };

    struct V2BoardScene {
        std::size_t artifact;
        std::size_t compiled_board;
        std::unique_ptr<BoardScene> model;
    };

    ProjectBundleSchemaVersion schema = ProjectBundleSchemaVersion::V3;
    ProjectBundleStorageKind storage_kind = ProjectBundleStorageKind::Directory;
    std::string project_name;
    std::optional<std::string> project_version;
    std::optional<std::string> project_description;
    std::string manifest_bytes;
    std::optional<ProjectRunSummary> v2_run;
    std::optional<BuildId> v2_build_id;
    std::optional<ContentHash> v2_bundle_digest;
    std::optional<ContentHash> v2_authoring_inputs_digest;
    std::optional<DependencyLock> v2_dependency_lock;
    ExportSelection v2_export_selection;
    std::vector<V2Artifact> v2_artifacts;
    std::vector<V2Circuit> v2_circuits;
    std::vector<V2Schematic> v2_schematics;
    std::vector<V2Board> v2_boards;
    std::vector<V2CompiledBoard> v2_compiled_boards;
    std::vector<V2BoardScene> v2_board_scenes;
    std::size_t v2_diagnostics{};
    std::size_t v2_tests{};
    std::vector<std::size_t> v2_selected_exports;
};

} // namespace volt::io::detail
