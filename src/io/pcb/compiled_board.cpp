#include <volt/io/pcb/compiled_board.hpp>

#include "board_resolution_detail.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/errors.hpp>
#include <volt/core/version.hpp>
#include <volt/io/capabilities/board_capability_profile.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/parts/part_definition_reader.hpp>
#include <volt/io/parts/part_definition_writer.hpp>
#include <volt/io/pcb/board_resolution.hpp>
#include <volt/io/pcb/pcb_reader.hpp>
#include <volt/io/pcb/pcb_writer.hpp>

namespace volt::io {
namespace {

using Json = nlohmann::json;

constexpr auto archive_magic = std::string_view{"VOLT-COMPILED\0", 14U};
constexpr auto compiler_name = std::string_view{"volt.native.compile-board"};
constexpr auto logical_snapshot_format =
    std::string_view{"volt.compiled-board.logical-dependencies"};
constexpr auto capability_snapshot_format = std::string_view{"volt.compiled-board.capabilities"};
constexpr auto closure_format = std::string_view{"volt.compiled-board.selected-closure"};
constexpr auto provenance_format = std::string_view{"volt.compiled-board.provenance"};
constexpr std::uint64_t max_manifest_bytes = 16U * 1024U * 1024U;
constexpr std::uint64_t max_entry_count = 100'000U;
constexpr std::uint64_t max_path_bytes = 4096U;
constexpr std::uint64_t max_payload_bytes = 1024U * 1024U * 1024U;

struct Payload {
    std::string path;
    std::string role;
    std::string bytes;
    ContentHash digest;
};

struct LogicalSnapshot {
    std::string bytes;
    std::map<std::string, std::string> component_documents;
};

struct Archive {
    Json manifest;
    std::map<std::string, std::string> payloads;
};

struct DecodedComponent {
    ComponentDefinition definition;
    Json logical_projection;
};

struct DecodedPart {
    std::string reference_key;
    PartDefinition part;
    FootprintDefinition footprint;
    std::optional<std::string> model_bytes;
};

[[noreturn]] void fail(std::string message, ErrorCode code = ErrorCode::InvalidArgument) {
    throw KernelLogicError{code, message};
}

void require(bool condition, std::string message, ErrorCode code = ErrorCode::InvalidArgument) {
    if (!condition) {
        fail(std::move(message), code);
    }
}

[[nodiscard]] Json parse_json(std::string_view bytes, std::string_view label) {
    try {
        return Json::parse(bytes.begin(), bytes.end());
    } catch (const Json::exception &error) {
        fail(std::string{label} + " JSON is invalid: " + error.what());
    }
}

[[nodiscard]] Json parse_canonical_json(std::string_view bytes, std::string_view label) {
    auto document = parse_json(bytes, label);
    // V1 recognizes the owner's exact nlohmann dump representation. Changing JSON number or
    // string encoding rules therefore requires a versioned codec decision, not a permissive read.
    require(document.dump() == bytes, std::string{label} + " bytes are not canonical JSON");
    return document;
}

void require_exact_keys(const Json &object, std::initializer_list<std::string_view> expected,
                        std::string_view label) {
    require(object.is_object(), std::string{label} + " must be an object");
    auto actual = std::set<std::string>{};
    for (auto it = object.begin(); it != object.end(); ++it) {
        actual.insert(it.key());
    }
    auto wanted = std::set<std::string>{};
    for (const auto key : expected) {
        wanted.emplace(key);
    }
    require(actual == wanted, std::string{label} + " has missing or unknown fields");
}

[[nodiscard]] const Json &field(const Json &object, std::string_view name, std::string_view label) {
    require(object.is_object(), std::string{label} + " must be an object");
    const auto match = object.find(std::string{name});
    require(match != object.end(), std::string{label} + " is missing field " + std::string{name});
    return *match;
}

[[nodiscard]] std::string string_field(const Json &object, std::string_view name,
                                       std::string_view label) {
    const auto &value = field(object, name, label);
    require(value.is_string(), std::string{label} + "." + std::string{name} + " must be a string");
    return value.get<std::string>();
}

[[nodiscard]] std::uint32_t uint32_field(const Json &object, std::string_view name,
                                         std::string_view label) {
    const auto &value = field(object, name, label);
    require(value.is_number_unsigned(),
            std::string{label} + "." + std::string{name} + " must be an unsigned integer");
    const auto result = value.get<std::uint64_t>();
    require(result <= std::numeric_limits<std::uint32_t>::max(),
            std::string{label} + "." + std::string{name} + " exceeds uint32");
    return static_cast<std::uint32_t>(result);
}

[[nodiscard]] std::string hash_suffix(const ContentHash &digest) {
    return digest.value().substr(std::string_view{"sha256:"}.size());
}

[[nodiscard]] std::string component_def_id(std::size_t index) {
    return "component_def:" + std::to_string(index);
}

[[nodiscard]] std::string pin_def_id(std::size_t index) {
    return "pin_def:" + std::to_string(index);
}

[[nodiscard]] Json reference_json(const LibraryPartRef &reference) {
    return Json{
        {"library_digest", reference.library_digest().value()},
        {"library_namespace", reference.library_namespace()},
        {"library_version", reference.library_version()},
        {"part_digest", reference.part_digest().value()},
        {"part_key", reference.part_key().value()},
    };
}

[[nodiscard]] LibraryPartRef reference_from_json(const Json &document) {
    require_exact_keys(
        document,
        {"library_digest", "library_namespace", "library_version", "part_digest", "part_key"},
        "CompiledBoard selected reference");
    return LibraryPartRef{
        string_field(document, "library_namespace", "CompiledBoard selected reference"),
        string_field(document, "library_version", "CompiledBoard selected reference"),
        PartKey{string_field(document, "part_key", "CompiledBoard selected reference")},
        ContentHash{string_field(document, "library_digest", "CompiledBoard selected reference")},
        ContentHash{string_field(document, "part_digest", "CompiledBoard selected reference")},
    };
}

[[nodiscard]] Json asset_json(const LibraryPartRef &origin, const PartAssetReference &reference) {
    auto kind = std::string{};
    switch (reference.kind()) {
    case PartAssetKind::Footprint:
        kind = "footprint";
        break;
    case PartAssetKind::Model3D:
        kind = "models3d";
        break;
    case PartAssetKind::Schematic:
    case PartAssetKind::Simulation:
    case PartAssetKind::Evidence:
    case PartAssetKind::Licence:
    case PartAssetKind::Provenance:
        fail("CompiledBoard selected closure contains an unsupported asset kind");
    }
    return Json{
        {"digest", reference.digest().value()},
        {"key", reference.key()},
        {"kind", std::move(kind)},
        {"library_digest", origin.library_digest().value()},
        {"library_namespace", origin.library_namespace()},
        {"library_version", origin.library_version()},
    };
}

[[nodiscard]] std::string reference_key(const LibraryPartRef &reference) {
    return reference_json(reference).dump();
}

[[nodiscard]] std::string component_path(const ContentHash &identity) {
    return "closure/components/" + hash_suffix(identity) + ".volt.json";
}

[[nodiscard]] std::string part_path(const LibraryPartRef &reference) {
    return "closure/parts/" + hash_suffix(sha256_content_hash(reference_key(reference))) +
           ".volt.json";
}

[[nodiscard]] std::string asset_path(const Json &identity) {
    const auto kind = string_field(identity, "kind", "CompiledBoard asset identity");
    return "closure/assets/" + kind + "/" + hash_suffix(sha256_content_hash(identity.dump())) +
           ".bin";
}

void add_payload(std::map<std::string, Payload> &payloads, std::string path, std::string role,
                 std::string bytes) {
    const auto existing = payloads.find(path);
    if (existing != payloads.end()) {
        require(existing->second.role == role && existing->second.bytes == bytes,
                "CompiledBoard closure path has conflicting payloads",
                ErrorCode::CrossReferenceViolation);
        return;
    }
    const auto digest = sha256_content_hash(bytes);
    payloads.emplace(path, Payload{path, std::move(role), std::move(bytes), digest});
}

[[nodiscard]] const Json &find_by_id(const Json &array, std::string_view id,
                                     std::string_view label) {
    require(array.is_array(), std::string{label} + " must be an array");
    const auto match = std::ranges::find_if(array, [&](const Json &entry) {
        return entry.is_object() && entry.value("id", std::string{}) == id;
    });
    require(match != array.end(), std::string{label} + " is missing ID " + std::string{id},
            ErrorCode::UnknownEntity);
    return *match;
}

[[nodiscard]] ComponentContractSpec contract_spec(const ComponentContract &contract) {
    return ComponentContractSpec{contract.key(),
                                 contract.pin_keys(),
                                 contract.framed_pins(),
                                 contract.relations(),
                                 contract.supply_domains(),
                                 contract.feature_schemas(),
                                 contract.feature_bindings()};
}

[[nodiscard]] ContentHash logical_component_identity(const Circuit &owner,
                                                     const ComponentDefinition &definition) {
    auto pins = std::vector<PinDefinition>{};
    pins.reserve(definition.pins().size());
    for (const auto pin : definition.pins()) {
        pins.push_back(owner.get(pin));
    }
    auto contract = std::optional<ComponentContractSpec>{};
    if (definition.contract().explicitly_authored()) {
        contract = contract_spec(definition.contract());
    }
    return ComponentDefinition::make(definition.name(), pins, definition.pins(),
                                     definition.properties(), definition.source(), {}, contract)
        .content_identity();
}

[[nodiscard]] Json component_document(const Json &logical, std::string_view definition_id) {
    const auto &definition = find_by_id(field(logical, "component_definitions", "logical circuit"),
                                        definition_id, "logical component definitions");
    auto pins = Json::array();
    auto remapped_pins = Json::array();
    const auto &definition_pins = field(definition, "pins", "logical component definition");
    require(definition_pins.is_array(), "logical component definition pins must be an array");
    for (std::size_t index = 0; index < definition_pins.size(); ++index) {
        require(definition_pins[index].is_string(),
                "logical component definition pin ID must be a string");
        auto pin = find_by_id(field(logical, "pin_definitions", "logical circuit"),
                              definition_pins[index].get<std::string>(), "logical pin definitions");
        pin["id"] = pin_def_id(index);
        pins.push_back(std::move(pin));
        remapped_pins.push_back(pin_def_id(index));
    }

    auto remapped_definition = definition;
    remapped_definition["id"] = component_def_id(0);
    remapped_definition["pins"] = std::move(remapped_pins);
    return Json{
        {"component_definitions", Json::array({std::move(remapped_definition)})},
        {"components", Json::array()},
        {"format", logical_circuit_format_name()},
        {"nets", Json::array()},
        {"pin_definitions", std::move(pins)},
        {"pins", Json::array()},
        {"version", logical_circuit_format_version()},
    };
}

[[nodiscard]] Json component_contract_document(const Json &logical,
                                               std::string_view definition_id) {
    auto document = component_document(logical, definition_id);
    auto &definition = document["component_definitions"].at(0);
    if (definition.contains("contract")) {
        definition["name"] = "CompiledBoard component contract";
    }
    definition["properties"] = Json::object();
    return document;
}

[[nodiscard]] Json module_component_origins(const Json &logical) {
    auto result = std::vector<Json>{};
    const auto instances = logical.find("module_instances");
    const auto definitions = logical.find("module_definitions");
    if (instances == logical.end() || definitions == logical.end()) {
        return Json::array();
    }
    require(instances->is_array() && definitions->is_array(),
            "logical module provenance inputs must be arrays");
    for (const auto &instance : *instances) {
        const auto definition_id = string_field(instance, "definition", "logical module instance");
        const auto &definition =
            find_by_id(*definitions, definition_id, "logical module definitions");
        const auto module_name = string_field(definition, "name", "logical module definition");
        const auto instance_name = string_field(instance, "name", "logical module instance");
        const auto &templates = field(definition, "components", "logical module definition");
        for (const auto &origin : field(instance, "component_origins", "logical module instance")) {
            const auto template_id =
                string_field(origin, "template_component", "logical module component origin");
            const auto &template_component =
                find_by_id(templates, template_id, "logical module component templates");
            result.push_back(Json{
                {"component", string_field(origin, "component", "logical module component origin")},
                {"module_definition", module_name},
                {"module_instance", instance_name},
                {"template_reference", string_field(template_component, "reference",
                                                    "logical module component template")}});
        }
    }
    std::ranges::sort(result, {}, [](const Json &entry) { return entry.dump(); });
    return Json(std::move(result));
}

[[nodiscard]] Json module_net_origins(const Json &logical) {
    auto result = std::vector<Json>{};
    const auto instances = logical.find("module_instances");
    const auto definitions = logical.find("module_definitions");
    if (instances == logical.end() || definitions == logical.end()) {
        return Json::array();
    }
    for (const auto &instance : *instances) {
        const auto definition_id = string_field(instance, "definition", "logical module instance");
        const auto &definition =
            find_by_id(*definitions, definition_id, "logical module definitions");
        const auto module_name = string_field(definition, "name", "logical module definition");
        const auto instance_name = string_field(instance, "name", "logical module instance");
        const auto &templates = field(definition, "local_nets", "logical module definition");
        for (const auto &origin : field(instance, "net_origins", "logical module instance")) {
            const auto template_id =
                string_field(origin, "template_net", "logical module net origin");
            const auto &template_net =
                find_by_id(templates, template_id, "logical module net templates");
            result.push_back(Json{{"module_definition", module_name},
                                  {"module_instance", instance_name},
                                  {"net", string_field(origin, "net", "logical module net origin")},
                                  {"template_net", string_field(template_net, "name",
                                                                "logical module net template")}});
        }
    }
    std::ranges::sort(result, {}, [](const Json &entry) { return entry.dump(); });
    return Json(std::move(result));
}

void filter_net_classes(Json &logical) {
    const auto net_classes = logical.find("net_classes");
    if (net_classes == logical.end()) {
        return;
    }
    auto &classes = (*net_classes)["classes"];
    auto &assignments = (*net_classes)["net_assignments"];
    require(classes.is_array() && assignments.is_array(),
            "logical net-class fields must be arrays");

    auto used = std::set<std::string>{};
    for (const auto &assignment : assignments) {
        used.insert(string_field(assignment, "net_class", "logical net-class assignment"));
    }
    if (used.empty()) {
        logical.erase("net_classes");
        return;
    }

    auto remap = std::map<std::string, std::string>{};
    auto filtered = Json::array();
    for (const auto &entry : classes) {
        const auto old_id = string_field(entry, "id", "logical net class");
        if (!used.contains(old_id)) {
            continue;
        }
        const auto new_id = "net_class:" + std::to_string(filtered.size());
        auto copy = entry;
        copy["id"] = new_id;
        remap.emplace(old_id, new_id);
        filtered.push_back(std::move(copy));
    }
    require(remap.size() == used.size(), "logical net-class assignment is dangling",
            ErrorCode::CrossReferenceViolation);
    for (auto &assignment : assignments) {
        assignment["net_class"] =
            remap.at(string_field(assignment, "net_class", "logical net-class assignment"));
    }
    classes = std::move(filtered);
}

[[nodiscard]] LogicalSnapshot build_logical_snapshot(const Circuit &circuit) {
    const auto complete = parse_json(write_logical_circuit(circuit), "logical circuit");
    auto logical = complete;
    const auto component_origins = module_component_origins(complete);
    const auto net_origins = module_net_origins(complete);

    auto referenced_definitions = std::set<std::string>{};
    for (const auto &component : field(complete, "components", "logical circuit")) {
        referenced_definitions.insert(string_field(component, "definition", "logical component"));
    }

    auto definition_remap = std::map<std::string, std::string>{};
    auto pin_remap = std::map<std::string, std::string>{};
    auto pin_definitions = Json::array();
    auto component_definitions = Json::array();
    auto identities = Json::array();
    auto component_documents = std::map<std::string, std::string>{};

    const auto &complete_definitions = field(complete, "component_definitions", "logical circuit");
    for (std::size_t index = 0; index < complete_definitions.size(); ++index) {
        const auto &definition = complete_definitions[index];
        const auto old_id = string_field(definition, "id", "logical component definition");
        if (!referenced_definitions.contains(old_id)) {
            continue;
        }
        const auto new_id = component_def_id(component_definitions.size());
        definition_remap.emplace(old_id, new_id);
        const auto identity = circuit.get(ComponentDefId{index}).content_identity();
        identities.push_back(Json{{"content_identity", identity.value()}, {"id", new_id}});
        const auto component_bytes = component_contract_document(complete, old_id).dump();
        const auto [existing, document_inserted] =
            component_documents.emplace(identity.value(), component_bytes);
        require(document_inserted || existing->second == component_bytes,
                "logical component identity maps to conflicting semantic contracts",
                ErrorCode::CrossReferenceViolation);

        auto copy = definition;
        copy["id"] = new_id;
        copy.erase("schematic_symbols");
        if (const auto contract = copy.find("contract"); contract != copy.end()) {
            (*contract)["content_identity"] =
                logical_component_identity(circuit, circuit.get(ComponentDefId{index})).value();
        }
        auto remapped_pins = Json::array();
        for (const auto &pin : field(definition, "pins", "logical component definition")) {
            require(pin.is_string(), "logical component definition pin ID must be a string");
            const auto old_pin = pin.get<std::string>();
            const auto new_pin = pin_def_id(pin_definitions.size());
            const auto [unused, inserted] = pin_remap.emplace(old_pin, new_pin);
            static_cast<void>(unused);
            require(inserted, "logical pin definition belongs to multiple component definitions",
                    ErrorCode::CrossReferenceViolation);
            auto pin_definition = find_by_id(field(complete, "pin_definitions", "logical circuit"),
                                             old_pin, "logical pin definitions");
            pin_definition["id"] = new_pin;
            pin_definitions.push_back(std::move(pin_definition));
            remapped_pins.push_back(new_pin);
        }
        copy["pins"] = std::move(remapped_pins);
        component_definitions.push_back(std::move(copy));
    }
    require(definition_remap.size() == referenced_definitions.size(),
            "logical component references an absent definition",
            ErrorCode::CrossReferenceViolation);

    logical["pin_definitions"] = std::move(pin_definitions);
    logical["component_definitions"] = std::move(component_definitions);
    for (auto &component : logical["components"]) {
        component["definition"] =
            definition_remap.at(string_field(component, "definition", "logical component"));
    }
    for (auto &pin : logical["pins"]) {
        pin["definition"] = pin_remap.at(string_field(pin, "definition", "logical pin"));
    }
    logical.erase("module_definitions");
    logical.erase("module_instances");
    filter_net_classes(logical);

    auto wrapper = Json{
        {"circuit", std::move(logical)},
        {"component_definition_identities", std::move(identities)},
        {"component_origins", component_origins},
        {"format", logical_snapshot_format},
        {"net_origins", net_origins},
        {"schema_version", static_cast<std::uint32_t>(CompiledBoardSchemaVersion::V1)},
    };
    return LogicalSnapshot{wrapper.dump(), std::move(component_documents)};
}

[[nodiscard]] std::string build_physical_snapshot(const Board &board) {
    auto document = parse_json(write_pcb_board(board, FootprintLibrary{}), "PCB snapshot");
    document.erase("viewer");
    const auto &board_json = field(document, "board", "PCB snapshot");
    const auto &footprints = field(board_json, "footprint_definitions", "PCB Board snapshot");
    require(footprints.is_array() && footprints.empty(),
            "CompiledBoard authoring Board must not carry cached footprint definitions",
            ErrorCode::InvalidState);
    return document.dump();
}

[[nodiscard]] Json capabilities_json(const CompiledBoardCapabilities &capabilities) {
    auto additional = Json::array();
    for (const auto capability : capabilities.additional()) {
        if (capability == BoardAssetCapability::Models3D) {
            additional.push_back("models3d");
            continue;
        }
        fail("CompiledBoard capability is unsupported by schema v1");
    }
    return Json{
        {"additional_assets", std::move(additional)},
        {"format", capability_snapshot_format},
        {"profile_document",
         parse_json(write_capability_profile(capabilities.profile()), "capability profile")},
        {"schema_version", static_cast<std::uint32_t>(CompiledBoardSchemaVersion::V1)},
    };
}

[[nodiscard]] CompiledBoardCapabilities capabilities_from_json(const Json &document) {
    require_exact_keys(document,
                       {"additional_assets", "format", "profile_document", "schema_version"},
                       "CompiledBoard capability snapshot");
    require(string_field(document, "format", "CompiledBoard capability snapshot") ==
                capability_snapshot_format,
            "CompiledBoard capability snapshot format is unsupported");
    require(uint32_field(document, "schema_version", "CompiledBoard capability snapshot") ==
                static_cast<std::uint32_t>(CompiledBoardSchemaVersion::V1),
            "CompiledBoard capability snapshot schema is unsupported");
    const auto &additional_json =
        field(document, "additional_assets", "CompiledBoard capability snapshot");
    require(additional_json.is_array(), "CompiledBoard additional capabilities must be an array");
    auto additional = std::vector<BoardAssetCapability>{};
    for (const auto &entry : additional_json) {
        require(entry.is_string(), "CompiledBoard capability name must be a string");
        const auto name = entry.get<std::string>();
        require(name == "models3d", "CompiledBoard capability is unsupported by schema v1");
        additional.push_back(BoardAssetCapability::Models3D);
    }
    const auto &profile = field(document, "profile_document", "CompiledBoard capability snapshot");
    auto decoded_profile = read_capability_profile_text(profile.dump());
    require(parse_json(write_capability_profile(decoded_profile),
                       "materialized capability profile") == profile,
            "CompiledBoard capability profile is not owner-canonical",
            ErrorCode::CrossReferenceViolation);
    return CompiledBoardCapabilities{std::move(decoded_profile), std::move(additional)};
}

[[nodiscard]] ContentHash selected_closure_digest(const Json &selected_parts) {
    return sha256_content_hash(Json{
        {"format", closure_format},
        {"schema_version", static_cast<std::uint32_t>(CompiledBoardSchemaVersion::V1)},
        {"selected_parts",
         selected_parts}}.dump());
}

[[nodiscard]] ContentHash
provenance_digest(std::string_view board_name, std::string_view compiler_build,
                  const ContentHash &logical_digest, const ContentHash &physical_digest,
                  const ContentHash &closure_digest, const ContentHash &capability_digest) {
    return sha256_content_hash(Json{
        {"board_name", board_name},
        {"capability_digest", capability_digest.value()},
        {"compiler_build", compiler_build},
        {"compiler_name", compiler_name},
        {"compiler_version", static_cast<std::uint32_t>(CompiledBoardCompilerVersion::V1)},
        {"format", provenance_format},
        {"logical_dependency_digest", logical_digest.value()},
        {"physical_snapshot_digest", physical_digest.value()},
        {"schema_version", static_cast<std::uint32_t>(CompiledBoardSchemaVersion::V1)},
        {"selected_closure_digest",
         closure_digest.value()}}.dump());
}

[[nodiscard]] Json selected_part_row(const LibraryPartRef &reference, const PartDefinition &part,
                                     const Json &footprint, std::optional<Json> model) {
    auto result = Json{
        {"component_identity", part.implemented_component().value()},
        {"footprint", footprint},
        {"model_3d", nullptr},
        {"reference", reference_json(reference)},
    };
    if (model.has_value()) {
        result["model_3d"] = std::move(*model);
    }
    return result;
}

[[nodiscard]] Json
build_selected_closure(const BoardResolution &resolution, const PartLibraryBundle &bundle,
                       const CompiledBoardCapabilities &capabilities,
                       const std::map<std::string, std::string> &component_documents,
                       std::map<std::string, Payload> &payloads) {
    auto rows_by_reference = std::map<std::string, Json>{};
    for (const auto &resolved : resolution.parts()) {
        const auto &reference = resolved.reference();
        const auto key = reference_key(reference);
        if (rows_by_reference.contains(key)) {
            continue;
        }

        const auto &part = bundle.resolve(reference);
        const auto component_document =
            component_documents.find(part.implemented_component().value());
        require(component_document != component_documents.end(),
                "CompiledBoard selected part component contract is outside the logical closure",
                ErrorCode::CrossReferenceViolation);
        add_payload(payloads, component_path(part.implemented_component()), "component_definition",
                    component_document->second);
        add_payload(payloads, part_path(reference), "part_definition", write_part_definition(part));

        const auto footprint_reference = detail::footprint_asset_reference(part);
        const auto *footprint =
            resolution.footprints().find(part.orderable_part().footprint().footprint());
        require(footprint != nullptr, "CompiledBoard resolved footprint asset is missing",
                ErrorCode::UnknownEntity);
        const auto footprint_bytes = write_footprint_asset(*footprint);
        require(sha256_content_hash(footprint_bytes) == footprint_reference.digest(),
                "CompiledBoard resolved footprint asset digest is stale",
                ErrorCode::CrossReferenceViolation);
        const auto footprint_identity = asset_json(reference, footprint_reference);
        add_payload(payloads, asset_path(footprint_identity), "footprint", footprint_bytes);

        auto model_identity = std::optional<Json>{};
        const auto &model = part.orderable_part().model_3d();
        if (capabilities.has(BoardAssetCapability::Models3D) && model.has_value()) {
            const auto model_reference = detail::model_asset_reference(*model);
            require(resolved.model_3d_bytes().has_value(),
                    "CompiledBoard resolved 3D asset is missing", ErrorCode::UnknownEntity);
            require(sha256_content_hash(*resolved.model_3d_bytes()) == model_reference.digest(),
                    "CompiledBoard resolved 3D asset digest is stale",
                    ErrorCode::CrossReferenceViolation);
            model_identity = asset_json(reference, model_reference);
            add_payload(payloads, asset_path(*model_identity), "models3d",
                        *resolved.model_3d_bytes());
        }

        rows_by_reference.emplace(
            key, selected_part_row(reference, part, footprint_identity, std::move(model_identity)));
    }

    auto rows = Json::array();
    for (auto &[unused, row] : rows_by_reference) {
        static_cast<void>(unused);
        rows.push_back(std::move(row));
    }
    return rows;
}

[[nodiscard]] Json entry_json(const Payload &payload) {
    return Json{
        {"digest", payload.digest.value()},
        {"path", payload.path},
        {"role", payload.role},
    };
}

void append_u64(std::string &out, std::uint64_t value) {
    for (auto shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((value >> static_cast<unsigned>(shift)) & 0xffU));
    }
}

void append_sized(std::string &out, std::string_view value) {
    append_u64(out, static_cast<std::uint64_t>(value.size()));
    out.append(value);
}

[[nodiscard]] std::string encode_archive(const Json &manifest,
                                         const std::map<std::string, Payload> &payloads) {
    auto bytes = std::string{archive_magic};
    append_sized(bytes, manifest.dump());
    append_u64(bytes, static_cast<std::uint64_t>(payloads.size()));
    for (const auto &[path, payload] : payloads) {
        append_sized(bytes, path);
        append_sized(bytes, payload.bytes);
    }
    return bytes;
}

[[nodiscard]] std::uint64_t read_u64(std::string_view bytes, std::size_t &cursor) {
    require(cursor <= bytes.size() && bytes.size() - cursor >= 8U,
            "CompiledBoard archive is truncated");
    auto value = std::uint64_t{0};
    for (auto index = std::size_t{0}; index < 8U; ++index) {
        value = (value << 8U) | static_cast<unsigned char>(bytes[cursor + index]);
    }
    cursor += 8U;
    return value;
}

[[nodiscard]] std::string read_sized(std::string_view bytes, std::size_t &cursor,
                                     std::uint64_t limit, std::string_view label) {
    const auto size = read_u64(bytes, cursor);
    require(size <= limit, "CompiledBoard " + std::string{label} + " exceeds its limit");
    require(cursor <= bytes.size() && size <= static_cast<std::uint64_t>(bytes.size() - cursor),
            "CompiledBoard archive is truncated");
    auto result = std::string{bytes.substr(cursor, static_cast<std::size_t>(size))};
    cursor += static_cast<std::size_t>(size);
    return result;
}

void require_canonical_path(std::string_view path) {
    require(!path.empty(), "CompiledBoard payload path must not be empty");
    require(path.front() != '/' && path.find('\\') == std::string_view::npos &&
                path.find('\0') == std::string_view::npos && !(path.size() >= 2U && path[1] == ':'),
            "CompiledBoard payload path is unsafe");
    auto begin = std::size_t{0};
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        const auto segment =
            path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);
        require(!segment.empty() && segment != "." && segment != "..",
                "CompiledBoard payload path is unsafe");
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
}

