#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/pcb/board.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt::io::detail {

class ProjectBundleStorage final {
  public:
    struct Artifact {
        std::string kind;
        std::string name;
        std::string path;
        std::string media_type;
        std::map<std::string, std::string> group;
        std::optional<std::string> sha256;
        std::string manifest_record_json;
        std::string bytes;
    };

    struct CircuitDocument {
        std::string design;
        std::size_t artifact;
        std::unique_ptr<Circuit> model;
    };

    struct SchematicDocument {
        std::string design;
        std::string schematic;
        std::size_t artifact;
        std::size_t circuit;
        std::unique_ptr<volt::Schematic> model;
    };

    struct BoardDocument {
        std::string design;
        std::string board;
        std::size_t artifact;
        std::size_t circuit;
        std::unique_ptr<volt::Board> model;
    };

    ProjectBundleSchemaVersion schema = ProjectBundleSchemaVersion::V1;
    ProjectBundleStorageKind storage_kind = ProjectBundleStorageKind::Directory;
    std::string project_name;
    std::optional<std::string> project_version;
    std::optional<std::string> project_description;
    std::string manifest_bytes;
    std::vector<Artifact> artifacts;
    std::vector<CircuitDocument> circuits;
    std::vector<SchematicDocument> schematics;
    std::vector<BoardDocument> boards;
};

} // namespace volt::io::detail
