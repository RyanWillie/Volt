#include <volt/io/project_bundle_v2_writer.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
#include <system_error>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <volt/circuit/bom/bom.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/assembly/cpl_writer.hpp>
#include <volt/io/bom/bom_writer.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/pcb/board_scene.hpp>
#include <volt/io/pcb/compiled_board.hpp>
#include <volt/io/pcb/pcb_svg_writer.hpp>
#include <volt/io/pcb/pcb_writer.hpp>
#include <volt/io/schematic/schematic_svg_writer.hpp>
#include <volt/io/schematic/schematic_writer.hpp>

#include "project_bundle_v2_contract.hpp"
#include "project_bundle_v2_reports.hpp"

namespace volt::io {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::string_view format_name = "volt.project_result";
constexpr std::string_view producer_name = "volt.project-bundle";
constexpr std::uint32_t producer_version = 1;
constexpr std::string_view manifest_path = "manifest.volt.json";

[[noreturn]] void reject(std::string message, ErrorCode code = ErrorCode::CrossReferenceViolation) {
    throw KernelLogicError{code, "ProjectBundle v2: " + std::move(message)};
}

void require(bool condition, std::string message,
             ErrorCode code = ErrorCode::CrossReferenceViolation) {
    if (!condition) {
        reject(std::move(message), code);
    }
}

[[nodiscard]] std::string canonical(const Json &value) {
    return value.dump(-1, ' ', false, Json::error_handler_t::strict);
}

[[nodiscard]] Json artifact_ref_json(const ArtifactRef &reference);

[[nodiscard]] std::string hash_suffix(const ContentHash &hash) {
    return hash.value().substr(std::string_view{"sha256:"}.size(), 20U);
}

[[nodiscard]] std::string_view status_name(ProjectStatus status) {
    switch (status) {
    case ProjectStatus::Clean:
        return "clean";
    case ProjectStatus::ExpectedDiagnostics:
        return "expected-diagnostics";
    case ProjectStatus::Failed:
        return "failed";
    }
    reject("project status is unsupported", ErrorCode::InvalidArgument);
}

[[nodiscard]] std::string_view authoring_kind_name(AuthoringInputKind kind) {
    switch (kind) {
    case AuthoringInputKind::ProjectSource:
        return "project_source";
    case AuthoringInputKind::DeclaredInput:
        return "declared_input";
    }
    reject("authoring input kind is unsupported", ErrorCode::InvalidArgument);
}

[[nodiscard]] std::string_view attachment_kind_name(LibraryAttachmentKind kind) {
    switch (kind) {
    case LibraryAttachmentKind::Symbol:
        return "symbol";
    case LibraryAttachmentKind::Footprint:
        return "footprint";
    }
    reject("library attachment kind is unsupported", ErrorCode::InvalidArgument);
}

[[nodiscard]] std::string_view asset_kind_name(LibraryAssetKind kind) {
    switch (kind) {
    case LibraryAssetKind::Glb:
        return "glb";
    case LibraryAssetKind::Step:
        return "step";
    }
    reject("library asset kind is unsupported", ErrorCode::InvalidArgument);
}

[[nodiscard]] std::string_view export_kind_name(ExportKind kind) {
    switch (kind) {
    case ExportKind::SchematicSvg:
        return "schematic_svg";
    case ExportKind::BoardSvg:
        return "board_svg";
    case ExportKind::BoardLayerImage:
        return "board_layer_image";
    case ExportKind::KicadPcb:
        return "kicad_pcb";
    case ExportKind::Bom:
        return "bom";
    case ExportKind::Cpl:
        return "cpl";
    case ExportKind::Fabrication:
        return "fabrication";
    case ExportKind::Step:
        return "step";
    case ExportKind::WholeBoardGlb:
        return "whole_board_glb";
    }
    reject("export kind is unsupported", ErrorCode::InvalidArgument);
}

[[nodiscard]] Json export_target_json(const ExportTarget &target) {
    return std::visit(
        [](const auto &value) -> Json {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<T, ModelExportTarget>) {
                return Json{{"type", "model_export_target"},
                            {"value", Json{{"model", artifact_ref_json(value.model)}}}};
            } else if constexpr (std::same_as<T, BoardLayerExportTarget>) {
                return Json{
                    {"type", "board_layer_export_target"},
                    {"value", Json{{"compiled_board", artifact_ref_json(value.compiled_board)},
                                   {"layer", value.layer.value()}}}};
            } else {
                return Json{{"type", "library_asset_export_target"},
                            {"value",
                             Json{{"selected_part", artifact_ref_json(value.selected_part)},
                                  {"asset",
                                   Json{{"library_namespace", value.asset.library_namespace},
                                        {"library_version", value.asset.library_version},
                                        {"media_kind", asset_kind_name(value.asset.kind)},
                                        {"library_bundle_digest",
                                         value.asset.library_bundle_digest.value()},
                                        {"content_digest", value.asset.content_digest.value()}}}}}};
            }
        },
        target);
}

[[nodiscard]] std::string_view export_parameter_name(const ExportParameters &parameters) {
    return std::visit(
        [](const auto &value) -> std::string_view {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<T, NoExportParameters>) {
                return "no_export_parameters";
            } else if constexpr (std::same_as<T, SchematicSvgParameters>) {
                return "schematic_svg_parameters";
            } else if constexpr (std::same_as<T, BoardSvgParameters>) {
                return "board_svg_parameters";
            } else if constexpr (std::same_as<T, BoardLayerImageParameters>) {
                return "board_layer_image_parameters";
            } else if constexpr (std::same_as<T, KicadPcbParameters>) {
                return "kicad_pcb_parameters";
            } else if constexpr (std::same_as<T, BomParameters>) {
                return "bom_parameters";
            } else if constexpr (std::same_as<T, CplParameters>) {
                return "cpl_parameters";
            } else if constexpr (std::same_as<T, FabricationParameters>) {
                return "fabrication_parameters";
            } else if constexpr (std::same_as<T, StepParameters>) {
                return "step_parameters";
            } else {
                return "whole_board_glb_parameters";
            }
        },
        parameters);
}

[[nodiscard]] Json export_request_json(const ExportRequest &request) {
    return Json{
        {"kind", export_kind_name(request.kind)},
        {"target", export_target_json(request.target)},
        {"request_schema",
         Json{{"format", request.request_schema.format()},
              {"version", static_cast<std::uint32_t>(request.request_schema.version())}}},
        {"parameters",
         Json{{"type", export_parameter_name(request.parameters)}, {"value", Json::object()}}},
    };
}

[[nodiscard]] Json library_part_ref_json(const LibraryPartRef &reference) {
    return Json{{"library_namespace", reference.library_namespace()},
                {"library_version", reference.library_version()},
                {"part_key", reference.part_key().value()},
                {"library_bundle_digest", reference.library_digest().value()},
                {"part_digest", reference.part_digest().value()}};
}

[[nodiscard]] Json compiled_identity_json(const CompiledBoardIdentity &identity) {
    return Json{{"board", identity.board().value()},
                {"provenance_digest", identity.provenance_digest().value()}};
}

[[nodiscard]] Json owner_json(const ArtifactOwnerIdentity &owner) {
    return std::visit(
        [](const auto &value) -> Json {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<T, LogicalArtifactIdentity>) {
                return Json{{"type", "logical_artifact_identity"},
                            {"value", Json{{"design", value.design.value()}}}};
            } else if constexpr (std::same_as<T, SchematicArtifactIdentity>) {
                return Json{{"type", "schematic_artifact_identity"},
                            {"value", Json{{"design", value.design.value()},
                                           {"schematic", value.schematic.value()}}}};
            } else if constexpr (std::same_as<T, BoardArtifactIdentity>) {
                return Json{{"type", "board_artifact_identity"},
                            {"value", Json{{"design", value.design.value()},
                                           {"board", value.board.value()}}}};
            } else if constexpr (std::same_as<T, CompiledBoardIdentity>) {
                return Json{{"type", "compiled_board_identity"},
                            {"value", compiled_identity_json(value)}};
            } else if constexpr (std::same_as<T, BoardSceneArtifactIdentity>) {
                return Json{{"type", "board_scene_artifact_identity"},
                            {"value", Json{{"compiled_board",
                                            compiled_identity_json(value.compiled_board)}}}};
            } else if constexpr (std::same_as<T, ProjectSingletonIdentity>) {
                return Json{{"type", "project_singleton_identity"},
                            {"value", Json{{"key", "primary"}}}};
            } else if constexpr (std::same_as<T, LibraryComponentRef>) {
                return Json{
                    {"type", "library_component_ref"},
                    {"value", Json{{"library_namespace", value.library_namespace},
                                   {"library_version", value.library_version},
                                   {"component_key", value.component_key.value()},
                                   {"library_bundle_digest", value.library_bundle_digest.value()},
                                   {"component_digest", value.component_digest.value()}}}};
            } else if constexpr (std::same_as<T, LibraryPartRef>) {
                return Json{{"type", "library_part_ref"}, {"value", library_part_ref_json(value)}};
            } else if constexpr (std::same_as<T, LibraryAttachmentRef>) {
                return Json{
                    {"type", "library_attachment_ref"},
                    {"value", Json{{"library_namespace", value.library_namespace},
                                   {"library_version", value.library_version},
                                   {"attachment_kind", attachment_kind_name(value.kind)},
                                   {"key", value.key},
                                   {"library_bundle_digest", value.library_bundle_digest.value()},
                                   {"content_digest", value.content_digest.value()}}}};
            } else if constexpr (std::same_as<T, LibraryAssetRef>) {
                return Json{
                    {"type", "library_asset_ref"},
                    {"value", Json{{"library_namespace", value.library_namespace},
                                   {"library_version", value.library_version},
                                   {"media_kind", asset_kind_name(value.kind)},
                                   {"library_bundle_digest", value.library_bundle_digest.value()},
                                   {"content_digest", value.content_digest.value()}}}};
            } else {
                auto output_key = std::visit(
                    [](const auto &key) -> Json {
                        using Key = std::decay_t<decltype(key)>;
                        if constexpr (std::same_as<Key, PrimaryExportOutputKey>) {
                            return Json{{"type", "primary_export_output_key"},
                                        {"value", "primary"}};
                        } else if constexpr (std::same_as<Key, BoardLayerKey>) {
                            return Json{{"type", "board_layer_key"}, {"value", key.value()}};
                        } else {
                            return Json{
                                {"type", "library_asset_ref"},
                                {"value",
                                 Json{{"library_namespace", key.library_namespace},
                                      {"library_version", key.library_version},
                                      {"media_kind", asset_kind_name(key.kind)},
                                      {"library_bundle_digest", key.library_bundle_digest.value()},
                                      {"content_digest", key.content_digest.value()}}}};
                        }
                    },
                    value.output_key);
                return Json{{"type", "export_artifact_identity"},
                            {"value", Json{{"request_digest", value.request_digest.value()},
                                           {"output_key", std::move(output_key)}}}};
            }
        },
        owner);
}

[[nodiscard]] Json artifact_id_json(const ArtifactId &id) {
    return Json{{"kind", artifact_kind_name(id.kind())}, {"owner", owner_json(id.owner())}};
}

[[nodiscard]] Json artifact_ref_json(const ArtifactRef &reference) {
    return Json{{"artifact", artifact_id_json(reference.artifact())},
                {"content_digest", reference.content_digest().value()}};
}

