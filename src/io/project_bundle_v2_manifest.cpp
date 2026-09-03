#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "project_bundle_v2_contract.hpp"
#include "project_bundle_v2_internal.hpp"

namespace volt::io::v2_open {

[[nodiscard]] std::string canonical_dependency_lock(const DependencyLock &lock);

[[noreturn]] void fail(ProjectBundleOpenErrorCode code, std::string message) {
    throw ProjectBundleOpenError{code, "ProjectBundle v3: " + std::move(message)};
}

void require(bool condition, ProjectBundleOpenErrorCode code, std::string message) {
    if (!condition) {
        fail(code, std::move(message));
    }
}

[[nodiscard]] std::string canonical(const Json &value) {
    return value.dump(-1, ' ', false, Json::error_handler_t::strict);
}

[[nodiscard]] Json parse_json(std::string_view bytes, std::string_view label,
                              ProjectBundleOpenErrorCode code) {
    auto object_keys = std::vector<std::set<std::string>>{};
    const auto callback = [&](int, Json::parse_event_t event, Json &parsed) {
        if (event == Json::parse_event_t::object_start) {
            object_keys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
            if (object_keys.empty() ||
                !object_keys.back().insert(parsed.get<std::string>()).second) {
                fail(code, std::string{label} + " contains a duplicate JSON object key");
            }
        } else if (event == Json::parse_event_t::object_end) {
            object_keys.pop_back();
        }
        return true;
    };
    try {
        return Json::parse(bytes.begin(), bytes.end(), callback);
    } catch (const ProjectBundleOpenError &) {
        throw;
    } catch (const Json::exception &error) {
        fail(code, std::string{label} + " is malformed JSON: " + error.what());
    }
}

void require_keys(const Json &object, std::initializer_list<std::string_view> expected,
                  std::string_view label) {
    require(object.is_object(), ProjectBundleOpenErrorCode::MalformedManifest,
            std::string{label} + " must be an object");
    require(object.size() == expected.size(), ProjectBundleOpenErrorCode::MalformedManifest,
            std::string{label} + " has unknown, missing, or duplicate fields");
    auto actual = object.begin();
    for (const auto key : expected) {
        require(actual != object.end() && actual.key() == key,
                ProjectBundleOpenErrorCode::MalformedManifest,
                std::string{label} + " fields are not in canonical order");
        ++actual;
    }
}

void require_key_set(const Json &object, std::initializer_list<std::string_view> expected,
                     std::string_view label) {
    require(object.is_object(), ProjectBundleOpenErrorCode::ModelDecodeFailure,
            std::string{label} + " must be an object");
    auto required = std::set<std::string_view>{expected};
    require(object.size() == required.size(), ProjectBundleOpenErrorCode::ModelDecodeFailure,
            std::string{label} + " has unknown or missing fields");
    for (const auto &[key, unused] : object.items()) {
        static_cast<void>(unused);
        require(required.contains(key), ProjectBundleOpenErrorCode::ModelDecodeFailure,
                std::string{label} + " has an unknown field");
    }
}

[[nodiscard]] const Json &field(const Json &object, std::string_view key, std::string_view label) {
    require(object.contains(key), ProjectBundleOpenErrorCode::MalformedManifest,
            std::string{label} + " is missing " + std::string{key});
    return object.at(key);
}

[[nodiscard]] std::string string_field(const Json &object, std::string_view key,
                                       std::string_view label) {
    const auto &value = field(object, key, label);
    require(value.is_string(), ProjectBundleOpenErrorCode::MalformedManifest,
            std::string{label} + "." + std::string{key} + " must be a string");
    return value.get<std::string>();
}

[[nodiscard]] std::uint32_t u32_field(const Json &object, std::string_view key,
                                      std::string_view label) {
    const auto &value = field(object, key, label);
    require(value.is_number_unsigned() &&
                value.get<std::uint64_t>() <= std::numeric_limits<std::uint32_t>::max(),
            ProjectBundleOpenErrorCode::MalformedManifest,
            std::string{label} + "." + std::string{key} + " must be an unsigned 32-bit integer");
    return value.get<std::uint32_t>();
}

[[nodiscard]] std::optional<std::string>
nullable_string_field(const Json &object, std::string_view key, std::string_view label) {
    const auto &value = field(object, key, label);
    if (value.is_null()) {
        return std::nullopt;
    }
    require(value.is_string(), ProjectBundleOpenErrorCode::MalformedManifest,
            std::string{label} + "." + std::string{key} + " must be a string or null");
    return value.get<std::string>();
}

[[nodiscard]] ContentHash hash_field(const Json &object, std::string_view key,
                                     std::string_view label) {
    try {
        return ContentHash{string_field(object, key, label)};
    } catch (const std::exception &error) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             std::string{label} + "." + std::string{key} +
                 " is not a canonical content hash: " + error.what());
    }
}