[[nodiscard]] Archive decode_archive(std::string_view bytes) {
    require(bytes.size() >= archive_magic.size(), "CompiledBoard archive is truncated");
    require(bytes.substr(0U, archive_magic.size()) == archive_magic,
            "CompiledBoard archive magic is invalid");
    auto cursor = archive_magic.size();
    const auto manifest_bytes = read_sized(bytes, cursor, max_manifest_bytes, "manifest");
    auto manifest = parse_canonical_json(manifest_bytes, "CompiledBoard manifest");

    const auto count = read_u64(bytes, cursor);
    require(count <= max_entry_count, "CompiledBoard entry count exceeds its limit");
    auto payloads = std::map<std::string, std::string>{};
    auto previous = std::optional<std::string>{};
    for (auto index = std::uint64_t{0}; index < count; ++index) {
        auto path = read_sized(bytes, cursor, max_path_bytes, "path");
        require_canonical_path(path);
        require(!previous.has_value() || *previous < path,
                "CompiledBoard payload paths are not in canonical order");
        previous = path;
        auto payload = read_sized(bytes, cursor, max_payload_bytes, "payload");
        const auto [unused, inserted] = payloads.emplace(std::move(path), std::move(payload));
        static_cast<void>(unused);
        require(inserted, "CompiledBoard payload path is duplicated", ErrorCode::DuplicateName);
    }
    require(cursor == bytes.size(), "CompiledBoard archive has trailing bytes");
    return Archive{std::move(manifest), std::move(payloads)};
}