[[nodiscard]] std::string artifact_key(const ArtifactId &id) {
    return canonical(artifact_id_json(id));
}

[[nodiscard]] std::string artifact_ref_key(const ArtifactRef &reference) {
    return artifact_key(reference.artifact()) + '\n' + reference.content_digest().value();
}

[[nodiscard]] bool safe_logical_name(std::string_view value) {
    if (value.empty() || value.front() == '/' || value.find('\\') != value.npos ||
        value.find(':') != value.npos || value.starts_with("//")) {
        return false;
    }
    auto cursor = std::size_t{0};
    while (cursor < value.size()) {
        const auto separator = value.find('/', cursor);
        const auto end = separator == value.npos ? value.size() : separator;
        const auto segment = value.substr(cursor, end - cursor);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (std::ranges::any_of(segment, [](unsigned char ch) { return ch < 0x20U; })) {
            return false;
        }
        cursor = end + 1U;
    }
    return true;
}

[[nodiscard]] bool safe_bundle_path(std::string_view value) {
    if (value.empty() || value.front() == '/' || value.find('\\') != value.npos ||
        value.find(':') != value.npos) {
        return false;
    }
    auto cursor = std::size_t{0};
    while (cursor < value.size()) {
        const auto separator = value.find('/', cursor);
        const auto end = separator == value.npos ? value.size() : separator;
        const auto segment = value.substr(cursor, end - cursor);
        if (segment.empty() || segment == "." || segment == ".." || segment.back() == '.') {
            return false;
        }
        const auto is_ascii_alnum = [](unsigned char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
        };
        const auto first = static_cast<unsigned char>(segment.front());
        if (!(is_ascii_alnum(first) || first == '_')) {
            return false;
        }
        for (const auto raw : segment) {
            const auto ch = static_cast<unsigned char>(raw);
            if (!(is_ascii_alnum(ch) || ch == '_' || ch == '.' || ch == '-')) {
                return false;
            }
        }
        auto basename = std::string{segment.substr(0, segment.find('.'))};
        std::ranges::transform(basename, basename.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        const auto reserved = basename == "CON" || basename == "PRN" || basename == "AUX" ||
                              basename == "NUL" ||
                              (basename.size() == 4U &&
                               (basename.starts_with("COM") || basename.starts_with("LPT")) &&
                               basename.back() >= '1' && basename.back() <= '9');
        if (reserved) {
            return false;
        }
        cursor = end + 1U;
    }
    return true;
}

[[nodiscard]] std::string extension_for(ArtifactKind kind) {
    switch (kind) {
    case ArtifactKind::LogicalModel:
    case ArtifactKind::SchematicModel:
    case ArtifactKind::BoardModel:
    case ArtifactKind::ComponentDefinition:
    case ArtifactKind::PartDefinition:
    case ArtifactKind::FootprintDefinition:
    case ArtifactKind::BoardScene:
    case ArtifactKind::Diagnostics:
    case ArtifactKind::ProjectTests:
    case ArtifactKind::Bom:
    case ArtifactKind::Cpl:
        return ".json";
    case ArtifactKind::CompiledBoard:
        return ".voltcb";
    case ArtifactKind::SymbolDefinition:
        return ".symbol";
    case ArtifactKind::GlbAsset:
    case ArtifactKind::WholeBoardGlb:
        return ".glb";
    case ArtifactKind::StepAsset:
        return ".step";
    case ArtifactKind::SchematicSvg:
    case ArtifactKind::BoardSvg:
    case ArtifactKind::BoardLayerImage:
        return ".svg";
    case ArtifactKind::KicadPcb:
        return ".kicad_pcb";
    case ArtifactKind::FabricationPackage:
        return ".zip";
    }
    reject("artifact extension is unsupported");
}

[[nodiscard]] std::string artifact_path(const ArtifactId &id) {
    const auto digest = sha256_content_hash(canonical(artifact_id_json(id)));
    return "artifacts/" + std::string{artifact_kind_name(id.kind())} + "/" + hash_suffix(digest) +
           extension_for(id.kind());
}

[[nodiscard]] ArtifactRole role_for_kind(ArtifactKind kind) {
    switch (kind) {
    case ArtifactKind::LogicalModel:
    case ArtifactKind::SchematicModel:
    case ArtifactKind::BoardModel:
    case ArtifactKind::CompiledBoard:
    case ArtifactKind::ComponentDefinition:
    case ArtifactKind::PartDefinition:
    case ArtifactKind::SymbolDefinition:
    case ArtifactKind::FootprintDefinition:
        return ArtifactRole::Model;
    case ArtifactKind::BoardScene:
        return ArtifactRole::View;
    case ArtifactKind::SchematicSvg:
    case ArtifactKind::BoardSvg:
    case ArtifactKind::BoardLayerImage:
    case ArtifactKind::WholeBoardGlb:
        return ArtifactRole::Render;
    case ArtifactKind::Diagnostics:
    case ArtifactKind::ProjectTests:
    case ArtifactKind::Bom:
    case ArtifactKind::Cpl:
        return ArtifactRole::Report;
    case ArtifactKind::KicadPcb:
        return ArtifactRole::Adapter;
    case ArtifactKind::GlbAsset:
    case ArtifactKind::StepAsset:
        return ArtifactRole::Asset;
    case ArtifactKind::FabricationPackage:
        return ArtifactRole::Delivery;
    }
    reject("artifact role mapping is unsupported");
}

struct SchemaInfo {
    std::string format;
    std::uint32_t version;
    std::string media_type;
};

[[nodiscard]] SchemaInfo schema_for_kind(ArtifactKind kind) {
    switch (kind) {
    case ArtifactKind::LogicalModel:
        return {std::string{logical_circuit_format_name()},
                static_cast<std::uint32_t>(logical_circuit_format_version()),
                "application/vnd.volt.logical+json"};
    case ArtifactKind::SchematicModel:
        return {"volt.schematic", 1, "application/vnd.volt.schematic+json"};
    case ArtifactKind::BoardModel:
        return {"volt.pcb", 3, "application/vnd.volt.pcb+json"};
    case ArtifactKind::CompiledBoard:
        return {"volt.compiled-board", 1, "application/vnd.volt.compiled-board"};
    case ArtifactKind::ComponentDefinition:
        return {std::string{logical_circuit_format_name()},
                static_cast<std::uint32_t>(logical_circuit_format_version()),
                "application/vnd.volt.logical+json"};
    case ArtifactKind::PartDefinition:
        return {"volt.part-definition", 5, "application/vnd.volt.part+json"};
    case ArtifactKind::SymbolDefinition:
        return {std::string{symbol_definition_format_name()},
                static_cast<std::uint32_t>(symbol_definition_format_version()),
                "application/vnd.volt.symbol+json"};
    case ArtifactKind::FootprintDefinition:
        return {"volt.footprint", 1, "application/vnd.volt.footprint+json"};
    case ArtifactKind::BoardScene:
        return {"volt.board-scene", 1, "application/vnd.volt.board-scene+json"};
    case ArtifactKind::GlbAsset:
    case ArtifactKind::WholeBoardGlb:
        return {"glb", 2, "model/gltf-binary"};
    case ArtifactKind::Diagnostics:
        return {"volt.project-diagnostics", 1, "application/json"};
    case ArtifactKind::ProjectTests:
        return {"volt.project-tests", 1, "application/json"};
    case ArtifactKind::SchematicSvg:
    case ArtifactKind::BoardSvg:
    case ArtifactKind::BoardLayerImage:
        return {"svg", 1, "image/svg+xml"};
    case ArtifactKind::KicadPcb:
        return {"kicad_pcb", 20240108, "application/x-kicad-pcb"};
    case ArtifactKind::Bom:
        return {"volt.bom", 1, "application/vnd.volt.bom+json"};
    case ArtifactKind::Cpl:
        return {"volt.cpl", 1, "application/vnd.volt.cpl+json"};
    case ArtifactKind::FabricationPackage:
        return {"volt.fabrication-package", 1, "application/zip"};
    case ArtifactKind::StepAsset:
        return {"step", 1, "model/step"};
    }
    reject("artifact schema mapping is unsupported");
}

[[nodiscard]] std::string library_producer_name(ArtifactKind kind) {
    switch (kind) {
    case ArtifactKind::ComponentDefinition:
    case ArtifactKind::PartDefinition:
    case ArtifactKind::SymbolDefinition:
    case ArtifactKind::FootprintDefinition:
    case ArtifactKind::GlbAsset:
    case ArtifactKind::StepAsset:
        return "volt.part-library-bundle";
    default:
        return std::string{producer_name};
    }
}

struct DraftArtifact {
    ArtifactId id;
    std::string bytes;
    std::vector<ArtifactRef> dependencies;
    ContentHash producer_build;
    bool required_default;
};

struct LogicalInput {
    DesignKey design;
    const Circuit *circuit;
    const PartLibraryBundle *bundle;
};

struct SchematicInput {
    DesignKey design;
    SchematicKey key;
    const Schematic *schematic;
};

struct BoardInput {
    DesignKey design;
    const Board *board;
    const CompiledBoard *compiled;
    const BoardScene *scene;
    const PartLibraryBundle *bundle;
};

[[nodiscard]] PartAssetReference footprint_reference(const PartDefinition &part) {
    const auto &footprint = part.orderable_part().footprint();
    return PartAssetReference{PartAssetKind::Footprint,
                              "footprint:" + footprint.footprint().library() + "/" +
                                  footprint.footprint().name(),
                              footprint.hash()};
}

[[nodiscard]] std::optional<PartAssetReference> model_reference(const PartDefinition &part) {
    if (!part.orderable_part().model_3d().has_value()) {
        return std::nullopt;
    }
    const auto &model = *part.orderable_part().model_3d();
    return PartAssetReference{PartAssetKind::Model3D,
                              "model:" + model.format() + "/" + model.file_name(), model.hash()};
}

[[nodiscard]] LibraryComponentRef component_ref(const PartLibraryBundle &bundle,
                                                const ComponentDefinition &component) {
    return LibraryComponentRef{bundle.identity().namespace_name(), bundle.identity().version(),
                               component.contract().key(), bundle.library_digest(),
                               component.content_identity()};
}

[[nodiscard]] LibraryAttachmentRef attachment_ref(const PartLibraryBundle &bundle,
                                                  const PartAssetReference &reference,
                                                  LibraryAttachmentKind kind) {
    return LibraryAttachmentRef{bundle.identity().namespace_name(),
                                bundle.identity().version(),
                                kind,
                                reference.key(),
                                bundle.library_digest(),
                                reference.digest()};
}

[[nodiscard]] LibraryAssetRef library_asset_ref(const PartLibraryBundle &bundle,
                                                const PartAssetReference &reference,
                                                LibraryAssetKind kind) {
    return LibraryAssetRef{bundle.identity().namespace_name(), bundle.identity().version(), kind,
                           bundle.library_digest(), reference.digest()};
}

[[nodiscard]] ArtifactRef ref_for(const DraftArtifact &artifact) {
    return ArtifactRef{artifact.id, sha256_content_hash(artifact.bytes)};
}

void sort_unique_dependencies(std::vector<ArtifactRef> &dependencies) {
    std::ranges::sort(dependencies, {}, artifact_ref_key);
    require(std::ranges::adjacent_find(dependencies, {}, artifact_ref_key) == dependencies.end(),
            "artifact contains a duplicate direct dependency", ErrorCode::DuplicateName);
}

void normalize_dependencies(std::vector<ArtifactRef> &dependencies) {
    std::ranges::sort(dependencies, {}, artifact_ref_key);
    dependencies.erase(std::ranges::unique(dependencies, {}, artifact_ref_key).begin(),
                       dependencies.end());
}

[[nodiscard]] Json descriptor_json(const ArtifactDescriptor &descriptor,
                                   std::string_view producer_name_value,
                                   std::uint32_t producer_version_value,
                                   const ContentHash &producer_build) {
    auto dependencies = Json::array();
    for (const auto &dependency : descriptor.dependencies()) {
        dependencies.push_back(artifact_ref_json(dependency));
    }
    return Json{
        {"id", artifact_id_json(descriptor.id())},
        {"role", artifact_role_name(descriptor.role())},
        {"kind", artifact_kind_name(descriptor.kind())},
        {"schema",
         Json{{"format", descriptor.schema_format()}, {"version", descriptor.schema_version()}}},
        {"media_type", descriptor.media_type()},
        {"path", descriptor.path().value()},
        {"content_digest", descriptor.content_digest().value()},
        {"depends_on", std::move(dependencies)},
        {"producer", Json{{"name", producer_name_value},
                          {"version", producer_version_value},
                          {"build", producer_build.value()}}},
    };
}

[[nodiscard]] Json dependency_lock_json(const DependencyLock &lock) {
    auto libraries = Json::array();
    for (const auto &library : lock.libraries()) {
        libraries.push_back(Json{{"library", library.library_namespace},
                                 {"version", library.library_version},
                                 {"library_bundle_digest", library.library_bundle_digest.value()}});
    }
    auto selected_parts = Json::array();
    for (const auto &selection : lock.selected_parts()) {
        selected_parts.push_back(
            Json{{"selected_part", library_part_ref_json(selection.selected_part)},
                 {"vendored_part", artifact_ref_json(selection.vendored_part)}});
    }
    return Json{{"libraries", std::move(libraries)}, {"selected_parts", std::move(selected_parts)}};
}

void little_u32(std::string &out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xffU));
    out.push_back(static_cast<char>((value >> 8U) & 0xffU));
    out.push_back(static_cast<char>((value >> 16U) & 0xffU));
    out.push_back(static_cast<char>((value >> 24U) & 0xffU));
}