[[nodiscard]] ArtifactKind artifact_kind(std::string_view value) {
    constexpr auto values = std::array{
        std::pair{"logical_model", ArtifactKind::LogicalModel},
        std::pair{"schematic_model", ArtifactKind::SchematicModel},
        std::pair{"board_model", ArtifactKind::BoardModel},
        std::pair{"compiled_board", ArtifactKind::CompiledBoard},
        std::pair{"component_definition", ArtifactKind::ComponentDefinition},
        std::pair{"part_definition", ArtifactKind::PartDefinition},
        std::pair{"symbol_definition", ArtifactKind::SymbolDefinition},
        std::pair{"footprint_definition", ArtifactKind::FootprintDefinition},
        std::pair{"board_scene", ArtifactKind::BoardScene},
        std::pair{"glb_asset", ArtifactKind::GlbAsset},
        std::pair{"diagnostics", ArtifactKind::Diagnostics},
        std::pair{"project_tests", ArtifactKind::ProjectTests},
        std::pair{"schematic_svg", ArtifactKind::SchematicSvg},
        std::pair{"board_svg", ArtifactKind::BoardSvg},
        std::pair{"board_layer_image", ArtifactKind::BoardLayerImage},
        std::pair{"kicad_pcb", ArtifactKind::KicadPcb},
        std::pair{"bom", ArtifactKind::Bom},
        std::pair{"cpl", ArtifactKind::Cpl},
        std::pair{"fabrication_package", ArtifactKind::FabricationPackage},
        std::pair{"step_asset", ArtifactKind::StepAsset},
        std::pair{"whole_board_glb", ArtifactKind::WholeBoardGlb},
        std::pair{"evidence_asset", ArtifactKind::EvidenceAsset},
    };
    const auto match = std::ranges::find(values, value, &decltype(values)::value_type::first);
    if (match == values.end()) {
        fail(ProjectBundleOpenErrorCode::UnsupportedFormat,
             "artifact kind is unknown: " + std::string{value});
    }
    return match->second;
}

[[nodiscard]] ArtifactRole artifact_role(std::string_view value) {
    constexpr auto values = std::array{
        std::pair{"model", ArtifactRole::Model},       std::pair{"view", ArtifactRole::View},
        std::pair{"render", ArtifactRole::Render},     std::pair{"report", ArtifactRole::Report},
        std::pair{"adapter", ArtifactRole::Adapter},   std::pair{"asset", ArtifactRole::Asset},
        std::pair{"delivery", ArtifactRole::Delivery},
    };
    const auto match = std::ranges::find(values, value, &decltype(values)::value_type::first);
    if (match == values.end()) {
        fail(ProjectBundleOpenErrorCode::UnsupportedFormat,
             "artifact role is unknown: " + std::string{value});
    }
    return match->second;
}

[[nodiscard]] CompiledBoardIdentity compiled_identity(const Json &value, std::string_view label) {
    require_keys(value, {"board", "provenance_digest"}, label);
    return CompiledBoardIdentity{BoardName{string_field(value, "board", label)},
                                 hash_field(value, "provenance_digest", label)};
}

[[nodiscard]] LibraryPartRef library_part_ref(const Json &value, std::string_view label) {
    require_keys(value,
                 {"library_namespace", "library_version", "part_key", "library_bundle_digest",
                  "part_digest"},
                 label);
    return LibraryPartRef{string_field(value, "library_namespace", label),
                          string_field(value, "library_version", label),
                          PartKey{string_field(value, "part_key", label)},
                          hash_field(value, "library_bundle_digest", label),
                          hash_field(value, "part_digest", label)};
}

[[nodiscard]] LibraryAssetKind library_asset_kind(std::string_view value) {
    if (value == "glb") {
        return LibraryAssetKind::Glb;
    }
    if (value == "step") {
        return LibraryAssetKind::Step;
    }
    if (value == "evidence") {
        return LibraryAssetKind::Evidence;
    }
    fail(ProjectBundleOpenErrorCode::UnsupportedFormat,
         "library asset kind is unknown: " + std::string{value});
}

[[nodiscard]] LibraryAssetRef library_asset_ref(const Json &value, std::string_view label) {
    require_keys(value,
                 {"library_namespace", "library_version", "media_kind", "library_bundle_digest",
                  "content_digest"},
                 label);
    return LibraryAssetRef{
        string_field(value, "library_namespace", label),
        string_field(value, "library_version", label),
        library_asset_kind(string_field(value, "media_kind", label)),
        hash_field(value, "library_bundle_digest", label),
        hash_field(value, "content_digest", label),
    };
}