[[nodiscard]] std::map<std::string, std::string>
validate_entries(const Json &manifest, const std::map<std::string, std::string> &payloads) {
    const auto &entries = field(manifest, "entries", "CompiledBoard manifest");
    require(entries.is_array(), "CompiledBoard manifest entries must be an array");
    require(entries.size() == payloads.size(),
            "CompiledBoard manifest entry count does not match payloads");
    auto roles = std::map<std::string, std::string>{};
    auto previous = std::optional<std::string>{};
    for (const auto &entry : entries) {
        require_exact_keys(entry, {"digest", "path", "role"}, "CompiledBoard manifest entry");
        const auto path = string_field(entry, "path", "CompiledBoard manifest entry");
        const auto role = string_field(entry, "role", "CompiledBoard manifest entry");
        require_canonical_path(path);
        require(!previous.has_value() || *previous < path,
                "CompiledBoard manifest entries are not in canonical order");
        previous = path;
        const auto payload = payloads.find(path);
        require(payload != payloads.end(), "CompiledBoard manifest payload is missing",
                ErrorCode::UnknownEntity);
        require(ContentHash{string_field(entry, "digest", "CompiledBoard manifest entry")} ==
                    sha256_content_hash(payload->second),
                "CompiledBoard payload digest does not match bytes",
                ErrorCode::CrossReferenceViolation);
        const auto [unused, inserted] = roles.emplace(path, role);
        static_cast<void>(unused);
        require(inserted, "CompiledBoard manifest payload path is duplicated",
                ErrorCode::DuplicateName);
    }
    return roles;
}

