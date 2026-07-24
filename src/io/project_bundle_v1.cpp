#include "project_bundle_v1.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/content_hash.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/schematic/schematic_reader.hpp>

#include "pcb/pcb_reader_detail.hpp"
#include "project_bundle_source.hpp"
#include "project_bundle_storage.hpp"

namespace volt::io {
namespace {

using Json = nlohmann::json;

[[noreturn]] void fail(ProjectBundleOpenErrorCode code, std::string message) {
    throw ProjectBundleOpenError{code, std::move(message)};
}

[[nodiscard]] bool is_lower_hex(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

void validate_path_set(const std::set<std::string> &paths) {
    for (const auto &path : paths) {
        detail::validate_legacy_project_bundle_path(path);
        if (path == detail::project_bundle_manifest_path) {
            fail(ProjectBundleOpenErrorCode::DuplicateIdentity,
                 "ProjectBundle v1 artifact path collides with the root manifest");
        }
        auto separator = path.find('/');
        while (separator != std::string::npos) {
            if (paths.contains(path.substr(0U, separator))) {
                fail(ProjectBundleOpenErrorCode::DuplicateIdentity,
                     "ProjectBundle v1 artifact path collides with a file parent: " + path);
            }
            separator = path.find('/', separator + 1U);
        }
    }
}

[[nodiscard]] Json
parse_json(std::string_view bytes, std::string_view label,
           ProjectBundleOpenErrorCode error_code = ProjectBundleOpenErrorCode::MalformedManifest) {
    auto object_keys = std::vector<std::set<std::string>>{};
    const auto callback = [&](int, Json::parse_event_t event, Json &parsed) {
        if (event == Json::parse_event_t::object_start) {
            object_keys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
            if (object_keys.empty() ||
                !object_keys.back().insert(parsed.get<std::string>()).second) {
                fail(error_code, std::string{label} + " contains a duplicate JSON object key");
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
        fail(error_code, std::string{label} + " is malformed JSON: " + error.what());
    }
}

void require_object(const Json &value, std::string_view label) {
    if (!value.is_object()) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             std::string{label} + " must be an object");
    }
}

void require_array(const Json &value, std::string_view label) {
    if (!value.is_array()) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             std::string{label} + " must be an array");
    }
}

void require_exact_keys(const Json &object, const std::set<std::string> &required,
                        const std::set<std::string> &optional, std::string_view label) {
    require_object(object, label);
    for (const auto &key : required) {
        if (!object.contains(key)) {
            fail(ProjectBundleOpenErrorCode::MalformedManifest,
                 std::string{label} + " is missing required field " + key);
        }
    }
    for (const auto &[key, unused] : object.items()) {
        if (!required.contains(key) && !optional.contains(key)) {
            fail(ProjectBundleOpenErrorCode::MalformedManifest,
                 std::string{label} + " contains unsupported field " + key);
        }
    }
}

[[nodiscard]] const Json &required(const Json &object, std::string_view key,
                                   std::string_view label) {
    const auto match = object.find(key);
    if (match == object.end()) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             std::string{label} + " is missing required field " + std::string{key});
    }
    return *match;
}

[[nodiscard]] std::string required_string(const Json &object, std::string_view key,
                                          std::string_view label) {
    const auto &value = required(object, key, label);
    if (!value.is_string() || value.get_ref<const std::string &>().empty()) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             std::string{label} + " field " + std::string{key} + " must be a non-empty string");
    }
    return value.get<std::string>();
}

[[nodiscard]] std::uint32_t required_u32(const Json &object, std::string_view key,
                                         std::string_view label) {
    const auto &value = required(object, key, label);
    if (!value.is_number_unsigned() ||
        value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             std::string{label} + " field " + std::string{key} +
                 " must be an unsigned 32-bit integer");
    }
    return value.get<std::uint32_t>();
}

[[nodiscard]] std::optional<std::string> nullable_string(const Json &object, std::string_view key,
                                                         std::string_view label) {
    const auto &value = required(object, key, label);
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_string()) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             std::string{label} + " field " + std::string{key} + " must be a string or null");
    }
    return value.get<std::string>();
}