void little_u16(std::string &out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xffU));
    out.push_back(static_cast<char>((value >> 8U) & 0xffU));
}

[[nodiscard]] std::string zip_bytes(const std::map<std::string, std::string> &files) {
    struct Central {
        std::string path;
        std::uint32_t crc;
        std::uint32_t size;
        std::uint32_t offset;
    };

    auto output = std::string{};
    auto central = std::vector<Central>{};
    for (const auto &[path, bytes] : files) {
        require(path.size() <= 0xffffU && bytes.size() <= 0xffffffffULL,
                "archive entry exceeds deterministic ZIP32 limits", ErrorCode::InvalidArgument);
        require(output.size() <= std::numeric_limits<std::uint32_t>::max(),
                "archive exceeds deterministic ZIP32 limits", ErrorCode::InvalidArgument);
        const auto crc = static_cast<std::uint32_t>(crc32(
            0L, reinterpret_cast<const Bytef *>(bytes.data()), static_cast<uInt>(bytes.size())));
        const auto offset = static_cast<std::uint32_t>(output.size());
        little_u32(output, 0x04034b50U);
        little_u16(output, 20U);
        little_u16(output, 0x0800U);
        little_u16(output, 0U);
        little_u16(output, 0U);
        little_u16(output, 0x0021U);
        little_u32(output, crc);
        little_u32(output, static_cast<std::uint32_t>(bytes.size()));
        little_u32(output, static_cast<std::uint32_t>(bytes.size()));
        little_u16(output, static_cast<std::uint16_t>(path.size()));
        little_u16(output, 0U);
        output += path;
        output += bytes;
        central.push_back(Central{path, crc, static_cast<std::uint32_t>(bytes.size()), offset});
    }
    require(output.size() <= std::numeric_limits<std::uint32_t>::max(),
            "archive exceeds deterministic ZIP32 limits", ErrorCode::InvalidArgument);
    const auto central_offset = static_cast<std::uint32_t>(output.size());
    for (const auto &entry : central) {
        little_u32(output, 0x02014b50U);
        little_u16(output, 0x033fU);
        little_u16(output, 20U);
        little_u16(output, 0x0800U);
        little_u16(output, 0U);
        little_u16(output, 0U);
        little_u16(output, 0x0021U);
        little_u32(output, entry.crc);
        little_u32(output, entry.size);
        little_u32(output, entry.size);
        little_u16(output, static_cast<std::uint16_t>(entry.path.size()));
        little_u16(output, 0U);
        little_u16(output, 0U);
        little_u16(output, 0U);
        little_u16(output, 0U);
        little_u32(output, 0100644U << 16U);
        little_u32(output, entry.offset);
        output += entry.path;
    }
    require(output.size() <= std::numeric_limits<std::uint32_t>::max(),
            "archive central directory exceeds deterministic ZIP32 limits",
            ErrorCode::InvalidArgument);
    const auto central_size = static_cast<std::uint32_t>(output.size()) - central_offset;
    require(central.size() <= 0xffffU, "archive has too many entries", ErrorCode::InvalidArgument);
    little_u32(output, 0x06054b50U);
    little_u16(output, 0U);
    little_u16(output, 0U);
    little_u16(output, static_cast<std::uint16_t>(central.size()));
    little_u16(output, static_cast<std::uint16_t>(central.size()));
    little_u32(output, central_size);
    little_u32(output, central_offset);
    little_u16(output, 0U);
    return output;
}

[[nodiscard]] std::filesystem::path staging_path(const std::filesystem::path &destination) {
    static std::atomic_uint64_t sequence{0};
    const auto suffix = sequence.fetch_add(1U, std::memory_order_relaxed);
    return destination.parent_path() /
           (destination.filename().string() + ".volt-tmp-" + std::to_string(suffix));
}

void write_file(const std::filesystem::path &path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    auto out = std::ofstream{path, std::ios::binary | std::ios::trunc};
    require(out.is_open(), "could not create staging file", ErrorCode::InvalidState);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(out.good(), "could not write complete staging file", ErrorCode::InvalidState);
    out.close();
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
            std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace);
}

} // namespace

std::string_view artifact_role_name(ArtifactRole role) {
    switch (role) {
    case ArtifactRole::Model:
        return "model";
    case ArtifactRole::View:
        return "view";
    case ArtifactRole::Render:
        return "render";
    case ArtifactRole::Report:
        return "report";
    case ArtifactRole::Adapter:
        return "adapter";
    case ArtifactRole::Asset:
        return "asset";
    case ArtifactRole::Delivery:
        return "delivery";
    }
    reject("artifact role is unsupported", ErrorCode::InvalidArgument);
}

std::string_view artifact_kind_name(ArtifactKind kind) {
    switch (kind) {
    case ArtifactKind::LogicalModel:
        return "logical_model";
    case ArtifactKind::SchematicModel:
        return "schematic_model";
    case ArtifactKind::BoardModel:
        return "board_model";
    case ArtifactKind::CompiledBoard:
        return "compiled_board";
    case ArtifactKind::ComponentDefinition:
        return "component_definition";
    case ArtifactKind::PartDefinition:
        return "part_definition";
    case ArtifactKind::SymbolDefinition:
        return "symbol_definition";
    case ArtifactKind::FootprintDefinition:
        return "footprint_definition";
    case ArtifactKind::BoardScene:
        return "board_scene";
    case ArtifactKind::GlbAsset:
        return "glb_asset";
    case ArtifactKind::Diagnostics:
        return "diagnostics";
    case ArtifactKind::ProjectTests:
        return "project_tests";
    case ArtifactKind::SchematicSvg:
        return "schematic_svg";
    case ArtifactKind::BoardSvg:
        return "board_svg";
    case ArtifactKind::BoardLayerImage:
        return "board_layer_image";
    case ArtifactKind::KicadPcb:
        return "kicad_pcb";
    case ArtifactKind::Bom:
        return "bom";
    case ArtifactKind::Cpl:
        return "cpl";
    case ArtifactKind::FabricationPackage:
        return "fabrication_package";
    case ArtifactKind::StepAsset:
        return "step_asset";
    case ArtifactKind::WholeBoardGlb:
        return "whole_board_glb";
    }
    reject("artifact kind is unsupported", ErrorCode::InvalidArgument);
}

DesignKey::DesignKey(std::string value) : value_{std::move(value)} {
    require(!value_.empty(), "DesignKey must not be empty", ErrorCode::InvalidArgument);
}

SchematicKey::SchematicKey(std::string value) : value_{std::move(value)} {
    require(!value_.empty(), "SchematicKey must not be empty", ErrorCode::InvalidArgument);
}

BoardLayerKey::BoardLayerKey(std::string value) : value_{std::move(value)} {
    require(!value_.empty(), "BoardLayerKey must not be empty", ErrorCode::InvalidArgument);
}

LogicalInputName::LogicalInputName(std::string value) : value_{std::move(value)} {
    require(safe_logical_name(value_), "logical input name is unsafe", ErrorCode::InvalidArgument);
}

AuthoringInput::AuthoringInput(AuthoringInputKind kind, LogicalInputName name, std::string bytes)
    : kind_{kind}, name_{std::move(name)}, bytes_{std::move(bytes)} {}