[[nodiscard]] ExportOutputKey export_output_key(const Json &object, std::string_view label) {
    require_keys(object, {"type", "value"}, label);
    const auto type = string_field(object, "type", label);
    const auto &value = field(object, "value", label);
    if (type == "primary_export_output_key") {
        require(value.is_string() && value.get<std::string>() == "primary",
                ProjectBundleOpenErrorCode::MalformedManifest,
                std::string{label} + " has an invalid primary key");
        return PrimaryExportOutputKey::Primary;
    }
    if (type == "board_layer_key") {
        require(value.is_string(), ProjectBundleOpenErrorCode::MalformedManifest,
                std::string{label} + " board layer value must be a string");
        return BoardLayerKey{value.get<std::string>()};
    }
    if (type == "library_asset_ref") {
        return library_asset_ref(value, label);
    }
    fail(ProjectBundleOpenErrorCode::UnsupportedFormat,
         std::string{label} + " type is unknown: " + type);
}

[[nodiscard]] ArtifactOwnerIdentity artifact_owner(const Json &object, ArtifactKind kind,
                                                   std::string_view label) {
    require_keys(object, {"type", "value"}, label);
    const auto type = string_field(object, "type", label);
    const auto &value = field(object, "value", label);
    if (type == "logical_artifact_identity") {
        require_keys(value, {"design"}, label);
        return LogicalArtifactIdentity{DesignKey{string_field(value, "design", label)}};
    }
    if (type == "schematic_artifact_identity") {
        require_keys(value, {"design", "schematic"}, label);
        return SchematicArtifactIdentity{DesignKey{string_field(value, "design", label)},
                                         SchematicKey{string_field(value, "schematic", label)}};
    }
    if (type == "board_artifact_identity") {
        require_keys(value, {"design", "board"}, label);
        return BoardArtifactIdentity{DesignKey{string_field(value, "design", label)},
                                     BoardName{string_field(value, "board", label)}};
    }
    if (type == "compiled_board_identity") {
        return compiled_identity(value, label);
    }
    if (type == "board_scene_artifact_identity") {
        require_keys(value, {"compiled_board"}, label);
        return BoardSceneArtifactIdentity{
            compiled_identity(field(value, "compiled_board", label), label)};
    }
    if (type == "project_singleton_identity") {
        require_keys(value, {"key"}, label);
        require(string_field(value, "key", label) == "primary",
                ProjectBundleOpenErrorCode::MalformedManifest,
                "project singleton key is not primary");
        return ProjectSingletonIdentity{ProjectSingletonKey::Primary};
    }
    if (type == "library_component_ref") {
        require_keys(value,
                     {"library_namespace", "library_version", "component_key",
                      "library_bundle_digest", "component_digest"},
                     label);
        return LibraryComponentRef{
            string_field(value, "library_namespace", label),
            string_field(value, "library_version", label),
            ComponentKey{string_field(value, "component_key", label)},
            hash_field(value, "library_bundle_digest", label),
            hash_field(value, "component_digest", label),
        };
    }
    if (type == "library_part_ref") {
        return library_part_ref(value, label);
    }
    if (type == "library_attachment_ref") {
        require_keys(value,
                     {"library_namespace", "library_version", "attachment_kind", "key",
                      "library_bundle_digest", "content_digest"},
                     label);
        const auto attachment_kind = string_field(value, "attachment_kind", label);
        require(attachment_kind == "symbol" || attachment_kind == "footprint",
                ProjectBundleOpenErrorCode::UnsupportedFormat,
                "library attachment kind is unknown");
        return LibraryAttachmentRef{
            string_field(value, "library_namespace", label),
            string_field(value, "library_version", label),
            attachment_kind == "symbol" ? LibraryAttachmentKind::Symbol
                                        : LibraryAttachmentKind::Footprint,
            string_field(value, "key", label),
            hash_field(value, "library_bundle_digest", label),
            hash_field(value, "content_digest", label),
        };
    }
    if (type == "library_asset_ref") {
        return library_asset_ref(value, label);
    }
    if (type == "export_artifact_identity") {
        require_keys(value, {"request_digest", "output_key"}, label);
        return ExportArtifactIdentity{
            hash_field(value, "request_digest", label),
            export_output_key(field(value, "output_key", label), label),
        };
    }
    fail(ProjectBundleOpenErrorCode::UnsupportedFormat, "artifact owner type is unknown: " + type +
                                                            " for " +
                                                            std::string{artifact_kind_name(kind)});
}