void require_role(const std::map<std::string, std::string> &roles, std::string_view path,
                  std::string_view role) {
    const auto match = roles.find(std::string{path});
    require(match != roles.end(), "CompiledBoard required payload is missing",
            ErrorCode::UnknownEntity);
    require(match->second == role, "CompiledBoard payload role is invalid",
            ErrorCode::CrossReferenceViolation);
}

[[nodiscard]] std::string payload_at(const std::map<std::string, std::string> &payloads,
                                     std::string_view path) {
    const auto match = payloads.find(std::string{path});
    require(match != payloads.end(), "CompiledBoard required payload is missing",
            ErrorCode::UnknownEntity);
    return match->second;
}

void validate_origin_records(const Json &wrapper) {
    const auto &circuit = field(wrapper, "circuit", "CompiledBoard logical snapshot");
    auto components = std::set<std::string>{};
    for (const auto &component : field(circuit, "components", "CompiledBoard logical circuit")) {
        components.insert(string_field(component, "id", "CompiledBoard logical component"));
    }
    auto nets = std::set<std::string>{};
    for (const auto &net : field(circuit, "nets", "CompiledBoard logical circuit")) {
        nets.insert(string_field(net, "id", "CompiledBoard logical net"));
    }
    const auto &component_origins =
        field(wrapper, "component_origins", "CompiledBoard logical snapshot");
    const auto &net_origins = field(wrapper, "net_origins", "CompiledBoard logical snapshot");
    require(component_origins.is_array() && net_origins.is_array(),
            "CompiledBoard logical origin metadata must use arrays");
    auto previous_component = std::optional<std::string>{};
    for (const auto &origin : component_origins) {
        require_exact_keys(
            origin, {"component", "module_definition", "module_instance", "template_reference"},
            "CompiledBoard component origin");
        const auto canonical = origin.dump();
        require(!previous_component.has_value() || *previous_component < canonical,
                "CompiledBoard component origins are not in canonical order",
                ErrorCode::CrossReferenceViolation);
        previous_component = canonical;
        require(components.contains(
                    string_field(origin, "component", "CompiledBoard component origin")),
                "CompiledBoard component origin is dangling", ErrorCode::CrossReferenceViolation);
    }
    auto previous_net = std::optional<std::string>{};
    for (const auto &origin : net_origins) {
        require_exact_keys(origin, {"module_definition", "module_instance", "net", "template_net"},
                           "CompiledBoard net origin");
        const auto canonical = origin.dump();
        require(!previous_net.has_value() || *previous_net < canonical,
                "CompiledBoard net origins are not in canonical order",
                ErrorCode::CrossReferenceViolation);
        previous_net = canonical;
        require(nets.contains(string_field(origin, "net", "CompiledBoard net origin")),
                "CompiledBoard net origin is dangling", ErrorCode::CrossReferenceViolation);
    }
}