ArtifactId::ArtifactId(ArtifactKind kind, ArtifactOwnerIdentity owner)
    : kind_{kind}, owner_{std::move(owner)} {
    const auto owner_matches = std::visit(
        [&](const auto &value) {
            using Owner = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<Owner, LogicalArtifactIdentity>) {
                return kind_ == ArtifactKind::LogicalModel;
            } else if constexpr (std::same_as<Owner, SchematicArtifactIdentity>) {
                return kind_ == ArtifactKind::SchematicModel;
            } else if constexpr (std::same_as<Owner, BoardArtifactIdentity>) {
                return kind_ == ArtifactKind::BoardModel;
            } else if constexpr (std::same_as<Owner, CompiledBoardIdentity>) {
                return kind_ == ArtifactKind::CompiledBoard;
            } else if constexpr (std::same_as<Owner, BoardSceneArtifactIdentity>) {
                return kind_ == ArtifactKind::BoardScene;
            } else if constexpr (std::same_as<Owner, ProjectSingletonIdentity>) {
                return value.key == ProjectSingletonKey::Primary &&
                       (kind_ == ArtifactKind::Diagnostics || kind_ == ArtifactKind::ProjectTests);
            } else if constexpr (std::same_as<Owner, LibraryComponentRef>) {
                return kind_ == ArtifactKind::ComponentDefinition &&
                       !value.library_namespace.empty() && !value.library_version.empty();
            } else if constexpr (std::same_as<Owner, LibraryPartRef>) {
                return kind_ == ArtifactKind::PartDefinition;
            } else if constexpr (std::same_as<Owner, LibraryAttachmentRef>) {
                return !value.library_namespace.empty() && !value.library_version.empty() &&
                       !value.key.empty() &&
                       ((kind_ == ArtifactKind::SymbolDefinition &&
                         value.kind == LibraryAttachmentKind::Symbol) ||
                        (kind_ == ArtifactKind::FootprintDefinition &&
                         value.kind == LibraryAttachmentKind::Footprint));
            } else if constexpr (std::same_as<Owner, LibraryAssetRef>) {
                return kind_ == ArtifactKind::GlbAsset && value.kind == LibraryAssetKind::Glb &&
                       !value.library_namespace.empty() && !value.library_version.empty();
            } else {
                if (kind_ == ArtifactKind::BoardLayerImage) {
                    return std::holds_alternative<BoardLayerKey>(value.output_key);
                }
                if (kind_ == ArtifactKind::StepAsset) {
                    return std::holds_alternative<LibraryAssetRef>(value.output_key) &&
                           std::get<LibraryAssetRef>(value.output_key).kind ==
                               LibraryAssetKind::Step &&
                           !std::get<LibraryAssetRef>(value.output_key).library_namespace.empty() &&
                           !std::get<LibraryAssetRef>(value.output_key).library_version.empty();
                }
                return (kind_ == ArtifactKind::SchematicSvg || kind_ == ArtifactKind::BoardSvg ||
                        kind_ == ArtifactKind::KicadPcb || kind_ == ArtifactKind::Bom ||
                        kind_ == ArtifactKind::Cpl || kind_ == ArtifactKind::FabricationPackage ||
                        kind_ == ArtifactKind::WholeBoardGlb) &&
                       std::holds_alternative<PrimaryExportOutputKey>(value.output_key) &&
                       std::get<PrimaryExportOutputKey>(value.output_key) ==
                           PrimaryExportOutputKey::Primary;
            }
        },
        owner_);
    require(owner_matches, "ArtifactKind has an incompatible owner identity",
            ErrorCode::InvalidArgument);
}

ArtifactRef::ArtifactRef(ArtifactId artifact, ContentHash content_digest)
    : artifact_{std::move(artifact)}, content_digest_{std::move(content_digest)} {}

RelativeBundlePath::RelativeBundlePath(std::string value) : value_{std::move(value)} {
    require(safe_bundle_path(value_), "relative bundle path is unsafe", ErrorCode::InvalidArgument);
}