[[nodiscard]] ArtifactId artifact_id(const Json &object, std::string_view label) {
    require_keys(object, {"kind", "owner"}, label);
    const auto kind = artifact_kind(string_field(object, "kind", label));
    try {
        return ArtifactId{kind, artifact_owner(field(object, "owner", label), kind, label)};
    } catch (const ProjectBundleOpenError &) {
        throw;
    } catch (const std::exception &error) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             std::string{label} + " is not a valid typed ArtifactId: " + error.what());
    }
}

[[nodiscard]] ArtifactRef artifact_ref(const Json &object, std::string_view label) {
    require_keys(object, {"artifact", "content_digest"}, label);
    return ArtifactRef{artifact_id(field(object, "artifact", label), label),
                       hash_field(object, "content_digest", label)};
}

[[nodiscard]] ArtifactDescriptor descriptor(const Json &object) {
    constexpr auto label = std::string_view{"artifact descriptor"};
    require_keys(object,
                 {"id", "role", "kind", "schema", "media_type", "path", "content_digest",
                  "depends_on", "producer"},
                 label);
    const auto id = artifact_id(field(object, "id", label), label);
    const auto role = artifact_role(string_field(object, "role", label));
    const auto kind = artifact_kind(string_field(object, "kind", label));
    const auto &schema = field(object, "schema", label);
    require_keys(schema, {"format", "version"}, "artifact schema");
    const auto &dependencies = field(object, "depends_on", label);
    require(dependencies.is_array(), ProjectBundleOpenErrorCode::MalformedManifest,
            "artifact depends_on must be an array");
    auto parsed_dependencies = std::vector<ArtifactRef>{};
    parsed_dependencies.reserve(dependencies.size());
    auto previous = std::optional<std::string>{};
    for (const auto &dependency : dependencies) {
        auto parsed = artifact_ref(dependency, "artifact dependency");
        const auto key = detail::project_bundle_v2_artifact_ref_json(parsed);
        require(!previous.has_value() || *previous < key,
                ProjectBundleOpenErrorCode::DuplicateIdentity,
                "artifact dependencies are duplicated or not canonically ordered");
        previous = key;
        parsed_dependencies.push_back(std::move(parsed));
    }
    const auto &producer = field(object, "producer", label);
    require_keys(producer, {"name", "version", "build"}, "artifact producer");
    try {
        return ArtifactDescriptor{
            id,
            role,
            kind,
            string_field(schema, "format", "artifact schema"),
            u32_field(schema, "version", "artifact schema"),
            string_field(object, "media_type", label),
            RelativeBundlePath{string_field(object, "path", label)},
            hash_field(object, "content_digest", label),
            std::move(parsed_dependencies),
            string_field(producer, "name", "artifact producer"),
            u32_field(producer, "version", "artifact producer"),
            hash_field(producer, "build", "artifact producer"),
        };
    } catch (const ProjectBundleOpenError &) {
        throw;
    } catch (const std::exception &error) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             "artifact descriptor violates its closed typed contract: " +
                 std::string{error.what()});
    }
}

[[nodiscard]] DependencyLock dependency_lock(const Json &object) {
    require_keys(object, {"libraries", "selected_parts"}, "dependency lock");
    const auto &library_values = field(object, "libraries", "dependency lock");
    const auto &selection_values = field(object, "selected_parts", "dependency lock");
    require(library_values.is_array() && selection_values.is_array(),
            ProjectBundleOpenErrorCode::MalformedManifest,
            "dependency lock members must be arrays");
    auto libraries = std::vector<LockedLibrary>{};
    for (const auto &library : library_values) {
        require_keys(library, {"library", "version", "library_bundle_digest"}, "locked library");
        libraries.push_back(
            LockedLibrary{string_field(library, "library", "locked library"),
                          string_field(library, "version", "locked library"),
                          hash_field(library, "library_bundle_digest", "locked library")});
    }
    auto selections = std::vector<LockedPartSelection>{};
    for (const auto &selection : selection_values) {
        require_keys(selection, {"selected_part", "vendored_part"}, "locked selection");
        selections.push_back(LockedPartSelection{
            library_part_ref(field(selection, "selected_part", "locked selection"),
                             "locked selected part"),
            artifact_ref(field(selection, "vendored_part", "locked selection"),
                         "locked vendored part"),
        });
    }
    try {
        auto result = DependencyLock{std::move(libraries), std::move(selections)};
        require(canonical(object) == canonical_dependency_lock(result),
                ProjectBundleOpenErrorCode::MalformedManifest,
                "dependency lock is not canonically ordered");
        return result;
    } catch (const ProjectBundleOpenError &) {
        throw;
    } catch (const std::exception &error) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             "dependency lock is invalid: " + std::string{error.what()});
    }
}