void validate_net_class_closure(const Json &circuit) {
    const auto net_classes = circuit.find("net_classes");
    if (net_classes == circuit.end()) {
        return;
    }
    const auto &classes = field(*net_classes, "classes", "CompiledBoard net classes");
    const auto &assignments = field(*net_classes, "net_assignments", "CompiledBoard net classes");
    require(classes.is_array() && assignments.is_array(),
            "CompiledBoard net-class fields must be arrays");
    auto defined = std::set<std::string>{};
    for (const auto &entry : classes) {
        defined.insert(string_field(entry, "id", "CompiledBoard net class"));
    }
    auto assigned = std::set<std::string>{};
    for (const auto &entry : assignments) {
        assigned.insert(string_field(entry, "net_class", "CompiledBoard net-class assignment"));
    }
    require(defined == assigned, "CompiledBoard logical snapshot contains an unassigned net class",
            ErrorCode::CrossReferenceViolation);
}

[[nodiscard]] std::map<std::string, ContentHash> validate_logical_snapshot(const Json &wrapper) {
    require_exact_keys(wrapper,
                       {"circuit", "component_definition_identities", "component_origins", "format",
                        "net_origins", "schema_version"},
                       "CompiledBoard logical snapshot");
    require(string_field(wrapper, "format", "CompiledBoard logical snapshot") ==
                logical_snapshot_format,
            "CompiledBoard logical snapshot format is unsupported");
    require(uint32_field(wrapper, "schema_version", "CompiledBoard logical snapshot") ==
                static_cast<std::uint32_t>(CompiledBoardSchemaVersion::V1),
            "CompiledBoard logical snapshot schema is unsupported");
    const auto &circuit = field(wrapper, "circuit", "CompiledBoard logical snapshot");
    require(circuit.find("module_definitions") == circuit.end() &&
                circuit.find("module_instances") == circuit.end(),
            "CompiledBoard logical snapshot contains mutable module templates");

    auto referenced_definitions = std::set<std::string>{};
    for (const auto &component : field(circuit, "components", "CompiledBoard logical circuit")) {
        referenced_definitions.insert(
            string_field(component, "definition", "CompiledBoard logical component"));
    }
    auto actual_definitions = std::set<std::string>{};
    auto ordered_definitions = std::vector<std::string>{};
    auto referenced_pins = std::set<std::string>{};
    for (const auto &definition :
         field(circuit, "component_definitions", "CompiledBoard logical circuit")) {
        require(definition.find("schematic_symbols") == definition.end(),
                "CompiledBoard logical snapshot contains Schematic references");
        const auto id =
            string_field(definition, "id", "CompiledBoard logical component definition");
        actual_definitions.insert(id);
        ordered_definitions.push_back(id);
        for (const auto &pin :
             field(definition, "pins", "CompiledBoard logical component definition")) {
            require(pin.is_string(), "CompiledBoard logical pin reference must be a string");
            referenced_pins.insert(pin.get<std::string>());
        }
    }
    require(referenced_definitions == actual_definitions,
            "CompiledBoard logical snapshot does not contain exactly referenced definitions",
            ErrorCode::CrossReferenceViolation);
    auto actual_pins = std::set<std::string>{};
    for (const auto &pin : field(circuit, "pin_definitions", "CompiledBoard logical circuit")) {
        actual_pins.insert(string_field(pin, "id", "CompiledBoard logical pin definition"));
    }
    require(referenced_pins == actual_pins,
            "CompiledBoard logical snapshot does not contain exactly referenced pin definitions",
            ErrorCode::CrossReferenceViolation);

    auto identities = std::map<std::string, ContentHash>{};
    const auto &identity_rows =
        field(wrapper, "component_definition_identities", "CompiledBoard logical snapshot");
    require(identity_rows.is_array(), "CompiledBoard component identity metadata must be an array");
    require(identity_rows.size() == ordered_definitions.size(),
            "CompiledBoard component identities do not cover the logical definitions",
            ErrorCode::CrossReferenceViolation);
    for (std::size_t index = 0; index < identity_rows.size(); ++index) {
        const auto &entry = identity_rows[index];
        require_exact_keys(entry, {"content_identity", "id"}, "CompiledBoard component identity");
        const auto id = string_field(entry, "id", "CompiledBoard component identity");
        require(id == ordered_definitions[index],
                "CompiledBoard component identities are not in canonical order",
                ErrorCode::CrossReferenceViolation);
        const auto [unused, inserted] =
            identities.emplace(id, ContentHash{string_field(entry, "content_identity",
                                                            "CompiledBoard component identity")});
        static_cast<void>(unused);
        require(inserted, "CompiledBoard component identity is duplicated",
                ErrorCode::DuplicateName);
    }
    validate_net_class_closure(circuit);
    validate_origin_records(wrapper);
    return identities;
}