[[nodiscard]] std::map<std::string, std::string>
optional_string_object(const Json &object, std::string_view key,
                       const std::set<std::string> &allowed, std::string_view label) {
    const auto match = object.find(key);
    if (match == object.end()) {
        return {};
    }
    require_object(*match, std::string{label} + "." + std::string{key});
    auto result = std::map<std::string, std::string>{};
    for (const auto &[name, value] : match->items()) {
        if (!allowed.contains(name) || !value.is_string() ||
            value.get_ref<const std::string &>().empty()) {
            fail(ProjectBundleOpenErrorCode::MalformedManifest,
                 std::string{label} + "." + std::string{key} +
                     " contains an unsupported or non-string field");
        }
        result.emplace(name, value.get<std::string>());
    }
    return result;
}

struct LegacyManifest {
    Json root;
    std::string project_name;
    std::optional<std::string> project_version;
    std::optional<std::string> project_description;
};

[[nodiscard]] LegacyManifest parse_legacy_manifest(std::string_view bytes) {
    auto root = parse_json(bytes, "ProjectBundle manifest");
    require_object(root, "ProjectBundle manifest");
    const auto format = required_string(root, "format", "ProjectBundle manifest");
    if (format != "volt.project_result") {
        fail(ProjectBundleOpenErrorCode::UnsupportedFormat,
             "ProjectBundle manifest format is unsupported: " + format);
    }
    const auto schema = required_u32(root, "schema_version", "ProjectBundle manifest");
    if (schema == static_cast<std::uint32_t>(ProjectBundleSchemaVersion::V2)) {
        fail(ProjectBundleOpenErrorCode::UnsupportedSchema,
             "ProjectBundle schema v2 is recognized but Q3 graph verification is not implemented");
    }
    if (schema != static_cast<std::uint32_t>(ProjectBundleSchemaVersion::V1)) {
        fail(ProjectBundleOpenErrorCode::UnsupportedSchema,
             "ProjectBundle manifest schema is unsupported: " + std::to_string(schema));
    }
    require_exact_keys(root,
                       {"format", "schema_version", "project", "ok", "profile", "status", "stages",
                        "artifacts", "diagnostics", "tests"},
                       {}, "ProjectBundle manifest");
    const auto &project = required(root, "project", "ProjectBundle manifest");
    require_exact_keys(project, {"name", "version", "description"}, {}, "ProjectBundle project");
    const auto project_name = required_string(project, "name", "ProjectBundle project");
    const auto project_version = nullable_string(project, "version", "ProjectBundle project");
    const auto project_description =
        nullable_string(project, "description", "ProjectBundle project");
    if (!required(root, "ok", "ProjectBundle manifest").is_boolean()) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             "ProjectBundle manifest ok field must be boolean");
    }
    static_cast<void>(required_string(root, "profile", "ProjectBundle manifest"));
    const auto status = required_string(root, "status", "ProjectBundle manifest");
    if (status != "clean" && status != "expected-diagnostics" && status != "failed") {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             "ProjectBundle manifest status is unsupported");
    }
    require_array(required(root, "stages", "ProjectBundle manifest"), "ProjectBundle stages");
    require_array(required(root, "artifacts", "ProjectBundle manifest"), "ProjectBundle artifacts");
    require_exact_keys(required(root, "diagnostics", "ProjectBundle manifest"),
                       {"path", "summary", "status"}, {}, "ProjectBundle diagnostics record");
    require_exact_keys(required(root, "tests", "ProjectBundle manifest"), {"path", "summary"}, {},
                       "ProjectBundle tests record");
    return LegacyManifest{std::move(root), project_name, project_version, project_description};
}

[[nodiscard]] std::string artifact_identity(std::string_view kind, std::string_view name,
                                            const std::map<std::string, std::string> &group) {
    auto result = std::string{kind};
    result.push_back('\0');
    result.append(name);
    for (const auto &[key, value] : group) {
        result.push_back('\0');
        result.append(key);
        result.push_back('=');
        result.append(value);
    }
    return result;
}

void require_group(const detail::ProjectBundleStorage::Artifact &artifact,
                   const std::set<std::string> &keys) {
    auto actual = std::set<std::string>{};
    for (const auto &[key, unused] : artifact.group) {
        actual.insert(key);
    }
    if (actual != keys) {
        fail(ProjectBundleOpenErrorCode::OwnershipViolation,
             "ProjectBundle v1 " + artifact.kind +
                 " artifact has incomplete or foreign owner grouping");
    }
}