[[nodiscard]] Json library_part_ref_json(const LibraryPartRef &reference) {
    return Json{{"library_namespace", reference.library_namespace()},
                {"library_version", reference.library_version()},
                {"part_key", reference.part_key().value()},
                {"library_bundle_digest", reference.library_digest().value()},
                {"part_digest", reference.part_digest().value()}};
}

[[nodiscard]] Json canonical_dependency_lock_json(const DependencyLock &lock) {
    auto libraries = Json::array();
    for (const auto &library : lock.libraries()) {
        libraries.push_back(Json{{"library", library.library_namespace},
                                 {"version", library.library_version},
                                 {"library_bundle_digest", library.library_bundle_digest.value()}});
    }
    auto selections = Json::array();
    for (const auto &selection : lock.selected_parts()) {
        selections.push_back(Json{
            {"selected_part", library_part_ref_json(selection.selected_part)},
            {"vendored_part",
             Json::parse(detail::project_bundle_v2_artifact_ref_json(selection.vendored_part))}});
    }
    return Json{{"libraries", std::move(libraries)}, {"selected_parts", std::move(selections)}};
}

[[nodiscard]] std::string canonical_dependency_lock(const DependencyLock &lock) {
    return canonical(canonical_dependency_lock_json(lock));
}

[[nodiscard]] ExportKind export_kind(std::string_view value) {
    constexpr auto values = std::array{
        std::pair{"schematic_svg", ExportKind::SchematicSvg},
        std::pair{"board_svg", ExportKind::BoardSvg},
        std::pair{"board_layer_image", ExportKind::BoardLayerImage},
        std::pair{"kicad_pcb", ExportKind::KicadPcb},
        std::pair{"bom", ExportKind::Bom},
        std::pair{"cpl", ExportKind::Cpl},
        std::pair{"fabrication", ExportKind::Fabrication},
        std::pair{"step", ExportKind::Step},
        std::pair{"whole_board_glb", ExportKind::WholeBoardGlb},
    };
    const auto match = std::ranges::find(values, value, &decltype(values)::value_type::first);
    if (match == values.end()) {
        fail(ProjectBundleOpenErrorCode::UnsupportedFormat,
             "export kind is unknown: " + std::string{value});
    }
    return match->second;
}

[[nodiscard]] ExportTarget export_target(const Json &object) {
    require_keys(object, {"type", "value"}, "export target");
    const auto type = string_field(object, "type", "export target");
    const auto &value = field(object, "value", "export target");
    if (type == "model_export_target") {
        require_keys(value, {"model"}, "model export target");
        return ModelExportTarget{
            artifact_ref(field(value, "model", "model export target"), "export model")};
    }
    if (type == "board_layer_export_target") {
        require_keys(value, {"compiled_board", "layer"}, "board layer export target");
        return BoardLayerExportTarget{
            artifact_ref(field(value, "compiled_board", "board layer export target"),
                         "export compiled board"),
            BoardLayerKey{string_field(value, "layer", "board layer export target")}};
    }
    if (type == "library_asset_export_target") {
        require_keys(value, {"selected_part", "asset"}, "library asset export target");
        return LibraryAssetExportTarget{
            artifact_ref(field(value, "selected_part", "library asset export target"),
                         "export selected part"),
            library_asset_ref(field(value, "asset", "library asset export target"),
                              "export library asset")};
    }
    fail(ProjectBundleOpenErrorCode::UnsupportedFormat, "export target type is unknown: " + type);
}

[[nodiscard]] ExportParameters export_parameters(const Json &object) {
    require_keys(object, {"type", "value"}, "export parameters");
    const auto type = string_field(object, "type", "export parameters");
    const auto &value = field(object, "value", "export parameters");
    require(value.is_object() && value.empty(), ProjectBundleOpenErrorCode::MalformedManifest,
            "export parameter value must be the closed empty object");
    if (type == "no_export_parameters") {
        return NoExportParameters{};
    }
    if (type == "schematic_svg_parameters") {
        return SchematicSvgParameters{};
    }
    if (type == "board_svg_parameters") {
        return BoardSvgParameters{};
    }
    if (type == "board_layer_image_parameters") {
        return BoardLayerImageParameters{};
    }
    if (type == "kicad_pcb_parameters") {
        return KicadPcbParameters{};
    }
    if (type == "bom_parameters") {
        return BomParameters{};
    }
    if (type == "cpl_parameters") {
        return CplParameters{};
    }
    if (type == "fabrication_parameters") {
        return FabricationParameters{};
    }
    if (type == "step_parameters") {
        return StepParameters{};
    }
    if (type == "whole_board_glb_parameters") {
        return WholeBoardGlbParameters{};
    }
    fail(ProjectBundleOpenErrorCode::UnsupportedFormat,
         "export parameter type is unknown: " + type);
}