[[nodiscard]] std::map<std::string, DecodedComponent>
decode_components(const Json &selected_parts, const std::map<std::string, std::string> &payloads,
                  const std::map<std::string, std::string> &roles,
                  std::set<std::string> &expected_paths) {
    auto identities = std::set<std::string>{};
    for (const auto &row : selected_parts) {
        identities.insert(string_field(row, "component_identity", "CompiledBoard selected part"));
    }

    auto result = std::map<std::string, DecodedComponent>{};
    for (const auto &identity_value : identities) {
        const auto identity = ContentHash{identity_value};
        const auto path = component_path(identity);
        expected_paths.insert(path);
        require_role(roles, path, "component_definition");
        const auto bytes = payload_at(payloads, path);
        const auto document = parse_canonical_json(bytes, "CompiledBoard component definition");
        auto owner = read_logical_circuit_text(bytes);
        require(parse_json(write_logical_circuit(owner), "materialized component definition") ==
                    document,
                "CompiledBoard component payload is not owner-canonical",
                ErrorCode::CrossReferenceViolation);
        require(owner.all<ComponentDefId>().size() == 1U && owner.all<ComponentId>().size() == 0U &&
                    owner.all<NetId>().size() == 0U,
                "CompiledBoard component payload has invalid cardinality");
        const auto &component = owner.get(ComponentDefId{0});
        require(component.content_identity() == identity,
                "CompiledBoard component payload identity is stale",
                ErrorCode::CrossReferenceViolation);
        auto logical_projection = document;
        auto &projected_definition = logical_projection["component_definitions"].at(0);
        projected_definition.erase("schematic_symbols");
        if (const auto contract = projected_definition.find("contract");
            contract != projected_definition.end()) {
            (*contract)["content_identity"] = logical_component_identity(owner, component).value();
        }
        result.emplace(identity_value, DecodedComponent{component, std::move(logical_projection)});
    }
    return result;
}

[[nodiscard]] std::vector<DecodedPart>
decode_parts(const Json &selected_parts, const std::map<std::string, DecodedComponent> &components,
             const std::map<std::string, std::string> &payloads,
             const std::map<std::string, std::string> &roles,
             const CompiledBoardCapabilities &capabilities, std::set<std::string> &expected_paths) {
    auto result = std::vector<DecodedPart>{};
    auto previous = std::optional<std::string>{};
    for (const auto &row : selected_parts) {
        require_exact_keys(row, {"component_identity", "footprint", "model_3d", "reference"},
                           "CompiledBoard selected part");
        const auto reference =
            reference_from_json(field(row, "reference", "CompiledBoard selected part"));
        const auto key = reference_key(reference);
        require(!previous.has_value() || *previous < key,
                "CompiledBoard selected parts are not in canonical order");
        previous = key;
        const auto component_identity =
            string_field(row, "component_identity", "CompiledBoard selected part");
        const auto component = components.find(component_identity);
        require(component != components.end(),
                "CompiledBoard selected part component payload is missing",
                ErrorCode::UnknownEntity);

        const auto part_document_path = part_path(reference);
        expected_paths.insert(part_document_path);
        require_role(roles, part_document_path, "part_definition");
        const auto part_bytes = payload_at(payloads, part_document_path);
        auto part = read_part_definition_text(part_bytes, component->second.definition);
        require(write_part_definition(part) == part_bytes,
                "CompiledBoard part definition bytes are not canonical");
        require(part.content_identity() == reference.part_digest() &&
                    part.implemented_component().value() == component_identity &&
                    part.identity().namespace_name() == reference.library_namespace() &&
                    part.identity().name() == reference.part_key().value() &&
                    part.identity().version() == reference.library_version(),
                "CompiledBoard selected part identity is stale",
                ErrorCode::CrossReferenceViolation);

        const auto expected_footprint =
            asset_json(reference, detail::footprint_asset_reference(part));
        const auto &footprint_identity = field(row, "footprint", "CompiledBoard selected part");
        require(footprint_identity == expected_footprint,
                "CompiledBoard selected footprint identity is stale",
                ErrorCode::CrossReferenceViolation);
        const auto footprint_path = asset_path(footprint_identity);
        expected_paths.insert(footprint_path);
        require_role(roles, footprint_path, "footprint");
        const auto footprint_bytes = payload_at(payloads, footprint_path);
        require(sha256_content_hash(footprint_bytes) ==
                    detail::footprint_asset_reference(part).digest(),
                "CompiledBoard selected footprint digest is stale",
                ErrorCode::CrossReferenceViolation);
        auto footprint = read_footprint_asset(footprint_bytes);
        require(write_footprint_asset(footprint) == footprint_bytes,
                "CompiledBoard footprint bytes are not canonical");

        auto model_bytes = std::optional<std::string>{};
        const auto &model_json = field(row, "model_3d", "CompiledBoard selected part");
        const auto &model = part.orderable_part().model_3d();
        if (capabilities.has(BoardAssetCapability::Models3D) && model.has_value()) {
            require(model_json.is_object(), "CompiledBoard models3d closure is incomplete",
                    ErrorCode::UnknownEntity);
            const auto expected_model =
                asset_json(reference, detail::model_asset_reference(*model));
            require(model_json == expected_model, "CompiledBoard selected 3D identity is stale",
                    ErrorCode::CrossReferenceViolation);
            const auto model_path = asset_path(model_json);
            expected_paths.insert(model_path);
            require_role(roles, model_path, "models3d");
            model_bytes = payload_at(payloads, model_path);
            require(
                sha256_content_hash(*model_bytes) == detail::model_asset_reference(*model).digest(),
                "CompiledBoard selected 3D digest is stale", ErrorCode::CrossReferenceViolation);
        } else {
            require(model_json.is_null(),
                    "CompiledBoard models3d closure exceeds declared capabilities",
                    ErrorCode::CrossReferenceViolation);
        }
        result.push_back(
            DecodedPart{key, std::move(part), std::move(footprint), std::move(model_bytes)});
    }
    return result;
}