void validate_summary(const Json &summary, const std::set<std::string> &keys,
                      std::string_view label) {
    require_exact_keys(summary, keys, {}, label);
    for (const auto &[key, value] : summary.items()) {
        if (!value.is_number_unsigned()) {
            fail(ProjectBundleOpenErrorCode::MalformedManifest,
                 std::string{label} + " field " + key + " must be unsigned");
        }
    }
}

void validate_manifest_reports(const LegacyManifest &manifest,
                               const std::vector<detail::ProjectBundleStorage::Artifact> &artifacts,
                               const std::map<std::string, std::size_t> &by_path) {
    const auto &diagnostics_record = manifest.root.at("diagnostics");
    const auto diagnostics_path =
        required_string(diagnostics_record, "path", "ProjectBundle diagnostics record");
    detail::validate_legacy_project_bundle_path(diagnostics_path);
    const auto diagnostics_artifact = by_path.find(diagnostics_path);
    if (diagnostics_artifact == by_path.end() ||
        artifacts[diagnostics_artifact->second].kind != "diagnostics") {
        fail(ProjectBundleOpenErrorCode::MissingEntry,
             "ProjectBundle diagnostics record does not name its declared diagnostics artifact");
    }
    const auto diagnostics_payload =
        parse_json(artifacts[diagnostics_artifact->second].bytes, "ProjectBundle diagnostics");
    require_object(diagnostics_payload, "ProjectBundle diagnostics");
    const auto diagnostics_status =
        required_string(diagnostics_payload, "status", "ProjectBundle diagnostics");
    const auto recorded_diagnostics_status =
        required_string(diagnostics_record, "status", "ProjectBundle diagnostics record");
    if (diagnostics_status != recorded_diagnostics_status ||
        diagnostics_status != manifest.root.at("status").get<std::string>()) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             "ProjectBundle diagnostics status facts disagree");
    }
    validate_summary(required(diagnostics_payload, "summary", "ProjectBundle diagnostics"),
                     {"errors", "infos", "warnings"}, "ProjectBundle diagnostics summary");
    validate_summary(diagnostics_record.at("summary"), {"errors", "infos", "warnings"},
                     "ProjectBundle manifest diagnostics summary");
    if (diagnostics_payload.at("summary") != diagnostics_record.at("summary")) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             "ProjectBundle diagnostics summary facts disagree");
    }

    const auto &tests_record = manifest.root.at("tests");
    const auto tests_path = required_string(tests_record, "path", "ProjectBundle tests record");
    detail::validate_legacy_project_bundle_path(tests_path);
    const auto tests_artifact = by_path.find(tests_path);
    if (tests_artifact == by_path.end() ||
        artifacts[tests_artifact->second].kind != "project_tests") {
        fail(ProjectBundleOpenErrorCode::MissingEntry,
             "ProjectBundle tests record does not name its declared project-tests artifact");
    }
    const auto tests_payload =
        parse_json(artifacts[tests_artifact->second].bytes, "ProjectBundle project tests");
    require_object(tests_payload, "ProjectBundle project tests");
    validate_summary(required(tests_payload, "summary", "ProjectBundle project tests"),
                     {"failed", "passed"}, "ProjectBundle project-tests summary");
    validate_summary(tests_record.at("summary"), {"failed", "passed"},
                     "ProjectBundle manifest project-tests summary");
    if (tests_payload.at("summary") != tests_record.at("summary")) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             "ProjectBundle project-tests summary facts disagree");
    }
    const auto &payload_tests = required(tests_payload, "tests", "ProjectBundle project tests");
    require_array(payload_tests, "ProjectBundle project tests list");
    auto stage_tests = Json::array();
    auto previous_stage = std::optional<std::uint32_t>{};
    const auto stage_order =
        std::map<std::string, std::uint32_t>{{"design", 0U}, {"schematic", 1U}, {"board", 2U}};
    for (const auto &stage : manifest.root.at("stages")) {
        require_exact_keys(stage, {"name", "model_count", "tests"}, {}, "ProjectBundle stage");
        const auto name = required_string(stage, "name", "ProjectBundle stage");
        const auto order = stage_order.find(name);
        if (order == stage_order.end() ||
            (previous_stage.has_value() && *previous_stage >= order->second)) {
            fail(ProjectBundleOpenErrorCode::MalformedManifest,
                 "ProjectBundle stages are unsupported, duplicated, or out of order");
        }
        previous_stage = order->second;
        static_cast<void>(required_u32(stage, "model_count", "ProjectBundle stage"));
        const auto &tests = required(stage, "tests", "ProjectBundle stage");
        require_array(tests, "ProjectBundle stage tests");
        for (const auto &test : tests) {
            require_exact_keys(test, {"stage", "name", "ok", "message"}, {},
                               "ProjectBundle stage test");
            if (required_string(test, "stage", "ProjectBundle stage test") != name ||
                !required(test, "ok", "ProjectBundle stage test").is_boolean() ||
                !required(test, "message", "ProjectBundle stage test").is_string()) {
                fail(ProjectBundleOpenErrorCode::MalformedManifest,
                     "ProjectBundle stage test fields are malformed or foreign");
            }
            static_cast<void>(required_string(test, "name", "ProjectBundle stage test"));
            stage_tests.push_back(test);
        }
    }
    if (stage_tests != payload_tests) {
        fail(ProjectBundleOpenErrorCode::MalformedManifest,
             "ProjectBundle stage and project-test facts disagree");
    }
}