void require_export_shape(const ExportRequest &request) {
    const auto model_kind = [&]() -> std::optional<ArtifactKind> {
        if (!std::holds_alternative<ModelExportTarget>(request.target)) {
            return std::nullopt;
        }
        return std::get<ModelExportTarget>(request.target).model.artifact().kind();
    };
    switch (request.kind) {
    case ExportKind::SchematicSvg:
        require(model_kind() == ArtifactKind::SchematicModel &&
                    std::holds_alternative<SchematicSvgParameters>(request.parameters),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "Schematic SVG request has an incompatible target or parameters");
        return;
    case ExportKind::BoardSvg:
        require(model_kind() == ArtifactKind::CompiledBoard &&
                    std::holds_alternative<BoardSvgParameters>(request.parameters),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "Board SVG request has an incompatible target or parameters");
        return;
    case ExportKind::BoardLayerImage:
        require(
            std::holds_alternative<BoardLayerExportTarget>(request.target) &&
                std::get<BoardLayerExportTarget>(request.target).compiled_board.artifact().kind() ==
                    ArtifactKind::CompiledBoard &&
                std::holds_alternative<BoardLayerImageParameters>(request.parameters),
            ProjectBundleOpenErrorCode::OwnershipViolation,
            "Board layer request has an incompatible target or parameters");
        return;
    case ExportKind::Bom:
        require(model_kind() == ArtifactKind::LogicalModel &&
                    std::holds_alternative<BomParameters>(request.parameters),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "BOM request has an incompatible target or parameters");
        return;
    case ExportKind::Cpl:
        require(model_kind() == ArtifactKind::CompiledBoard &&
                    std::holds_alternative<CplParameters>(request.parameters),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "CPL request has an incompatible target or parameters");
        return;
    case ExportKind::Step:
        require(std::holds_alternative<LibraryAssetExportTarget>(request.target) &&
                    std::get<LibraryAssetExportTarget>(request.target)
                            .selected_part.artifact()
                            .kind() == ArtifactKind::PartDefinition &&
                    std::get<LibraryAssetExportTarget>(request.target).asset.kind ==
                        LibraryAssetKind::Step &&
                    std::holds_alternative<StepParameters>(request.parameters),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "STEP request has an incompatible target or parameters");
        return;
    case ExportKind::KicadPcb:
    case ExportKind::Fabrication:
    case ExportKind::WholeBoardGlb:
        fail(ProjectBundleOpenErrorCode::UnsupportedFormat,
             "export kind cannot be emitted by the v3 producer contract");
    }
}

[[nodiscard]] ExportRequest export_request(const Json &object) {
    require_keys(object, {"kind", "target", "request_schema", "parameters"}, "export request");
    const auto &schema = field(object, "request_schema", "export request");
    require_keys(schema, {"format", "version"}, "export request schema");
    require(string_field(schema, "format", "export request schema") == "volt.export-request" &&
                u32_field(schema, "version", "export request schema") == 1U,
            ProjectBundleOpenErrorCode::UnsupportedFormat, "export request schema is unsupported");
    auto result = ExportRequest{
        export_kind(string_field(object, "kind", "export request")),
        export_target(field(object, "target", "export request")),
        ExportRequestSchema{},
        export_parameters(field(object, "parameters", "export request")),
    };
    require_export_shape(result);
    require(detail::project_bundle_v2_export_request_json(result) == canonical(object),
            ProjectBundleOpenErrorCode::MalformedManifest,
            "export request is not canonical or violates its kind-shaped contract");
    return result;
}

[[nodiscard]] ExportSelection export_selection(const Json &value) {
    require(value.is_array(), ProjectBundleOpenErrorCode::MalformedManifest,
            "export_selection must be an array");
    auto result = ExportSelection{};
    auto previous = std::optional<std::string>{};
    for (const auto &request : value) {
        auto parsed = export_request(request);
        const auto digest =
            sha256_content_hash(detail::project_bundle_v2_export_request_json(parsed)).value();
        require(!previous.has_value() || *previous < digest,
                ProjectBundleOpenErrorCode::DuplicateIdentity,
                "export selection is duplicated or not canonically ordered");
        previous = digest;
        result.push_back(std::move(parsed));
    }
    return result;
}