[[nodiscard]] const DecodedPart &find_decoded_part(std::span<const DecodedPart> parts,
                                                   const LibraryPartRef &reference) {
    const auto key = reference_key(reference);
    const auto match = std::ranges::lower_bound(parts, key, {}, &DecodedPart::reference_key);
    require(match != parts.end() && match->reference_key == key,
            "CompiledBoard selected closure does not cover the logical reference",
            ErrorCode::UnknownEntity);
    return *match;
}

[[nodiscard]] std::string build_archive_bytes(const Circuit &circuit, const Board &board,
                                              const BoardResolution &resolution,
                                              const PartLibraryBundle &selected_closure,
                                              const CompiledBoardCapabilities &capabilities) {
    auto payloads = std::map<std::string, Payload>{};
    const auto logical = build_logical_snapshot(circuit);
    const auto physical = build_physical_snapshot(board);
    const auto capability = capabilities_json(capabilities).dump();
    add_payload(payloads, "snapshots/capabilities.json", "capability_snapshot", capability);
    add_payload(payloads, "snapshots/logical.json", "logical_dependencies", logical.bytes);
    add_payload(payloads, "snapshots/physical.json", "physical_snapshot", physical);
    const auto selected_parts = build_selected_closure(resolution, selected_closure, capabilities,
                                                       logical.component_documents, payloads);

    const auto logical_digest = sha256_content_hash(logical.bytes);
    const auto physical_digest = sha256_content_hash(physical);
    const auto capability_digest = sha256_content_hash(capability);
    const auto closure_digest = selected_closure_digest(selected_parts);
    const auto build = std::string{version_string()};
    const auto provenance = provenance_digest(board.name().value(), build, logical_digest,
                                              physical_digest, closure_digest, capability_digest);

    auto entries = Json::array();
    for (const auto &[unused, payload] : payloads) {
        static_cast<void>(unused);
        entries.push_back(entry_json(payload));
    }
    const auto manifest = Json{
        {"board_name", board.name().value()},
        {"capability_digest", capability_digest.value()},
        {"compiler_build", build},
        {"compiler_name", compiler_name},
        {"compiler_version", static_cast<std::uint32_t>(CompiledBoardCompilerVersion::V1)},
        {"entries", std::move(entries)},
        {"format", compiled_board_format_name()},
        {"logical_dependency_digest", logical_digest.value()},
        {"physical_snapshot_digest", physical_digest.value()},
        {"provenance_digest", provenance.value()},
        {"schema_version", static_cast<std::uint32_t>(CompiledBoardSchemaVersion::V1)},
        {"selected_closure_digest", closure_digest.value()},
        {"selected_parts", selected_parts},
    };
    return encode_archive(manifest, payloads);
}

[[nodiscard]] DiagnosticReport design_diagnostics(const Board &board,
                                                  const FootprintLibrary &footprints) {
    auto result = validate_for_pcb(board.circuit());
    const auto board_diagnostics = validate_board(board, footprints);
    for (const auto &diagnostic : board_diagnostics.diagnostics()) {
        result.add(diagnostic);
    }
    return result;
}

} // namespace

CompiledBoardCompileResult compile_board(const Circuit &circuit, const Board &board,
                                         const PartLibraryBundle &selected_closure,
                                         CompiledBoardCapabilities capabilities) {
    auto diagnostics = validate_for_pcb(board.circuit());
    auto archive_bytes = std::string{};
    try {
        if (&board.circuit() != &circuit) {
            throw KernelLogicError{
                ErrorCode::CrossReferenceViolation,
                "CompiledBoard Circuit and named Board have different logical owners"};
        }
        const auto resolution =
            resolve_board(board, selected_closure,
                          BoardResolutionCapabilities{
                              capabilities.profile(),
                              std::vector<BoardAssetCapability>{capabilities.additional().begin(),
                                                                capabilities.additional().end()}});
        archive_bytes =
            build_archive_bytes(circuit, board, resolution, selected_closure, capabilities);
    } catch (const KernelError &error) {
        return CompiledBoardCompileResult::failure(
            CompiledBoardFailure{error.code(), error.what(), error.entity()},
            std::move(diagnostics));
    }

    auto artifact = open_compiled_board(archive_bytes);
    diagnostics = design_diagnostics(artifact.board(), artifact.footprints());
    return CompiledBoardCompileResult::success(std::move(artifact), std::move(diagnostics));
}

std::string write_compiled_board(const CompiledBoard &compiled) {
    return std::string{compiled.bytes()};
}