[[nodiscard]] std::shared_ptr<const detail::ProjectBundleStorage>
open_legacy(const detail::BundleSource &source, LegacyManifest manifest, std::string manifest_bytes,
            std::optional<detail::FileIdentity> manifest_identity) {
    auto storage = std::make_shared<detail::ProjectBundleStorage>();
    storage->storage_kind = source.kind();
    storage->project_name = std::move(manifest.project_name);
    storage->project_version = std::move(manifest.project_version);
    storage->project_description = std::move(manifest.project_description);
    storage->manifest_bytes = std::move(manifest_bytes);

    auto paths = std::set<std::string>{};
    auto identities = std::set<std::string>{};
    for (const auto &record : manifest.root.at("artifacts")) {
        require_exact_keys(record, {"kind", "name", "path", "media_type"},
                           {"group", "sha256", "source"}, "ProjectBundle artifact");
        auto artifact = detail::ProjectBundleStorage::Artifact{};
        artifact.kind = required_string(record, "kind", "ProjectBundle artifact");
        artifact.name = required_string(record, "name", "ProjectBundle artifact");
        artifact.path = required_string(record, "path", "ProjectBundle artifact");
        artifact.media_type = required_string(record, "media_type", "ProjectBundle artifact");
        artifact.group = optional_string_object(record, "group", {"design", "schematic", "board"},
                                                "ProjectBundle artifact");
        const auto sha = record.find("sha256");
        if (sha != record.end()) {
            if (!sha->is_string() || sha->get_ref<const std::string &>().size() != 64U ||
                !std::ranges::all_of(sha->get_ref<const std::string &>(), is_lower_hex)) {
                fail(ProjectBundleOpenErrorCode::MalformedManifest,
                     "ProjectBundle v1 sha256 must use 64 lowercase hexadecimal digits");
            }
            artifact.sha256 = sha->get<std::string>();
        }
        if (const auto source_record = record.find("source"); source_record != record.end()) {
            require_object(*source_record, "ProjectBundle artifact source");
            for (const auto &[key, value] : source_record->items()) {
                if (!value.is_string() || value.get_ref<const std::string &>().empty()) {
                    fail(ProjectBundleOpenErrorCode::MalformedManifest,
                         "ProjectBundle artifact source facts must be non-empty strings");
                }
            }
        }
        static_cast<void>(detail::validate_legacy_project_bundle_path(artifact.path));
        if (!paths.insert(artifact.path).second ||
            !identities.insert(artifact_identity(artifact.kind, artifact.name, artifact.group))
                 .second) {
            fail(ProjectBundleOpenErrorCode::DuplicateIdentity,
                 "ProjectBundle v1 manifest contains a duplicate path or artifact identity");
        }
        artifact.manifest_record_json = record.dump();
        storage->artifacts.push_back(std::move(artifact));
    }
    validate_path_set(paths);
    std::ranges::sort(storage->artifacts, {}, &detail::ProjectBundleStorage::Artifact::path);

    auto by_path = std::map<std::string, std::size_t>{};
    auto host_identities = std::set<detail::FileIdentity>{};
    if (manifest_identity.has_value()) {
        host_identities.insert(*manifest_identity);
    }
    for (auto index = std::size_t{0}; index < storage->artifacts.size(); ++index) {
        auto &artifact = storage->artifacts[index];
        auto captured = source.read(artifact.path);
        if (captured.identity.has_value() && !host_identities.insert(*captured.identity).second) {
            fail(ProjectBundleOpenErrorCode::DuplicateIdentity,
                 "Distinct ProjectBundle v1 paths resolve to the same host file identity");
        }
        if (artifact.sha256.has_value()) {
            const auto digest = sha256_content_hash(captured.bytes).value().substr(7U);
            if (digest != *artifact.sha256) {
                fail(ProjectBundleOpenErrorCode::DigestMismatch,
                     "ProjectBundle v1 recorded sha256 does not match captured bytes: " +
                         artifact.path);
            }
        }
        artifact.bytes = std::move(captured.bytes);
        by_path.emplace(artifact.path, index);
    }
    validate_manifest_reports(manifest, storage->artifacts, by_path);

    auto circuit_by_design = std::map<std::string, std::size_t>{};
    for (auto artifact_index = std::size_t{0}; artifact_index < storage->artifacts.size();
         ++artifact_index) {
        const auto &artifact = storage->artifacts[artifact_index];
        if (artifact.kind != "logical") {
            continue;
        }
        require_group(artifact, {"design"});
        if (artifact.media_type != "application/vnd.volt.logical+json" ||
            artifact.name != artifact.group.at("design")) {
            fail(ProjectBundleOpenErrorCode::OwnershipViolation,
                 "ProjectBundle v1 logical artifact identity or media type is inconsistent");
        }
        const auto design = artifact.group.at("design");
        if (circuit_by_design.contains(design)) {
            fail(ProjectBundleOpenErrorCode::DuplicateIdentity,
                 "ProjectBundle v1 contains duplicate logical design identity " + design);
        }
        try {
            static_cast<void>(parse_json(artifact.bytes, "ProjectBundle logical model",
                                         ProjectBundleOpenErrorCode::ModelDecodeFailure));
            auto model = std::make_unique<Circuit>(read_logical_circuit_text(artifact.bytes));
            const auto index = storage->circuits.size();
            storage->circuits.push_back(detail::ProjectBundleStorage::CircuitDocument{
                design, artifact_index, std::move(model)});
            circuit_by_design.emplace(design, index);
        } catch (const ProjectBundleOpenError &) {
            throw;
        } catch (const std::exception &error) {
            fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                 "ProjectBundle v1 logical model decode failed: " + std::string{error.what()});
        }
    }
    std::ranges::sort(storage->circuits, {},
                      &detail::ProjectBundleStorage::CircuitDocument::design);
    circuit_by_design.clear();
    for (auto index = std::size_t{0}; index < storage->circuits.size(); ++index) {
        circuit_by_design.emplace(storage->circuits[index].design, index);
    }

    auto schematic_identities = std::set<std::pair<std::string, std::string>>{};
    auto board_identities = std::set<std::pair<std::string, std::string>>{};
    for (auto artifact_index = std::size_t{0}; artifact_index < storage->artifacts.size();
         ++artifact_index) {
        const auto &artifact = storage->artifacts[artifact_index];
        if (artifact.kind != "schematic" && artifact.kind != "pcb") {
            continue;
        }
        const auto projection_key = artifact.kind == "schematic" ? "schematic" : "board";
        require_group(artifact, {"design", projection_key});
        const auto design = artifact.group.at("design");
        const auto circuit = circuit_by_design.find(design);
        if (circuit == circuit_by_design.end()) {
            fail(ProjectBundleOpenErrorCode::OwnershipViolation,
                 "ProjectBundle v1 projection names a missing logical owner " + design);
        }
        if (artifact.kind == "schematic") {
            if (artifact.media_type != "application/vnd.volt.schematic+json" ||
                !schematic_identities.emplace(design, artifact.group.at("schematic")).second) {
                fail(ProjectBundleOpenErrorCode::DuplicateIdentity,
                     "ProjectBundle v1 Schematic identity or media type is invalid");
            }
            try {
                static_cast<void>(parse_json(artifact.bytes, "ProjectBundle Schematic model",
                                             ProjectBundleOpenErrorCode::ModelDecodeFailure));
                auto model = std::make_unique<volt::Schematic>(
                    read_schematic_text(artifact.bytes, *storage->circuits[circuit->second].model));
                storage->schematics.push_back(detail::ProjectBundleStorage::SchematicDocument{
                    design, artifact.group.at("schematic"), artifact_index, circuit->second,
                    std::move(model)});
            } catch (const ProjectBundleOpenError &) {
                throw;
            } catch (const std::exception &error) {
                fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                     "ProjectBundle v1 Schematic decode failed: " + std::string{error.what()});
            }
        } else {
            if (artifact.media_type != "application/vnd.volt.pcb+json" ||
                !board_identities.emplace(design, artifact.group.at("board")).second) {
                fail(ProjectBundleOpenErrorCode::DuplicateIdentity,
                     "ProjectBundle v1 Board identity or media type is invalid");
            }
            try {
                static_cast<void>(parse_json(artifact.bytes, "ProjectBundle Board model",
                                             ProjectBundleOpenErrorCode::ModelDecodeFailure));
                auto model =
                    std::make_unique<volt::Board>(detail::read_project_bundle_v1_pcb_board_text(
                        *storage->circuits[circuit->second].model, artifact.bytes));
                if (model->name().value() != artifact.group.at("board")) {
                    fail(ProjectBundleOpenErrorCode::OwnershipViolation,
                         "ProjectBundle v1 Board payload name disagrees with its manifest owner");
                }
                storage->boards.push_back(detail::ProjectBundleStorage::BoardDocument{
                    design, artifact.group.at("board"), artifact_index, circuit->second,
                    std::move(model)});
            } catch (const ProjectBundleOpenError &) {
                throw;
            } catch (const std::exception &error) {
                fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                     "ProjectBundle v1 Board decode failed: " + std::string{error.what()});
            }
        }
    }
    std::ranges::sort(storage->schematics, [](const auto &left, const auto &right) {
        return std::tie(left.design, left.schematic) < std::tie(right.design, right.schematic);
    });
    std::ranges::sort(storage->boards, [](const auto &left, const auto &right) {
        return std::tie(left.design, left.board) < std::tie(right.design, right.board);
    });

    for (const auto &artifact : storage->artifacts) {
        const auto design = artifact.group.find("design");
        if (design != artifact.group.end() && !circuit_by_design.contains(design->second)) {
            fail(ProjectBundleOpenErrorCode::OwnershipViolation,
                 "ProjectBundle v1 artifact names a missing logical owner " + design->second);
        }
        const auto schematic = artifact.group.find("schematic");
        if (schematic != artifact.group.end() &&
            (design == artifact.group.end() ||
             !schematic_identities.contains({design->second, schematic->second}))) {
            fail(ProjectBundleOpenErrorCode::OwnershipViolation,
                 "ProjectBundle v1 artifact names a missing Schematic owner");
        }
        const auto board = artifact.group.find("board");
        if (board != artifact.group.end() &&
            (design == artifact.group.end() ||
             !board_identities.contains({design->second, board->second}))) {
            fail(ProjectBundleOpenErrorCode::OwnershipViolation,
                 "ProjectBundle v1 artifact names a missing Board owner");
        }
    }

    const auto model_counts =
        std::map<std::string, std::size_t>{{"design", storage->circuits.size()},
                                           {"schematic", storage->schematics.size()},
                                           {"board", storage->boards.size()}};
    for (const auto &stage : manifest.root.at("stages")) {
        const auto name = stage.at("name").get<std::string>();
        if (stage.at("model_count").get<std::size_t>() != model_counts.at(name)) {
            fail(ProjectBundleOpenErrorCode::MalformedManifest,
                 "ProjectBundle v1 stage model count disagrees with decoded models");
        }
    }
    return storage;
}

} // namespace

namespace detail {

std::shared_ptr<const ProjectBundleStorage> open_project_bundle_v1(const BundleSource &source,
                                                                   CapturedEntry manifest_capture) {
    auto manifest = parse_legacy_manifest(manifest_capture.bytes);
    return open_legacy(source, std::move(manifest), std::move(manifest_capture.bytes),
                       manifest_capture.identity);
}

} // namespace detail
} // namespace volt::io