[[nodiscard]] ProjectStatus project_status(std::string_view value) {
    if (value == "clean") {
        return ProjectStatus::Clean;
    }
    if (value == "expected-diagnostics") {
        return ProjectStatus::ExpectedDiagnostics;
    }
    if (value == "failed") {
        return ProjectStatus::Failed;
    }
    fail(ProjectBundleOpenErrorCode::UnsupportedFormat,
         "project run status is unknown: " + std::string{value});
}

[[nodiscard]] ParsedManifest parse_manifest(std::string_view bytes) {
    auto root = parse_json(bytes, "manifest", ProjectBundleOpenErrorCode::MalformedManifest);
    require_keys(root,
                 {"format", "schema_version", "project", "run", "authoring_inputs", "build_id",
                  "bundle_digest", "dependency_lock", "export_selection", "artifacts"},
                 "manifest");
    require(string_field(root, "format", "manifest") == detail::project_bundle_v2_format,
            ProjectBundleOpenErrorCode::UnsupportedFormat, "manifest format is unsupported");
    require(u32_field(root, "schema_version", "manifest") == 3U,
            ProjectBundleOpenErrorCode::UnsupportedSchema, "manifest schema is unsupported");
    require(canonical(root) == bytes, ProjectBundleOpenErrorCode::MalformedManifest,
            "manifest bytes are not the canonical v3 representation");

    const auto &project = field(root, "project", "manifest");
    require_keys(project, {"name", "version", "description"}, "project");
    auto project_value = ProjectIdentity{string_field(project, "name", "project"),
                                         nullable_string_field(project, "version", "project"),
                                         nullable_string_field(project, "description", "project")};
    require(!project_value.name.empty(), ProjectBundleOpenErrorCode::MalformedManifest,
            "project name must not be empty");

    const auto &run = field(root, "run", "manifest");
    require_keys(run, {"ok", "status", "profile", "stages"}, "run");
    require(field(run, "ok", "run").is_boolean(), ProjectBundleOpenErrorCode::MalformedManifest,
            "run.ok must be boolean");
    require(field(run, "stages", "run").is_array(), ProjectBundleOpenErrorCode::MalformedManifest,
            "run.stages must be an array");
    auto stages = std::vector<std::string>{};
    auto stage_names = std::set<std::string>{};
    for (const auto &stage : field(run, "stages", "run")) {
        require(stage.is_string() && !stage.get_ref<const std::string &>().empty(),
                ProjectBundleOpenErrorCode::MalformedManifest,
                "run stage must be a non-empty string");
        auto value = stage.get<std::string>();
        require(stage_names.insert(value).second, ProjectBundleOpenErrorCode::DuplicateIdentity,
                "run stages contain a duplicate");
        stages.push_back(std::move(value));
    }
    auto run_value = ProjectRunSummary{field(run, "ok", "run").get<bool>(),
                                       project_status(string_field(run, "status", "run")),
                                       string_field(run, "profile", "run"), std::move(stages)};
    require(
        !run_value.profile.empty() && run_value.ok == (run_value.status != ProjectStatus::Failed),
        ProjectBundleOpenErrorCode::MalformedManifest, "run summary is internally inconsistent");

    const auto &authoring = field(root, "authoring_inputs", "manifest");
    require_keys(authoring, {"entrypoint", "records", "digest"}, "authoring inputs");
    const auto entrypoint =
        LogicalInputName{string_field(authoring, "entrypoint", "authoring inputs")};
    const auto &records = field(authoring, "records", "authoring inputs");
    require(records.is_array(), ProjectBundleOpenErrorCode::MalformedManifest,
            "authoring input records must be an array");
    auto previous_record = std::optional<std::tuple<std::string, std::string, std::string>>{};
    auto input_names = std::set<std::pair<std::string, std::string>>{};
    auto entrypoint_count = std::size_t{0};
    for (const auto &record : records) {
        require_keys(record, {"kind", "name", "content_digest"}, "authoring input");
        const auto kind = string_field(record, "kind", "authoring input");
        require(kind == "project_source" || kind == "declared_input",
                ProjectBundleOpenErrorCode::UnsupportedFormat, "authoring input kind is unknown");
        const auto name = LogicalInputName{string_field(record, "name", "authoring input")};
        const auto digest = hash_field(record, "content_digest", "authoring input");
        const auto key = std::tuple{kind, name.value(), digest.value()};
        require(!previous_record.has_value() || *previous_record < key,
                ProjectBundleOpenErrorCode::DuplicateIdentity,
                "authoring input records are duplicated or not canonically ordered");
        previous_record = key;
        require(input_names.emplace(kind, name.value()).second,
                ProjectBundleOpenErrorCode::DuplicateIdentity,
                "authoring input records contain a duplicate kind/name pair");
        if (kind == "project_source" && name.value() == entrypoint.value()) {
            ++entrypoint_count;
        }
    }
    require(entrypoint_count == 1U, ProjectBundleOpenErrorCode::MalformedManifest,
            "authoring entrypoint does not name exactly one project_source record");
    const auto authoring_core = Json{{"entrypoint", entrypoint.value()}, {"records", records}};
    const auto authoring_digest = hash_field(authoring, "digest", "authoring inputs");
    require(sha256_content_hash(canonical(authoring_core)) == authoring_digest,
            ProjectBundleOpenErrorCode::DigestMismatch,
            "authoring input digest does not match its canonical records");

    const auto lock = dependency_lock(field(root, "dependency_lock", "manifest"));
    const auto exports = export_selection(field(root, "export_selection", "manifest"));
    const auto &artifact_values = field(root, "artifacts", "manifest");
    require(artifact_values.is_array(), ProjectBundleOpenErrorCode::MalformedManifest,
            "manifest artifacts must be an array");
    auto artifacts = std::vector<ArtifactDescriptor>{};
    artifacts.reserve(artifact_values.size());
    auto previous_artifact = std::optional<std::string>{};
    for (const auto &artifact : artifact_values) {
        auto parsed = descriptor(artifact);
        const auto key = detail::project_bundle_v2_artifact_key(parsed.id());
        require(!previous_artifact.has_value() || *previous_artifact < key,
                ProjectBundleOpenErrorCode::DuplicateIdentity,
                "manifest artifacts are duplicated or not canonically ordered");
        previous_artifact = key;
        artifacts.push_back(std::move(parsed));
    }
    require(!artifacts.empty(), ProjectBundleOpenErrorCode::MalformedManifest,
            "manifest contains no artifacts");

    const auto build_id = BuildId{hash_field(root, "build_id", "manifest")};
    const auto bundle_digest = hash_field(root, "bundle_digest", "manifest");
    return ParsedManifest{std::move(root),
                          std::move(project_value),
                          std::move(run_value),
                          authoring_digest,
                          build_id,
                          bundle_digest,
                          lock,
                          exports,
                          std::move(artifacts)};
}