namespace {

template <typename Publisher>
[[nodiscard]] CompiledBoard open_compiled_board_impl(std::string_view bytes, Publisher publisher) {
    auto archive = decode_archive(bytes);
    const auto &manifest = archive.manifest;
    require_exact_keys(manifest,
                       {"board_name", "capability_digest", "compiler_build", "compiler_name",
                        "compiler_version", "entries", "format", "logical_dependency_digest",
                        "physical_snapshot_digest", "provenance_digest", "schema_version",
                        "selected_closure_digest", "selected_parts"},
                       "CompiledBoard manifest");
    require(string_field(manifest, "format", "CompiledBoard manifest") ==
                compiled_board_format_name(),
            "CompiledBoard format is unsupported");
    require(uint32_field(manifest, "schema_version", "CompiledBoard manifest") ==
                static_cast<std::uint32_t>(CompiledBoardSchemaVersion::V1),
            "CompiledBoard schema is unsupported");
    require(string_field(manifest, "compiler_name", "CompiledBoard manifest") == compiler_name,
            "CompiledBoard compiler owner is unsupported");
    require(uint32_field(manifest, "compiler_version", "CompiledBoard manifest") ==
                static_cast<std::uint32_t>(CompiledBoardCompilerVersion::V1),
            "CompiledBoard compiler contract is unsupported");

    const auto roles = validate_entries(manifest, archive.payloads);
    require_role(roles, "snapshots/capabilities.json", "capability_snapshot");
    require_role(roles, "snapshots/logical.json", "logical_dependencies");
    require_role(roles, "snapshots/physical.json", "physical_snapshot");
    auto expected_paths = std::set<std::string>{
        "snapshots/capabilities.json",
        "snapshots/logical.json",
        "snapshots/physical.json",
    };

    const auto logical_bytes = payload_at(archive.payloads, "snapshots/logical.json");
    const auto physical_bytes = payload_at(archive.payloads, "snapshots/physical.json");
    const auto capability_bytes = payload_at(archive.payloads, "snapshots/capabilities.json");
    const auto logical_digest =
        ContentHash{string_field(manifest, "logical_dependency_digest", "CompiledBoard manifest")};
    const auto physical_digest =
        ContentHash{string_field(manifest, "physical_snapshot_digest", "CompiledBoard manifest")};
    const auto capability_digest =
        ContentHash{string_field(manifest, "capability_digest", "CompiledBoard manifest")};
    require(logical_digest == sha256_content_hash(logical_bytes) &&
                physical_digest == sha256_content_hash(physical_bytes) &&
                capability_digest == sha256_content_hash(capability_bytes),
            "CompiledBoard canonical input digest does not match payload bytes",
            ErrorCode::CrossReferenceViolation);

    const auto logical_json = parse_canonical_json(logical_bytes, "CompiledBoard logical snapshot");
    const auto component_identities = validate_logical_snapshot(logical_json);
    const auto &logical_circuit_json =
        field(logical_json, "circuit", "CompiledBoard logical snapshot");
    auto logical_circuit = read_logical_circuit_text(logical_circuit_json.dump());
    require(parse_json(write_logical_circuit(logical_circuit), "materialized logical circuit") ==
                logical_circuit_json,
            "CompiledBoard logical snapshot is not owner-canonical",
            ErrorCode::CrossReferenceViolation);

    const auto capability_json =
        parse_canonical_json(capability_bytes, "CompiledBoard capability snapshot");
    auto capabilities = capabilities_from_json(capability_json);

    const auto &selected_parts = field(manifest, "selected_parts", "CompiledBoard manifest");
    require(selected_parts.is_array(), "CompiledBoard selected parts must be an array");
    const auto closure_digest =
        ContentHash{string_field(manifest, "selected_closure_digest", "CompiledBoard manifest")};
    require(closure_digest == selected_closure_digest(selected_parts),
            "CompiledBoard selected closure digest is stale", ErrorCode::CrossReferenceViolation);
    const auto components =
        decode_components(selected_parts, archive.payloads, roles, expected_paths);
    auto parts = decode_parts(selected_parts, components, archive.payloads, roles, capabilities,
                              expected_paths);

    auto footprint_library = FootprintLibrary{};
    auto resolved_parts = std::vector<ResolvedBoardPart>{};
    auto selected_reference_keys = std::set<std::string>{};
    for (std::size_t index = 0; index < logical_circuit.all<ComponentId>().size(); ++index) {
        const auto component = ComponentId{index};
        const auto &instance = logical_circuit.get(component);
        require(!instance.selected_physical_part().has_value(),
                "CompiledBoard logical snapshot contains resolved physical truth");
        if (!instance.selected_library_part_ref().has_value()) {
            continue;
        }
        const auto &reference = *instance.selected_library_part_ref();
        selected_reference_keys.insert(reference_key(reference));
        const auto &decoded = find_decoded_part(parts, reference);
        const auto definition_id = component_def_id(instance.definition().index());
        const auto identity = component_identities.find(definition_id);
        require(identity != component_identities.end(),
                "CompiledBoard logical component identity is missing", ErrorCode::UnknownEntity);
        const auto closure_component = components.find(identity->second.value());
        require(closure_component != components.end(),
                "CompiledBoard logical component payload is missing", ErrorCode::UnknownEntity);
        require(component_contract_document(logical_circuit_json, definition_id) ==
                    closure_component->second.logical_projection,
                "CompiledBoard logical component differs from its selected closure",
                ErrorCode::CrossReferenceViolation);
        detail::add_exact_footprint(footprint_library, decoded.footprint);
        resolved_parts.push_back(detail::materialize_resolved_part(
            logical_circuit, component, decoded.part, decoded.footprint, decoded.model_bytes,
            identity->second));
    }
    require(selected_reference_keys.size() == selected_parts.size(),
            "CompiledBoard selected closure contains an unconsumed part",
            ErrorCode::CrossReferenceViolation);

    require(expected_paths.size() == roles.size() &&
                std::ranges::equal(expected_paths, std::views::keys(roles)),
            "CompiledBoard archive contains an extraneous closure payload",
            ErrorCode::CrossReferenceViolation);

    const auto physical_json =
        parse_canonical_json(physical_bytes, "CompiledBoard physical snapshot");
    require(physical_json.find("viewer") == physical_json.end(),
            "CompiledBoard physical snapshot contains derived viewer state");
    auto physical_board = read_pcb_board_text(logical_circuit, physical_bytes);
    require(physical_board.name().value() ==
                string_field(manifest, "board_name", "CompiledBoard manifest"),
            "CompiledBoard physical Board identity is stale", ErrorCode::CrossReferenceViolation);

    auto resolution_capabilities = BoardResolutionCapabilities{
        capabilities.profile(), std::vector<BoardAssetCapability>{capabilities.additional().begin(),
                                                                  capabilities.additional().end()}};
    const auto resolution = BoardResolution::materialize(
        physical_board, closure_digest, std::move(resolution_capabilities),
        std::move(footprint_library), std::move(resolved_parts));

    const auto build = string_field(manifest, "compiler_build", "CompiledBoard manifest");
    const auto expected_provenance =
        provenance_digest(resolution.board_name().value(), build, logical_digest, physical_digest,
                          closure_digest, capability_digest);
    const auto stored_provenance =
        ContentHash{string_field(manifest, "provenance_digest", "CompiledBoard manifest")};
    require(expected_provenance == stored_provenance, "CompiledBoard provenance digest is stale",
            ErrorCode::CrossReferenceViolation);

    auto provenance = CompiledBoardProvenance{
        CompiledBoardSchemaVersion::V1,
        CompiledBoardCompilerVersion::V1,
        std::string{compiler_name},
        build,
        logical_digest,
        physical_digest,
        closure_digest,
        capability_digest,
        stored_provenance,
    };
    auto compiled =
        publisher(std::move(logical_circuit), resolution, std::move(capabilities),
                  std::move(provenance), logical_bytes, physical_bytes, std::string{bytes});
    require(build_physical_snapshot(compiled.board()) == physical_bytes,
            "CompiledBoard materialized Board differs from its canonical physical snapshot",
            ErrorCode::InvalidState);
    return compiled;
}

} // namespace

CompiledBoard open_compiled_board(std::string_view bytes) {
    return CompiledBoard::Codec::open(bytes);
}

} // namespace volt::io

namespace volt {

CompiledBoard CompiledBoard::Codec::open(std::string_view bytes) {
    return io::open_compiled_board_impl(
        bytes, [](Circuit logical_dependencies, const BoardResolution &resolution,
                  CompiledBoardCapabilities capabilities, CompiledBoardProvenance provenance,
                  std::string logical_dependency_snapshot, std::string physical_snapshot,
                  std::string archive_bytes) {
            return CompiledBoard::materialize_verified(
                std::move(logical_dependencies), resolution, std::move(capabilities),
                std::move(provenance), std::move(logical_dependency_snapshot),
                std::move(physical_snapshot), std::move(archive_bytes));
        });
}

} // namespace volt