ArtifactDescriptor::ArtifactDescriptor(ArtifactId id, ArtifactRole role, ArtifactKind kind,
                                       std::string schema_format, std::uint32_t schema_version,
                                       std::string media_type, RelativeBundlePath path,
                                       ContentHash content_digest,
                                       std::vector<ArtifactRef> dependencies,
                                       std::string producer_name_value,
                                       std::uint32_t producer_version_value,
                                       ContentHash producer_build)
    : id_{std::move(id)}, role_{role}, kind_{kind}, schema_format_{std::move(schema_format)},
      schema_version_{schema_version}, media_type_{std::move(media_type)}, path_{std::move(path)},
      content_digest_{std::move(content_digest)}, dependencies_{std::move(dependencies)},
      producer_name_{std::move(producer_name_value)}, producer_version_{producer_version_value},
      producer_build_{std::move(producer_build)} {
    require(id_.kind() == kind_, "descriptor ID kind does not match descriptor kind");
    require(role_for_kind(kind_) == role_, "descriptor role does not match its closed kind");
    const auto schema = schema_for_kind(kind_);
    require(schema_format_ == schema.format && schema_version_ == schema.version &&
                media_type_ == schema.media_type,
            "descriptor schema or media type does not match its closed kind",
            ErrorCode::InvalidArgument);
    auto folded_path = path_.value();
    std::ranges::transform(folded_path, folded_path.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    require(folded_path != manifest_path, "descriptor path aliases the reserved manifest path",
            ErrorCode::InvalidArgument);
    require(!producer_name_.empty() && producer_version_ != 0U,
            "descriptor producer identity is incomplete", ErrorCode::InvalidArgument);
    sort_unique_dependencies(dependencies_);
}

BuildId::BuildId(ContentHash value) : value_{std::move(value)} {}

DependencyLock::DependencyLock(std::vector<LockedLibrary> libraries,
                               std::vector<LockedPartSelection> selected_parts)
    : libraries_{std::move(libraries)}, selected_parts_{std::move(selected_parts)} {
    for (const auto &library : libraries_) {
        require(!library.library_namespace.empty() && !library.library_version.empty(),
                "locked library identity is incomplete", ErrorCode::InvalidArgument);
    }
    std::ranges::sort(libraries_, [](const auto &lhs, const auto &rhs) {
        return std::tuple{lhs.library_namespace, lhs.library_version,
                          lhs.library_bundle_digest.value()} <
               std::tuple{rhs.library_namespace, rhs.library_version,
                          rhs.library_bundle_digest.value()};
    });
    require(std::ranges::adjacent_find(libraries_) == libraries_.end(),
            "dependency lock contains a duplicate library row", ErrorCode::DuplicateName);
    require(std::ranges::adjacent_find(libraries_,
                                       [](const auto &lhs, const auto &rhs) {
                                           return lhs.library_namespace == rhs.library_namespace &&
                                                  lhs.library_version == rhs.library_version;
                                       }) == libraries_.end(),
            "dependency lock has conflicting origins for one library release");

    for (const auto &selection : selected_parts_) {
        require(selection.vendored_part.artifact().kind() == ArtifactKind::PartDefinition &&
                    std::holds_alternative<LibraryPartRef>(
                        selection.vendored_part.artifact().owner()) &&
                    std::get<LibraryPartRef>(selection.vendored_part.artifact().owner()) ==
                        selection.selected_part,
                "locked selected part does not match its vendored artifact");
    }
    std::ranges::sort(selected_parts_, [](const auto &lhs, const auto &rhs) {
        return canonical(library_part_ref_json(lhs.selected_part)) <
               canonical(library_part_ref_json(rhs.selected_part));
    });
    require(std::ranges::adjacent_find(selected_parts_,
                                       [](const auto &lhs, const auto &rhs) {
                                           return lhs.selected_part == rhs.selected_part;
                                       }) == selected_parts_.end(),
            "dependency lock contains a duplicate selected-part binding", ErrorCode::DuplicateName);
}

ExportRequestSchema::ExportRequestSchema(ExportRequestSchemaVersion version) : version_{version} {
    require(version_ == ExportRequestSchemaVersion::V1,
            "export request schema version is unsupported", ErrorCode::InvalidArgument);
}

class ProjectBundleV2::Storage {
  public:
    BuildId build_id{sha256_content_hash("")};
    ContentHash bundle_digest{sha256_content_hash("")};
    DependencyLock dependency_lock;
    std::string manifest;
    std::vector<ArtifactDescriptor> descriptors;
    std::map<std::string, std::string> files;
};

ProjectBundleV2::ProjectBundleV2(std::unique_ptr<Storage> storage) : storage_{std::move(storage)} {
    require(storage_ != nullptr, "ProjectBundleV2 storage must not be null",
            ErrorCode::InvalidArgument);
}

ProjectBundleV2::ProjectBundleV2(ProjectBundleV2 &&) noexcept = default;
ProjectBundleV2 &ProjectBundleV2::operator=(ProjectBundleV2 &&) noexcept = default;
ProjectBundleV2::~ProjectBundleV2() = default;

const BuildId &ProjectBundleV2::build_id() const noexcept { return storage_->build_id; }

const ContentHash &ProjectBundleV2::bundle_digest() const noexcept {
    return storage_->bundle_digest;
}

const DependencyLock &ProjectBundleV2::dependency_lock() const noexcept {
    return storage_->dependency_lock;
}

std::string_view ProjectBundleV2::manifest_bytes() const noexcept { return storage_->manifest; }

std::span<const ArtifactDescriptor> ProjectBundleV2::artifacts() const noexcept {
    return storage_->descriptors;
}

std::vector<std::string> ProjectBundleV2::paths() const {
    auto result = std::vector<std::string>{};
    result.reserve(storage_->files.size());
    for (const auto &[path, unused] : storage_->files) {
        static_cast<void>(unused);
        result.push_back(path);
    }
    return result;
}

std::string ProjectBundleV2::archive_bytes() const { return zip_bytes(storage_->files); }

void ProjectBundleV2::write(const std::filesystem::path &destination,
                            ProjectBundleV2Representation representation) const {
    require(!destination.empty(), "destination must not be empty", ErrorCode::InvalidArgument);
    const auto parent =
        destination.parent_path().empty() ? std::filesystem::path{"."} : destination.parent_path();
    require(std::filesystem::exists(parent) && std::filesystem::is_directory(parent),
            "destination parent must be an existing directory", ErrorCode::InvalidArgument);
    require(!std::filesystem::is_symlink(std::filesystem::symlink_status(parent)),
            "destination parent must not be a symbolic link", ErrorCode::InvalidArgument);

    const auto destination_status = std::filesystem::symlink_status(destination);
    require(!std::filesystem::is_symlink(destination_status),
            "destination must not be a symbolic link", ErrorCode::InvalidArgument);
    require(!std::filesystem::exists(destination_status), "destination already exists",
            ErrorCode::InvalidState);

    auto stage = staging_path(destination);
    while (!std::filesystem::create_directory(stage)) {
        stage = staging_path(destination);
    }
    try {
        if (representation == ProjectBundleV2Representation::Directory) {
            for (const auto &[path, bytes] : storage_->files) {
                write_file(stage / std::filesystem::path{path}, bytes);
            }
        } else if (representation == ProjectBundleV2Representation::Zip) {
            write_file(stage / "bundle.zip", archive_bytes());
        } else {
            reject("output representation is unsupported", ErrorCode::InvalidArgument);
        }
        if (representation == ProjectBundleV2Representation::Zip) {
            std::filesystem::create_hard_link(stage / "bundle.zip", destination);
            auto ignored = std::error_code{};
            std::filesystem::remove_all(stage, ignored);
        } else {
            std::filesystem::rename(stage, destination);
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(stage, ignored);
        throw;
    }
}

namespace {

struct ValidatedProjectBuild {
    Json authoring_records;
    ContentHash authoring_digest;
    std::map<std::string, const LogicalInput *> logical_by_design;
};

[[nodiscard]] ValidatedProjectBuild
validate_project_build(const ProjectRunSummary &run, const LogicalInputName &entrypoint,
                       std::span<const AuthoringInput> inputs, const ProjectReport &diagnostics,
                       const ProjectReport &tests, std::span<const LogicalInput> logicals) {
    require(!logicals.empty(), "a complete bundle requires at least one logical model",
            ErrorCode::InvalidState);
    require(run.ok == (run.status != ProjectStatus::Failed),
            "run ok flag disagrees with its closed status");
    const auto diagnostics_report = detail::read_project_diagnostics_report(diagnostics.bytes);
    require(diagnostics_report.status == status_name(run.status),
            "run status disagrees with the diagnostics report");
    const auto tests_report = detail::read_project_tests_report(tests.bytes);
    require(tests_report.failed == 0U || run.status == ProjectStatus::Failed,
            "run status disagrees with failed project tests");

    auto input_records = std::vector<std::tuple<AuthoringInputKind, std::string, ContentHash>>{};
    input_records.reserve(inputs.size());
    for (const auto &input : inputs) {
        input_records.emplace_back(input.kind(), input.name().value(),
                                   sha256_content_hash(input.bytes()));
    }
    std::ranges::sort(input_records, [](const auto &lhs, const auto &rhs) {
        return std::tuple{authoring_kind_name(std::get<0>(lhs)), std::get<1>(lhs),
                          std::get<2>(lhs).value()} <
               std::tuple{authoring_kind_name(std::get<0>(rhs)), std::get<1>(rhs),
                          std::get<2>(rhs).value()};
    });
    require(std::ranges::adjacent_find(input_records,
                                       [](const auto &lhs, const auto &rhs) {
                                           return std::get<0>(lhs) == std::get<0>(rhs) &&
                                                  std::get<1>(lhs) == std::get<1>(rhs);
                                       }) == input_records.end(),
            "authoring inputs contain a duplicate kind/name pair", ErrorCode::DuplicateName);
    const auto entrypoint_count = std::ranges::count_if(input_records, [&](const auto &record) {
        return std::get<0>(record) == AuthoringInputKind::ProjectSource &&
               std::get<1>(record) == entrypoint.value();
    });
    require(entrypoint_count == 1, "entrypoint must name exactly one ProjectSource input record",
            ErrorCode::InvalidArgument);
    auto authoring_records = Json::array();
    for (const auto &[kind, name, digest] : input_records) {
        authoring_records.push_back(Json{{"kind", authoring_kind_name(kind)},
                                         {"name", name},
                                         {"content_digest", digest.value()}});
    }
    const auto authoring_core =
        Json{{"entrypoint", entrypoint.value()}, {"records", authoring_records}};

    auto logical_by_design = std::map<std::string, const LogicalInput *>{};
    for (const auto &logical : logicals) {
        const auto [unused, inserted] = logical_by_design.emplace(logical.design.value(), &logical);
        static_cast<void>(unused);
        require(inserted, "logical DesignKey is duplicated", ErrorCode::DuplicateName);
    }
    return ValidatedProjectBuild{std::move(authoring_records),
                                 sha256_content_hash(canonical(authoring_core)),
                                 std::move(logical_by_design)};
}

class ProjectArtifactGraph {
  public:
    void accumulate_authoritative_artifacts(const ValidatedProjectBuild &validated,
                                            std::span<const LogicalInput> logicals,
                                            std::span<const SchematicInput> schematics,
                                            std::span<const BoardInput> boards,
                                            const ProjectReport &diagnostics,
                                            const ProjectReport &tests);

    void materialize_selected_exports(const ValidatedProjectBuild &validated,
                                      std::span<const SchematicInput> schematics,
                                      std::span<const BoardInput> boards,
                                      const ExportSelection &exports);

    void validate();

    [[nodiscard]] DependencyLock dependency_lock() const;

    [[nodiscard]] const std::map<std::string, DraftArtifact> &artifacts() const noexcept {
        return artifacts_;
    }

    [[nodiscard]] const Json &export_selection() const noexcept { return export_selection_; }

  private:
    [[nodiscard]] const DraftArtifact &add_artifact(ArtifactId id, std::string bytes,
                                                    std::vector<ArtifactRef> dependencies,
                                                    ContentHash producer_build_value,
                                                    bool required_default = true);

    void admit_library(const PartLibraryBundle &bundle);

    [[nodiscard]] ArtifactRef add_component(const PartLibraryBundle &bundle,
                                            const ComponentDefinition &component);

    [[nodiscard]] ArtifactRef add_part(const PartLibraryBundle &bundle,
                                       const LibraryPartRef &selected);

    [[nodiscard]] const DraftArtifact &require_target(const ArtifactRef &target) const;

    [[nodiscard]] const BoardInput &board_for_compiled(const ArtifactRef &target,
                                                       std::span<const BoardInput> boards) const;

    std::map<std::string, DraftArtifact> artifacts_;
    std::map<std::string, const PartLibraryBundle *> library_rows_;
    std::map<std::string, ArtifactRef> selected_refs_;
    std::map<std::string, ArtifactRef> component_artifacts_;
    std::map<std::string, ArtifactRef> part_artifacts_;
    std::map<std::string, ArtifactRef> attachment_artifacts_;
    std::map<std::string, ArtifactRef> glb_artifacts_;
    std::set<std::string> consumed_glb_assets_;
    Json export_selection_ = Json::array();
};

} // namespace

class ProjectBundleV2Builder::Storage {
  public:
    ProjectIdentity project;
    ProjectRunSummary run;
    LogicalInputName entrypoint;
    std::vector<AuthoringInput> inputs;
    ProjectReport diagnostics;
    ProjectReport tests;
    std::vector<LogicalInput> logicals;
    std::vector<SchematicInput> schematics;
    std::vector<BoardInput> boards;
    ExportSelection exports;

    Storage(ProjectIdentity project_value, ProjectRunSummary run_value,
            LogicalInputName entrypoint_value, std::vector<AuthoringInput> input_values,
            ProjectReport diagnostics_value, ProjectReport tests_value)
        : project{std::move(project_value)}, run{std::move(run_value)},
          entrypoint{std::move(entrypoint_value)}, inputs{std::move(input_values)},
          diagnostics{std::move(diagnostics_value)}, tests{std::move(tests_value)} {}
};

namespace {

const DraftArtifact &ProjectArtifactGraph::add_artifact(ArtifactId id, std::string bytes,
                                                        std::vector<ArtifactRef> dependencies,
                                                        ContentHash producer_build_value,
                                                        bool required_default) {
    normalize_dependencies(dependencies);
    auto artifact = DraftArtifact{std::move(id), std::move(bytes), std::move(dependencies),
                                  std::move(producer_build_value), required_default};
    const auto key = artifact_key(artifact.id);
    const auto [position, inserted] = artifacts_.emplace(key, std::move(artifact));
    require(inserted, "artifact identity is duplicated", ErrorCode::DuplicateName);
    return position->second;
}

void ProjectArtifactGraph::admit_library(const PartLibraryBundle &bundle) {
    const auto key = bundle.identity().namespace_name() + '\n' + bundle.identity().version() +
                     '\n' + bundle.library_digest().value();
    const auto same_release = std::ranges::find_if(library_rows_, [&](const auto &candidate) {
        const auto &admitted = *candidate.second;
        return admitted.identity().namespace_name() == bundle.identity().namespace_name() &&
               admitted.identity().version() == bundle.identity().version();
    });
    require(same_release == library_rows_.end() ||
                same_release->second->library_digest() == bundle.library_digest(),
            "one library release has conflicting exact digests");
    library_rows_.emplace(key, &bundle);
}

ArtifactRef ProjectArtifactGraph::add_component(const PartLibraryBundle &bundle,
                                                const ComponentDefinition &component) {
    admit_library(bundle);
    const auto owner = component_ref(bundle, component);
    const auto id = ArtifactId{ArtifactKind::ComponentDefinition, owner};
    const auto key = artifact_key(id);
    if (const auto existing = component_artifacts_.find(key);
        existing != component_artifacts_.end()) {
        return existing->second;
    }
    const auto bytes = bundle.component_document(component.contract().key());
    require(bytes.has_value(), "reachable component definition bytes are missing",
            ErrorCode::UnknownEntity);
    require(sha256_content_hash(*bytes) != sha256_content_hash(""),
            "component definition bytes are empty", ErrorCode::InvalidState);
    auto symbol_dependencies = std::vector<ArtifactRef>{};
    for (const auto &symbol : component.schematic_symbols()) {
        if (symbol.variant() != "default") {
            continue;
        }
        const auto component_entry =
            std::ranges::find(bundle.entries(), "component:" + component.contract().key().value(),
                              &PartLibraryBundleEntry::id);
        require(component_entry != bundle.entries().end(),
                "component definition entry is absent from the selected closure",
                ErrorCode::UnknownEntity);
        const auto symbol_key = "symbol:" + symbol.name() + "@" + symbol.variant();
        const auto symbol_entry =
            std::ranges::find_if(bundle.entries(), [&](const auto &candidate) {
                return candidate.role() == PartLibraryBundleEntryRole::Symbol &&
                       candidate.source_key() == std::optional{symbol_key} &&
                       std::ranges::find(component_entry->dependencies(), candidate.id()) !=
                           component_entry->dependencies().end();
            });
        require(symbol_entry != bundle.entries().end(),
                "component default symbol is absent from the selected closure",
                ErrorCode::UnknownEntity);
        const auto reference =
            PartAssetReference{PartAssetKind::Schematic, symbol_key, symbol_entry->digest()};
        const auto symbol_owner = attachment_ref(bundle, reference, LibraryAttachmentKind::Symbol);
        const auto symbol_id = ArtifactId{ArtifactKind::SymbolDefinition, symbol_owner};
        const auto symbol_artifact_key = artifact_key(symbol_id);
        auto symbol_ref = attachment_artifacts_.find(symbol_artifact_key);
        if (symbol_ref == attachment_artifacts_.end()) {
            const auto symbol_bytes = bundle.asset(reference);
            require(symbol_bytes.has_value(), "component default symbol bytes are missing",
                    ErrorCode::UnknownEntity);
            const auto &added =
                add_artifact(symbol_id, std::string{*symbol_bytes}, {}, bundle.library_digest());
            symbol_ref = attachment_artifacts_.emplace(symbol_artifact_key, ref_for(added)).first;
        }
        symbol_dependencies.push_back(symbol_ref->second);
    }
    const auto &added = add_artifact(id, std::string{*bytes}, std::move(symbol_dependencies),
                                     bundle.library_digest());
    const auto reference = ref_for(added);
    component_artifacts_.emplace(key, reference);
    return reference;
}

ArtifactRef ProjectArtifactGraph::add_part(const PartLibraryBundle &bundle,
                                           const LibraryPartRef &selected) {
    admit_library(bundle);
    const auto &part = bundle.resolve(selected);
    const auto id = ArtifactId{ArtifactKind::PartDefinition, selected};
    const auto key = artifact_key(id);
    if (const auto existing = part_artifacts_.find(key); existing != part_artifacts_.end()) {
        return existing->second;
    }
    const auto component =
        std::ranges::find(bundle.library().components(), part.implemented_component(),
                          &ComponentDefinition::content_identity);
    require(component != bundle.library().components().end(),
            "selected part component definition is missing", ErrorCode::UnknownEntity);
    auto dependencies = std::vector<ArtifactRef>{add_component(bundle, *component)};
    for (const auto &asset : part_asset_references(part)) {
        if (asset.kind() == PartAssetKind::Footprint) {
            const auto owner = attachment_ref(bundle, asset, LibraryAttachmentKind::Footprint);
            const auto asset_id = ArtifactId{ArtifactKind::FootprintDefinition, owner};
            const auto asset_key_value = artifact_key(asset_id);
            auto reference = attachment_artifacts_.find(asset_key_value);
            if (reference == attachment_artifacts_.end()) {
                const auto bytes = bundle.asset(asset);
                require(bytes.has_value(), "selected footprint bytes are missing",
                        ErrorCode::UnknownEntity);
                const auto &added =
                    add_artifact(asset_id, std::string{*bytes}, {}, bundle.library_digest());
                reference = attachment_artifacts_.emplace(asset_key_value, ref_for(added)).first;
            }
            dependencies.push_back(reference->second);
        } else if (asset.kind() == PartAssetKind::Schematic) {
            const auto owner = attachment_ref(bundle, asset, LibraryAttachmentKind::Symbol);
            const auto asset_id = ArtifactId{ArtifactKind::SymbolDefinition, owner};
            const auto asset_key_value = artifact_key(asset_id);
            auto reference = attachment_artifacts_.find(asset_key_value);
            if (reference == attachment_artifacts_.end()) {
                const auto bytes = bundle.asset(asset);
                require(bytes.has_value(), "selected symbol bytes are missing",
                        ErrorCode::UnknownEntity);
                const auto &added =
                    add_artifact(asset_id, std::string{*bytes}, {}, bundle.library_digest());
                reference = attachment_artifacts_.emplace(asset_key_value, ref_for(added)).first;
            }
            dependencies.push_back(reference->second);
        } else if (asset.kind() == PartAssetKind::Model3D &&
                   asset.key().starts_with("model:glb/")) {
            const auto owner = library_asset_ref(bundle, asset, LibraryAssetKind::Glb);
            const auto asset_id = ArtifactId{ArtifactKind::GlbAsset, owner};
            const auto asset_key_value = artifact_key(asset_id);
            if (!consumed_glb_assets_.contains(asset_key_value)) {
                continue;
            }
            auto reference = glb_artifacts_.find(asset_key_value);
            if (reference == glb_artifacts_.end()) {
                const auto bytes = bundle.asset(asset);
                require(bytes.has_value(), "consumed GLB bytes are missing",
                        ErrorCode::UnknownEntity);
                require(sha256_content_hash(*bytes) == asset.digest(),
                        "consumed GLB digest conflicts with library bytes");
                const auto &added =
                    add_artifact(asset_id, std::string{*bytes}, {}, bundle.library_digest());
                reference = glb_artifacts_.emplace(asset_key_value, ref_for(added)).first;
            }
            dependencies.push_back(reference->second);
        }
    }
    const auto bytes = bundle.part_document(selected.part_key());
    require(bytes.has_value(), "selected part bytes are missing", ErrorCode::UnknownEntity);
    const auto &added =
        add_artifact(id, std::string{*bytes}, std::move(dependencies), bundle.library_digest());
    const auto reference = ref_for(added);
    part_artifacts_.emplace(key, reference);
    selected_refs_.emplace(key, reference);
    return reference;
}

const DraftArtifact &ProjectArtifactGraph::require_target(const ArtifactRef &target) const {
    const auto match = artifacts_.find(artifact_key(target.artifact()));
    require(match != artifacts_.end() &&
                sha256_content_hash(match->second.bytes) == target.content_digest(),
            "export target is unresolved, foreign, or stale", ErrorCode::UnknownEntity);
    return match->second;
}

const BoardInput &
ProjectArtifactGraph::board_for_compiled(const ArtifactRef &target,
                                         std::span<const BoardInput> boards) const {
    const auto &artifact = require_target(target);
    require(artifact.id.kind() == ArtifactKind::CompiledBoard,
            "export requires a CompiledBoard target", ErrorCode::InvalidArgument);
    const auto &identity = std::get<CompiledBoardIdentity>(artifact.id.owner());
    const auto match = std::ranges::find_if(
        boards, [&](const auto &candidate) { return candidate.compiled->identity() == identity; });
    require(match != boards.end(), "export CompiledBoard owner is unavailable",
            ErrorCode::UnknownEntity);
    return *match;
}

} // namespace

ProjectBundleV2Builder::ProjectBundleV2Builder(ProjectIdentity project, ProjectRunSummary run,
                                               LogicalInputName entrypoint,
                                               std::vector<AuthoringInput> inputs,
                                               ProjectReport diagnostics, ProjectReport tests)
    : storage_{std::make_unique<Storage>(std::move(project), std::move(run), std::move(entrypoint),
                                         std::move(inputs), std::move(diagnostics),
                                         std::move(tests))} {
    require(!storage_->project.name.empty(), "project name must not be empty",
            ErrorCode::InvalidArgument);
    require(!storage_->run.profile.empty(), "profile name must not be empty",
            ErrorCode::InvalidArgument);
    auto stage_names = std::set<std::string>{};
    for (const auto &stage : storage_->run.stages) {
        require(!stage.empty(), "executed stage name must not be empty",
                ErrorCode::InvalidArgument);
        require(stage_names.insert(stage).second, "executed stage name is duplicated",
                ErrorCode::DuplicateName);
    }
}

ProjectBundleV2Builder::ProjectBundleV2Builder(ProjectBundleV2Builder &&) noexcept = default;
ProjectBundleV2Builder &
ProjectBundleV2Builder::operator=(ProjectBundleV2Builder &&) noexcept = default;
ProjectBundleV2Builder::~ProjectBundleV2Builder() = default;

ProjectBundleV2Builder &
ProjectBundleV2Builder::add_logical(DesignKey design, const Circuit &circuit,
                                    const PartLibraryBundle &selected_closure) {
    storage_->logicals.push_back(LogicalInput{std::move(design), &circuit, &selected_closure});
    return *this;
}

ProjectBundleV2Builder &ProjectBundleV2Builder::add_schematic(DesignKey design,
                                                              SchematicKey schematic,
                                                              const Schematic &model) {
    storage_->schematics.push_back(SchematicInput{std::move(design), std::move(schematic), &model});
    return *this;
}

ProjectBundleV2Builder &
ProjectBundleV2Builder::add_board(DesignKey design, const Board &authoring_board,
                                  const CompiledBoard &compiled, const BoardScene &scene,
                                  const PartLibraryBundle &selected_closure) {
    storage_->boards.push_back(
        BoardInput{std::move(design), &authoring_board, &compiled, &scene, &selected_closure});
    return *this;
}

ProjectBundleV2Builder &ProjectBundleV2Builder::select_exports(ExportSelection selection) {
    storage_->exports = std::move(selection);
    return *this;
}

namespace {

void ProjectArtifactGraph::accumulate_authoritative_artifacts(
    const ValidatedProjectBuild &validated, std::span<const LogicalInput> logicals,
    std::span<const SchematicInput> schematics, std::span<const BoardInput> boards,
    const ProjectReport &diagnostics, const ProjectReport &tests) {
    for (const auto &board : boards) {
        if (!board.compiled->capabilities().has(BoardAssetCapability::Models3D)) {
            require(board.scene->models().empty(),
                    "BoardScene references GLBs without models3d capability");
            continue;
        }
        for (const auto &resolved : board.compiled->parts()) {
            if (!resolved.physical_part().model_3d().has_value() ||
                resolved.physical_part().model_3d()->format() != "glb") {
                continue;
            }
            require(resolved.model_3d_bytes().has_value(),
                    "CompiledBoard consumed GLB bytes are missing", ErrorCode::UnknownEntity);
            const auto &part = board.bundle->resolve(resolved.reference());
            const auto model = model_reference(part);
            require(model.has_value() && model->key().starts_with("model:glb/") &&
                        model->digest() == sha256_content_hash(*resolved.model_3d_bytes()),
                    "CompiledBoard GLB provenance conflicts with its exact selected part");
            const auto id =
                ArtifactId{ArtifactKind::GlbAsset,
                           library_asset_ref(*board.bundle, *model, LibraryAssetKind::Glb)};
            consumed_glb_assets_.insert(artifact_key(id));
        }
    }

    auto logical_refs = std::map<std::string, ArtifactRef>{};
    for (const auto &logical : logicals) {
        auto dependencies = std::vector<ArtifactRef>{};
        for (const auto &component : logical.circuit->all<ComponentId>()) {
            const auto &definition = logical.circuit->get(component.definition());
            if (definition.source().has_value()) {
                const auto admitted =
                    logical.bundle->library().component(definition.contract().key());
                require(admitted.has_value() &&
                            admitted->get().content_identity() == definition.content_identity(),
                        "external component definition is outside the exact selected closure",
                        ErrorCode::UnknownEntity);
                dependencies.push_back(add_component(*logical.bundle, admitted->get()));
            }
            if (component.selected_library_part_ref().has_value()) {
                dependencies.push_back(
                    add_part(*logical.bundle, *component.selected_library_part_ref()));
            }
        }
        const auto id =
            ArtifactId{ArtifactKind::LogicalModel, LogicalArtifactIdentity{logical.design}};
        const auto &added = add_artifact(id, write_logical_circuit(*logical.circuit),
                                         std::move(dependencies), sha256_content_hash(""));
        logical_refs.emplace(logical.design.value(), ref_for(added));
    }

    for (const auto &schematic : schematics) {
        const auto logical = validated.logical_by_design.find(schematic.design.value());
        require(logical != validated.logical_by_design.end(), "Schematic has no logical owner",
                ErrorCode::UnknownEntity);
        require(&schematic.schematic->circuit() == logical->second->circuit,
                "Schematic crosses its declared logical owner");
        const auto id = ArtifactId{ArtifactKind::SchematicModel,
                                   SchematicArtifactIdentity{schematic.design, schematic.key}};
        const auto &added =
            add_artifact(id, write_schematic(*schematic.schematic),
                         {logical_refs.at(schematic.design.value())}, sha256_content_hash(""));
        static_cast<void>(added);
    }

    for (const auto &board : boards) {
        const auto logical = validated.logical_by_design.find(board.design.value());
        require(logical != validated.logical_by_design.end(), "Board has no logical owner",
                ErrorCode::UnknownEntity);
        require(&board.board->circuit() == logical->second->circuit,
                "Board crosses its declared logical owner");
        require(board.bundle->library_digest() == logical->second->bundle->library_digest(),
                "Board closure conflicts with its logical owner closure");
        require(board.compiled->board_name() == board.board->name(),
                "CompiledBoard belongs to a different Board");
        require(board.scene->source() == board.compiled->identity(),
                "BoardScene is stale or belongs to another CompiledBoard");
        require(sha256_content_hash(board.compiled->physical_snapshot()) ==
                    board.compiled->provenance().physical_snapshot_digest(),
                "CompiledBoard physical snapshot digest is stale");
        const auto authoring_board_bytes = write_pcb_board(*board.board, FootprintLibrary{});
        const auto authoring_board_snapshot =
            nlohmann::json::parse(authoring_board_bytes.begin(), authoring_board_bytes.end())
                .dump();
        require(sha256_content_hash(authoring_board_snapshot) ==
                    board.compiled->provenance().physical_snapshot_digest(),
                "CompiledBoard does not match the current authoring Board revision");
        require(sha256_content_hash(board.compiled->logical_dependency_snapshot()) ==
                    board.compiled->provenance().logical_dependency_digest(),
                "CompiledBoard logical dependency snapshot is stale");
        require(compiled_board_logical_dependency_digest(*logical->second->circuit) ==
                    board.compiled->provenance().logical_dependency_digest(),
                "CompiledBoard does not match the current logical owner revision");

        const auto board_id = ArtifactId{ArtifactKind::BoardModel,
                                         BoardArtifactIdentity{board.design, board.board->name()}};
        const auto &authoring =
            add_artifact(board_id, authoring_board_bytes, {logical_refs.at(board.design.value())},
                         sha256_content_hash(""));
        const auto authoring_ref = ref_for(authoring);

        auto compiled_dependencies =
            std::vector<ArtifactRef>{logical_refs.at(board.design.value()), authoring_ref};
        auto board_glbs = std::vector<ArtifactRef>{};
        for (const auto &resolved : board.compiled->parts()) {
            const auto selected = add_part(*board.bundle, resolved.reference());
            compiled_dependencies.push_back(selected);
            const auto &part = board.bundle->resolve(resolved.reference());
            const auto component = std::ranges::find(board.bundle->library().components(),
                                                     part.implemented_component(),
                                                     &ComponentDefinition::content_identity);
            require(component != board.bundle->library().components().end(),
                    "CompiledBoard selected component is absent", ErrorCode::UnknownEntity);
            compiled_dependencies.push_back(add_component(*board.bundle, *component));
            const auto footprint = footprint_reference(part);
            const auto footprint_id = ArtifactId{
                ArtifactKind::FootprintDefinition,
                attachment_ref(*board.bundle, footprint, LibraryAttachmentKind::Footprint)};
            compiled_dependencies.push_back(attachment_artifacts_.at(artifact_key(footprint_id)));
            if (board.compiled->capabilities().has(BoardAssetCapability::Models3D)) {
                const auto model = model_reference(part);
                if (model.has_value() && model->key().starts_with("model:glb/")) {
                    const auto glb_id =
                        ArtifactId{ArtifactKind::GlbAsset,
                                   library_asset_ref(*board.bundle, *model, LibraryAssetKind::Glb)};
                    const auto glb = glb_artifacts_.find(artifact_key(glb_id));
                    require(glb != glb_artifacts_.end(),
                            "CompiledBoard GLB closure is not vendored", ErrorCode::UnknownEntity);
                    compiled_dependencies.push_back(glb->second);
                    board_glbs.push_back(glb->second);
                }
            }
        }
        normalize_dependencies(compiled_dependencies);
        const auto compiled_id =
            ArtifactId{ArtifactKind::CompiledBoard, board.compiled->identity()};
        const auto &compiled_artifact =
            add_artifact(compiled_id, std::string{board.compiled->bytes()},
                         std::move(compiled_dependencies), sha256_content_hash(""));
        const auto compiled_ref = ref_for(compiled_artifact);

        normalize_dependencies(board_glbs);
        require(board.scene->models().size() == board_glbs.size(),
                "BoardScene GLB set does not equal its CompiledBoard GLB closure");
        for (const auto &model : board.scene->models()) {
            require(std::ranges::any_of(board_glbs,
                                        [&](const auto &reference) {
                                            return reference.content_digest() ==
                                                   model.reference().digest();
                                        }),
                    "BoardScene references a foreign or unconsumed GLB");
        }
        auto scene_dependencies = std::vector<ArtifactRef>{compiled_ref};
        scene_dependencies.insert(scene_dependencies.end(), board_glbs.begin(), board_glbs.end());
        const auto scene_id = ArtifactId{ArtifactKind::BoardScene,
                                         BoardSceneArtifactIdentity{board.compiled->identity()}};
        const auto &scene_artifact =
            add_artifact(scene_id, write_board_scene(*board.scene), std::move(scene_dependencies),
                         sha256_content_hash(""));
        static_cast<void>(scene_artifact);
    }

    auto evaluated_dependencies = std::vector<ArtifactRef>{};
    for (const auto &[unused, artifact] : artifacts_) {
        static_cast<void>(unused);
        const auto reference = ref_for(artifact);
        if (artifact.id.kind() == ArtifactKind::LogicalModel ||
            artifact.id.kind() == ArtifactKind::SchematicModel ||
            artifact.id.kind() == ArtifactKind::BoardModel) {
            evaluated_dependencies.push_back(reference);
        }
    }
    sort_unique_dependencies(evaluated_dependencies);

    const auto diagnostics_id = ArtifactId{ArtifactKind::Diagnostics,
                                           ProjectSingletonIdentity{ProjectSingletonKey::Primary}};
    const auto &diagnostics_artifact = add_artifact(
        diagnostics_id, diagnostics.bytes, evaluated_dependencies, sha256_content_hash(""));
    static_cast<void>(diagnostics_artifact);
    const auto tests_id = ArtifactId{ArtifactKind::ProjectTests,
                                     ProjectSingletonIdentity{ProjectSingletonKey::Primary}};
    const auto &tests_artifact =
        add_artifact(tests_id, tests.bytes, evaluated_dependencies, sha256_content_hash(""));
    static_cast<void>(tests_artifact);
}

} // namespace

namespace {

void ProjectArtifactGraph::materialize_selected_exports(const ValidatedProjectBuild &validated,
                                                        std::span<const SchematicInput> schematics,
                                                        std::span<const BoardInput> boards,
                                                        const ExportSelection &exports) {
    auto export_records = std::vector<std::pair<ContentHash, Json>>{};
    auto seen_export_requests = std::set<std::string>{};
    for (const auto &request : exports) {
        const auto request_json = export_request_json(request);
        const auto request_digest = sha256_content_hash(canonical(request_json));
        require(seen_export_requests.insert(request_digest.value()).second,
                "export selection contains a duplicate request", ErrorCode::DuplicateName);

        auto bytes = std::string{};
        auto dependencies = std::vector<ArtifactRef>{};
        auto output_kind = ArtifactKind::SchematicSvg;
        auto output_key = ExportOutputKey{PrimaryExportOutputKey::Primary};
        auto producer_build_value = sha256_content_hash("");
        switch (request.kind) {
        case ExportKind::SchematicSvg: {
            require(std::holds_alternative<ModelExportTarget>(request.target) &&
                        std::holds_alternative<SchematicSvgParameters>(request.parameters),
                    "Schematic SVG request has an incompatible target or parameters",
                    ErrorCode::InvalidArgument);
            const auto &target = std::get<ModelExportTarget>(request.target).model;
            const auto &artifact = require_target(target);
            require(artifact.id.kind() == ArtifactKind::SchematicModel,
                    "Schematic SVG target is not a Schematic", ErrorCode::InvalidArgument);
            const auto &owner = std::get<SchematicArtifactIdentity>(artifact.id.owner());
            const auto match = std::ranges::find_if(schematics, [&](const auto &candidate) {
                return candidate.design == owner.design && candidate.key == owner.schematic;
            });
            require(match != schematics.end(), "Schematic SVG owner is unavailable",
                    ErrorCode::UnknownEntity);
            bytes = write_schematic_svg(*match->schematic);
            dependencies.push_back(target);
            output_kind = ArtifactKind::SchematicSvg;
            break;
        }
        case ExportKind::BoardSvg: {
            require(std::holds_alternative<ModelExportTarget>(request.target) &&
                        std::holds_alternative<BoardSvgParameters>(request.parameters),
                    "Board SVG request has an incompatible target or parameters",
                    ErrorCode::InvalidArgument);
            const auto &target = std::get<ModelExportTarget>(request.target).model;
            const auto &board = board_for_compiled(target, boards);
            bytes = write_pcb_placement_svg(board.compiled->board(), board.compiled->footprints());
            dependencies.push_back(target);
            output_kind = ArtifactKind::BoardSvg;
            break;
        }
        case ExportKind::BoardLayerImage: {
            require(std::holds_alternative<BoardLayerExportTarget>(request.target) &&
                        std::holds_alternative<BoardLayerImageParameters>(request.parameters),
                    "Board layer request has an incompatible target or parameters",
                    ErrorCode::InvalidArgument);
            const auto &target = std::get<BoardLayerExportTarget>(request.target);
            const auto &board = board_for_compiled(target.compiled_board, boards);
            auto layer = std::optional<BoardLayerId>{};
            for (std::size_t index = 0; index < board.compiled->board().all<BoardLayerId>().size();
                 ++index) {
                const auto candidate = BoardLayerId{index};
                if (board.compiled->board().get(candidate).name() == target.layer.value()) {
                    require(!layer.has_value(), "Board layer target is ambiguous",
                            ErrorCode::DuplicateName);
                    layer = candidate;
                }
            }
            require(layer.has_value(), "Board layer target is unresolved",
                    ErrorCode::UnknownEntity);
            bytes = write_pcb_placement_svg(board.compiled->board(), board.compiled->footprints(),
                                            PcbPlacementSvgOptions{.layer_filter = layer});
            dependencies.push_back(target.compiled_board);
            output_kind = ArtifactKind::BoardLayerImage;
            output_key = target.layer;
            break;
        }
        case ExportKind::Bom: {
            require(std::holds_alternative<ModelExportTarget>(request.target) &&
                        std::holds_alternative<BomParameters>(request.parameters),
                    "BOM request has an incompatible target or parameters",
                    ErrorCode::InvalidArgument);
            const auto &target = std::get<ModelExportTarget>(request.target).model;
            const auto &artifact = require_target(target);
            require(artifact.id.kind() == ArtifactKind::LogicalModel,
                    "BOM target is not a logical model", ErrorCode::InvalidArgument);
            const auto &owner = std::get<LogicalArtifactIdentity>(artifact.id.owner());
            bytes = write_bom_json(
                project_bom(*validated.logical_by_design.at(owner.design.value())->circuit));
            dependencies.push_back(target);
            output_kind = ArtifactKind::Bom;
            break;
        }
        case ExportKind::Cpl: {
            require(std::holds_alternative<ModelExportTarget>(request.target) &&
                        std::holds_alternative<CplParameters>(request.parameters),
                    "CPL request has an incompatible target or parameters",
                    ErrorCode::InvalidArgument);
            const auto &target = std::get<ModelExportTarget>(request.target).model;
            const auto &board = board_for_compiled(target, boards);
            bytes = write_cpl_json(project_cpl(*board.compiled).cpl());
            dependencies.push_back(target);
            output_kind = ArtifactKind::Cpl;
            break;
        }
        case ExportKind::Step: {
            require(std::holds_alternative<LibraryAssetExportTarget>(request.target) &&
                        std::holds_alternative<StepParameters>(request.parameters),
                    "STEP request has an incompatible target or parameters",
                    ErrorCode::InvalidArgument);
            const auto &target = std::get<LibraryAssetExportTarget>(request.target);
            const auto &part_artifact = require_target(target.selected_part);
            require(part_artifact.id.kind() == ArtifactKind::PartDefinition &&
                        target.asset.kind == LibraryAssetKind::Step,
                    "STEP request target is not an exact selected-part STEP asset",
                    ErrorCode::InvalidArgument);
            const auto &selected = std::get<LibraryPartRef>(part_artifact.id.owner());
            const auto bundle_match =
                std::ranges::find_if(library_rows_, [&](const auto &candidate) {
                    return candidate.second->identity().namespace_name() ==
                               target.asset.library_namespace &&
                           candidate.second->identity().version() == target.asset.library_version &&
                           candidate.second->library_digest() == target.asset.library_bundle_digest;
                });
            require(bundle_match != library_rows_.end(), "STEP asset library origin is not locked",
                    ErrorCode::UnknownEntity);
            const auto &bundle = *bundle_match->second;
            const auto &part = bundle.resolve(selected);
            const auto model = model_reference(part);
            require(model.has_value() && model->key().starts_with("model:step/") &&
                        model->digest() == target.asset.content_digest,
                    "STEP asset is not persisted by the selected part", ErrorCode::UnknownEntity);
            const auto asset_bytes = bundle.asset(*model);
            require(asset_bytes.has_value() &&
                        sha256_content_hash(*asset_bytes) == target.asset.content_digest,
                    "STEP asset bytes are missing or stale", ErrorCode::UnknownEntity);
            bytes = std::string{*asset_bytes};
            dependencies.push_back(target.selected_part);
            output_kind = ArtifactKind::StepAsset;
            output_key = target.asset;
            producer_build_value = bundle.library_digest();
            break;
        }
        case ExportKind::KicadPcb:
        case ExportKind::Fabrication:
        case ExportKind::WholeBoardGlb:
            reject("selected exporter is unsupported by this producer contract",
                   ErrorCode::InvalidArgument);
        }
        const auto export_id =
            ArtifactId{output_kind, ExportArtifactIdentity{request_digest, std::move(output_key)}};
        static_cast<void>(add_artifact(export_id, std::move(bytes), std::move(dependencies),
                                       std::move(producer_build_value), false));
        export_records.emplace_back(request_digest, request_json);
    }
    std::ranges::sort(export_records, {}, [](const auto &record) { return record.first.value(); });
    for (const auto &[unused, record] : export_records) {
        static_cast<void>(unused);
        export_selection_.push_back(record);
    }
}

DependencyLock ProjectArtifactGraph::dependency_lock() const {
    auto locked_libraries = std::vector<LockedLibrary>{};
    for (const auto &[unused, bundle] : library_rows_) {
        static_cast<void>(unused);
        locked_libraries.push_back(LockedLibrary{bundle->identity().namespace_name(),
                                                 bundle->identity().version(),
                                                 bundle->library_digest()});
    }
    auto locked_parts = std::vector<LockedPartSelection>{};
    for (const auto &[unused, selected] : selected_refs_) {
        static_cast<void>(unused);
        locked_parts.push_back(
            LockedPartSelection{std::get<LibraryPartRef>(selected.artifact().owner()), selected});
    }
    return DependencyLock{std::move(locked_libraries), std::move(locked_parts)};
}

void ProjectArtifactGraph::validate() {
    for (const auto &[artifact_key_value, artifact] : artifacts_) {
        for (const auto &dependency : artifact.dependencies) {
            const auto target = artifacts_.find(artifact_key(dependency.artifact()));
            require(target != artifacts_.end() &&
                        sha256_content_hash(target->second.bytes) == dependency.content_digest(),
                    "artifact graph contains a missing or stale direct dependency",
                    ErrorCode::UnknownEntity);
            require(!artifact.required_default || target->second.required_default,
                    "required default artifact depends on a selected export");
        }
        static_cast<void>(artifact_key_value);
    }
    auto visit_state = std::map<std::string, std::uint8_t>{};
    auto visit = std::function<void(const std::string &)>{};
    visit = [&](const std::string &key) {
        const auto state = visit_state[key];
        require(state != 1U, "artifact dependency graph is cyclic");
        if (state == 2U) {
            return;
        }
        visit_state[key] = 1U;
        for (const auto &dependency : artifacts_.at(key).dependencies) {
            visit(artifact_key(dependency.artifact()));
        }
        visit_state[key] = 2U;
    };
    for (const auto &[key, unused] : artifacts_) {
        static_cast<void>(unused);
        visit(key);
    }
}

[[nodiscard]] ProjectBundleV2 encode_project_bundle(const ProjectIdentity &project,
                                                    const ProjectRunSummary &run,
                                                    const LogicalInputName &entrypoint,
                                                    const ValidatedProjectBuild &validated,
                                                    const ProjectArtifactGraph &graph) {
    const auto dependency_lock = graph.dependency_lock();
    const auto dependency_lock_value = dependency_lock_json(dependency_lock);
    const auto &artifacts = graph.artifacts();

    auto required_identity_artifacts = Json::array();
    for (const auto &[unused, artifact] : artifacts) {
        static_cast<void>(unused);
        if (!artifact.required_default) {
            continue;
        }
        auto dependencies = Json::array();
        for (const auto &dependency : artifact.dependencies) {
            dependencies.push_back(artifact_ref_json(dependency));
        }
        const auto schema = schema_for_kind(artifact.id.kind());
        required_identity_artifacts.push_back(
            Json{{"id", artifact_id_json(artifact.id)},
                 {"role", artifact_role_name(role_for_kind(artifact.id.kind()))},
                 {"kind", artifact_kind_name(artifact.id.kind())},
                 {"schema", Json{{"format", schema.format}, {"version", schema.version}}},
                 {"media_type", schema.media_type},
                 {"content_digest", sha256_content_hash(artifact.bytes).value()},
                 {"depends_on", std::move(dependencies)},
                 {"producer_name", library_producer_name(artifact.id.kind())},
                 {"producer_version", producer_version}});
    }
    const auto project_json =
        Json{{"name", project.name},
             {"version", project.version.has_value() ? Json(*project.version) : Json(nullptr)},
             {"description",
              project.description.has_value() ? Json(*project.description) : Json(nullptr)}};
    const auto build_identity =
        Json{{"format", format_name},
             {"schema_version", 2},
             {"project", project_json},
             {"authoring_inputs_digest", validated.authoring_digest.value()},
             {"dependency_lock", dependency_lock_value},
             {"artifacts", std::move(required_identity_artifacts)}};
    const auto build_id = BuildId{sha256_content_hash(canonical(build_identity))};

    auto result = std::make_unique<ProjectBundleV2::Storage>();
    result->build_id = build_id;
    result->dependency_lock = dependency_lock;
    for (const auto &[unused, artifact] : artifacts) {
        static_cast<void>(unused);
        const auto schema = schema_for_kind(artifact.id.kind());
        const auto path = artifact_path(artifact.id);
        const auto producer_build_value = library_producer_name(artifact.id.kind()) == producer_name
                                              ? build_id.content_hash()
                                              : artifact.producer_build;
        result->descriptors.emplace_back(
            artifact.id, role_for_kind(artifact.id.kind()), artifact.id.kind(), schema.format,
            schema.version, schema.media_type, RelativeBundlePath{path},
            sha256_content_hash(artifact.bytes), artifact.dependencies,
            library_producer_name(artifact.id.kind()), producer_version, producer_build_value);
        result->files.emplace(path, artifact.bytes);
    }
    std::ranges::sort(result->descriptors, {},
                      [](const auto &descriptor) { return artifact_key(descriptor.id()); });

    auto stage_array = Json::array();
    for (const auto &stage : run.stages) {
        stage_array.push_back(stage);
    }
    auto descriptor_array = Json::array();
    for (const auto &descriptor : result->descriptors) {
        const auto library_produced = library_producer_name(descriptor.kind()) != producer_name;
        const auto producer_build_value =
            library_produced ? descriptor.kind() == ArtifactKind::StepAsset
                                   ? artifacts.at(artifact_key(descriptor.id())).producer_build
                                   : std::visit(
                                         [&](const auto &owner) -> ContentHash {
                                             using T = std::decay_t<decltype(owner)>;
                                             if constexpr (std::same_as<T, LibraryComponentRef> ||
                                                           std::same_as<T, LibraryAttachmentRef> ||
                                                           std::same_as<T, LibraryAssetRef>) {
                                                 return owner.library_bundle_digest;
                                             } else if constexpr (std::same_as<T, LibraryPartRef>) {
                                                 return owner.library_digest();
                                             } else {
                                                 reject("library artifact has a non-library owner");
                                             }
                                         },
                                         descriptor.id().owner())
                             : build_id.content_hash();
        descriptor_array.push_back(descriptor_json(descriptor,
                                                   library_producer_name(descriptor.kind()),
                                                   producer_version, producer_build_value));
    }
    auto manifest_core =
        Json{{"format", format_name},
             {"schema_version", 2},
             {"project", project_json},
             {"run", Json{{"ok", run.ok},
                          {"status", status_name(run.status)},
                          {"profile", run.profile},
                          {"stages", std::move(stage_array)}}},
             {"authoring_inputs", Json{{"entrypoint", entrypoint.value()},
                                       {"records", validated.authoring_records},
                                       {"digest", validated.authoring_digest.value()}}},
             {"build_id", build_id.content_hash().value()},
             {"dependency_lock", dependency_lock_value},
             {"export_selection", graph.export_selection()},
             {"artifacts", std::move(descriptor_array)}};
    const auto bundle_digest = sha256_content_hash(canonical(manifest_core));
    auto manifest = manifest_core;
    auto ordered_manifest = Json{
        {"format", manifest.at("format")},
        {"schema_version", manifest.at("schema_version")},
        {"project", manifest.at("project")},
        {"run", manifest.at("run")},
        {"authoring_inputs", manifest.at("authoring_inputs")},
        {"build_id", manifest.at("build_id")},
        {"bundle_digest", bundle_digest.value()},
        {"dependency_lock", manifest.at("dependency_lock")},
        {"export_selection", manifest.at("export_selection")},
        {"artifacts", manifest.at("artifacts")},
    };
    result->bundle_digest = bundle_digest;
    result->manifest = canonical(ordered_manifest);
    result->files.emplace(std::string{manifest_path}, result->manifest);

    require(result->files.size() == result->descriptors.size() + 1U,
            "publication contains an undeclared or colliding path");
    auto folded_paths = std::set<std::string>{};
    for (const auto &[path, unused] : result->files) {
        static_cast<void>(unused);
        require(safe_bundle_path(path), "publication contains an unsafe path");
        auto folded = path;
        std::ranges::transform(folded, folded.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        require(folded_paths.insert(std::move(folded)).second,
                "publication paths collide under ASCII case folding");
    }
    return ProjectBundleV2{std::move(result)};
}

} // namespace

namespace detail {

std::string project_bundle_v2_artifact_id_json(const ArtifactId &id) {
    return canonical(artifact_id_json(id));
}

std::string project_bundle_v2_artifact_ref_json(const ArtifactRef &reference) {
    return canonical(artifact_ref_json(reference));
}

std::string project_bundle_v2_artifact_key(const ArtifactId &id) { return artifact_key(id); }

std::string project_bundle_v2_artifact_path(const ArtifactId &id) { return artifact_path(id); }

bool project_bundle_v2_safe_path(std::string_view path) { return safe_bundle_path(path); }

ArtifactRole project_bundle_v2_role_for_kind(ArtifactKind kind) { return role_for_kind(kind); }

ProjectBundleV2SchemaInfo project_bundle_v2_schema_for_kind(ArtifactKind kind) {
    const auto schema = schema_for_kind(kind);
    return {schema.format, schema.version, schema.media_type};
}

std::string project_bundle_v2_producer_for_kind(ArtifactKind kind) {
    return library_producer_name(kind);
}

std::string project_bundle_v2_export_request_json(const ExportRequest &request) {
    return canonical(export_request_json(request));
}

} // namespace detail

ProjectBundleV2 ProjectBundleV2Builder::build() const {
    const auto validated =
        validate_project_build(storage_->run, storage_->entrypoint, storage_->inputs,
                               storage_->diagnostics, storage_->tests, storage_->logicals);
    auto graph = ProjectArtifactGraph{};
    graph.accumulate_authoritative_artifacts(validated, storage_->logicals, storage_->schematics,
                                             storage_->boards, storage_->diagnostics,
                                             storage_->tests);
    graph.materialize_selected_exports(validated, storage_->schematics, storage_->boards,
                                       storage_->exports);
    graph.validate();
    return encode_project_bundle(storage_->project, storage_->run, storage_->entrypoint, validated,
                                 graph);
}

} // namespace volt::io