[[nodiscard]] bool is_export_artifact(const ArtifactDescriptor &artifact) {
    return std::holds_alternative<ExportArtifactIdentity>(artifact.id().owner());
}

void verify_build_and_bundle_digests(const ParsedManifest &manifest) {
    auto required = Json::array();
    for (const auto &artifact : manifest.artifacts) {
        if (is_export_artifact(artifact)) {
            continue;
        }
        auto dependencies = Json::array();
        for (const auto &dependency : artifact.dependencies()) {
            dependencies.push_back(
                Json::parse(detail::project_bundle_v2_artifact_ref_json(dependency)));
        }
        required.push_back(
            Json{{"id", Json::parse(detail::project_bundle_v2_artifact_id_json(artifact.id()))},
                 {"role", artifact_role_name(artifact.role())},
                 {"kind", artifact_kind_name(artifact.kind())},
                 {"schema", Json{{"format", artifact.schema_format()},
                                 {"version", artifact.schema_version()}}},
                 {"media_type", artifact.media_type()},
                 {"content_digest", artifact.content_digest().value()},
                 {"depends_on", std::move(dependencies)},
                 {"producer_name", artifact.producer_name()},
                 {"producer_version", artifact.producer_version()}});
    }
    const auto &project = manifest.root.at("project");
    const auto build_identity =
        Json{{"format", detail::project_bundle_v2_format},
             {"schema_version", 3},
             {"project", project},
             {"authoring_inputs_digest", manifest.authoring_digest.value()},
             {"dependency_lock", canonical_dependency_lock_json(manifest.lock)},
             {"artifacts", std::move(required)}};
    require(sha256_content_hash(canonical(build_identity)) == manifest.build_id.content_hash(),
            ProjectBundleOpenErrorCode::DigestMismatch,
            "BuildId does not match the complete required-default graph");

    const auto &root = manifest.root;
    const auto core = Json{{"format", root.at("format")},
                           {"schema_version", root.at("schema_version")},
                           {"project", root.at("project")},
                           {"run", root.at("run")},
                           {"authoring_inputs", root.at("authoring_inputs")},
                           {"build_id", root.at("build_id")},
                           {"dependency_lock", root.at("dependency_lock")},
                           {"export_selection", root.at("export_selection")},
                           {"artifacts", root.at("artifacts")}};
    require(sha256_content_hash(canonical(core)) == manifest.bundle_digest,
            ProjectBundleOpenErrorCode::DigestMismatch,
            "bundle digest does not match the complete canonical manifest core");
}

} // namespace volt::io::v2_open
