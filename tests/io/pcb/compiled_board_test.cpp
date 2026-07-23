#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/pcb/compiled_board.hpp>

namespace {

using Json = nlohmann::json;

[[nodiscard]] std::string asset_key(const volt::PartAssetReference &reference) {
    return std::to_string(static_cast<unsigned int>(reference.kind())) + ":" + reference.key();
}

class MemoryResolver final : public volt::PartAssetResolver {
  public:
    void add(const volt::PartAssetReference &reference, std::string bytes) {
        assets_.insert_or_assign(asset_key(reference), std::move(bytes));
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto match = assets_.find(asset_key(reference));
        return match == assets_.end() ? std::nullopt : std::optional{match->second};
    }

  private:
    std::map<std::string, std::string> assets_;
};

[[nodiscard]] volt::ComponentSpec resistor_spec() {
    return volt::ComponentSpec{
        .name = "Compiled resistor",
        .pins = {volt::PinSpec{.name = "A", .number = "1"},
                 volt::PinSpec{.name = "B", .number = "2"}},
        .schematic_symbols = {volt::SchematicSymbolReference{"test.compiled:resistor"}},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.compiled-resistor@1"},
                .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"B"}},
            },
    };
}

[[nodiscard]] std::vector<volt::PartFootprintPad>
part_pads(const volt::FootprintDefinition &footprint) {
    auto result = std::vector<volt::PartFootprintPad>{};
    for (const auto &pad : footprint.pads()) {
        result.emplace_back(pad.label(), pad.position().x_mm(), pad.position().y_mm(),
                            pad.size().width_mm(), pad.size().height_mm());
    }
    return result;
}

[[nodiscard]] volt::BoardCapabilityProfile profile() {
    return volt::BoardCapabilityProfile{
        "CompiledBoard fixture",
        volt::BoardCapabilityProvenance{"Native fixture", "2026-07-23"},
        0.1,
        0.2,
        0.4,
        {}};
}

struct Fixture {
    volt::ComponentSpec spec;
    volt::PartLibraryBuilder builder;
    MemoryResolver resolver;
    volt::PartKey selected_key;
    volt::PartKey unused_key;
    std::string model_bytes;
};

[[nodiscard]] Fixture fixture(bool bad_footprint_mapping = false, bool implicit_contract = false) {
    auto spec = resistor_spec();
    if (implicit_contract) {
        spec.contract.reset();
    }
    auto owner = volt::Circuit{};
    const auto definition_id = owner.define_component(spec);
    const auto &definition = owner.get(definition_id);
    const auto footprint = volt::passive_0603_footprint();
    auto footprint_bytes = volt::io::write_footprint_asset(footprint);
    if (bad_footprint_mapping) {
        const auto incomplete =
            volt::FootprintDefinition{footprint.ref(), std::vector{footprint.pads().front()}};
        footprint_bytes = volt::io::write_footprint_asset(incomplete);
    }
    const auto footprint_reference = volt::PartAssetReference{
        volt::PartAssetKind::Footprint,
        "footprint:" + footprint.ref().library() + "/" + footprint.ref().name(),
        volt::sha256_content_hash(footprint_bytes)};
    auto model_bytes = std::string{"compiled-board-model-v1"};
    const auto model_reference = volt::PartModel3DReference{
        "glb", "resistor.glb", volt::sha256_content_hash(model_bytes), {0.0, 0.0, 0.0}, 0.0};

    const auto make_part = [&](std::string key, std::string mpn) {
        const auto &pin_keys = definition.contract().pin_keys();
        return volt::PartDefinition{
            definition,
            volt::PartIdentity{"test.compiled", key, "1"},
            volt::ElectricalRecordSet{2},
            {volt::PinPackageTerminalMapping{pin_keys[0], {volt::PackageTerminalKey{"1"}}},
             volt::PinPackageTerminalMapping{pin_keys[1], {volt::PackageTerminalKey{"2"}}}},
            {},
            volt::PartProvenance{},
            {},
            volt::OrderablePart{
                volt::ManufacturerPart{"Volt", std::move(mpn)},
                volt::PackageRef{"0603"},
                volt::HashedFootprintReference{footprint.ref(), footprint_reference.digest()},
                part_pads(footprint),
                {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                                 {volt::FootprintPadKey{"1"}}},
                 volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                                 {volt::FootprintPadKey{"2"}}}},
                {},
                model_reference}};
    };

    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.compiled", "1", volt::PartLibrarySchemaVersion::V1}};
    const auto selected_key = volt::PartKey{"compiled-resistor"};
    const auto unused_key = volt::PartKey{"unused-resistor"};
    builder.add_component(spec)
        .add_part(make_part(selected_key.value(), "USED-0603"))
        .add_part(make_part(unused_key.value(), "UNUSED-0603"));
    auto resolver = MemoryResolver{};
    resolver.add(footprint_reference, footprint_bytes);
    resolver.add(volt::PartAssetReference{volt::PartAssetKind::Model3D,
                                          "model:" + model_reference.format() + "/" +
                                              model_reference.file_name(),
                                          model_reference.hash()},
                 model_bytes);
    return Fixture{std::move(spec), std::move(builder), std::move(resolver),
                   selected_key,    unused_key,         std::move(model_bytes)};
}

struct Design {
    std::unique_ptr<volt::Circuit> circuit;
    volt::ComponentId component;
    std::optional<volt::ComponentId> second_component;
    volt::Board board;
};

[[nodiscard]] Design design(const volt::ComponentSpec &spec,
                            const volt::io::PartLibraryBundle &bundle, const volt::PartKey &key,
                            std::string name = "Main", bool place = true,
                            bool add_second_component = false) {
    auto circuit = std::make_unique<volt::Circuit>();
    const auto definition = circuit->define_component(spec);
    const auto component = circuit->instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    circuit->update(component, volt::SelectLibraryPart{bundle, bundle.require(key)});
    const auto net = circuit->add_net(volt::NetSpec{.name = volt::NetName{"LOOP"}});
    static_cast<void>(
        circuit->connect(net, volt::queries::pin_by_name(*circuit, component, "A").value()));
    static_cast<void>(
        circuit->connect(net, volt::queries::pin_by_name(*circuit, component, "B").value()));
    auto second_component = std::optional<volt::ComponentId>{};
    if (add_second_component) {
        second_component = circuit->instantiate_component(
            definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R2"}});
        circuit->update(*second_component, volt::SelectLibraryPart{bundle, bundle.require(key)});
        static_cast<void>(circuit->connect(
            net, volt::queries::pin_by_name(*circuit, *second_component, "A").value()));
        static_cast<void>(circuit->connect(
            net, volt::queries::pin_by_name(*circuit, *second_component, "B").value()));
    }
    auto board = volt::Board{*circuit, volt::BoardName{std::move(name)}};
    board.set_capability_profile(profile());
    if (place) {
        const auto front = board.add_layer(
            volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
        const auto back = board.add_layer(
            volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
        board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
        board.set_outline(
            volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
        static_cast<void>(board.place_component(volt::ComponentPlacement{
            component, volt::BoardPoint{2.0, 3.0}, volt::BoardRotation::degrees(0.0),
            volt::BoardSide::Top, false}));
        if (second_component.has_value()) {
            static_cast<void>(board.place_component(volt::ComponentPlacement{
                *second_component, volt::BoardPoint{12.0, 3.0}, volt::BoardRotation::degrees(0.0),
                volt::BoardSide::Top, false}));
        }
    }
    return Design{std::move(circuit), component, second_component, std::move(board)};
}

[[nodiscard]] volt::CompiledBoardCapabilities baseline_capabilities() {
    return volt::CompiledBoardCapabilities{profile()};
}

[[nodiscard]] volt::CompiledBoardCapabilities models3d_capabilities() {
    return volt::CompiledBoardCapabilities{profile(), {volt::BoardAssetCapability::Models3D}};
}

[[nodiscard]] volt::CompiledBoard take_success(volt::CompiledBoardCompileResult result) {
    const auto failure_message =
        result.failure().has_value() ? result.failure()->message() : std::string{"success"};
    INFO(failure_message);
    REQUIRE(result.has_artifact());
    REQUIRE_FALSE(result.failure().has_value());
    return std::move(result).take_artifact();
}

struct ArchiveEntry {
    std::string path;
    std::string payload;
};

struct Archive {
    Json manifest;
    std::vector<ArchiveEntry> entries;
};

[[nodiscard]] std::uint64_t read_u64(std::string_view bytes, std::size_t &cursor) {
    auto value = std::uint64_t{0};
    for (auto index = std::size_t{0}; index < 8U; ++index) {
        value = (value << 8U) | static_cast<unsigned char>(bytes.at(cursor++));
    }
    return value;
}

[[nodiscard]] std::string read_sized(std::string_view bytes, std::size_t &cursor) {
    const auto size = static_cast<std::size_t>(read_u64(bytes, cursor));
    const auto result = std::string{bytes.substr(cursor, size)};
    cursor += size;
    return result;
}

[[nodiscard]] Archive decode_archive(std::string_view bytes) {
    auto cursor = std::size_t{14U};
    auto manifest = Json::parse(read_sized(bytes, cursor));
    auto entries = std::vector<ArchiveEntry>{};
    const auto count = read_u64(bytes, cursor);
    for (auto index = std::uint64_t{0}; index < count; ++index) {
        entries.push_back(ArchiveEntry{read_sized(bytes, cursor), read_sized(bytes, cursor)});
    }
    return Archive{std::move(manifest), std::move(entries)};
}

void append_u64(std::string &bytes, std::uint64_t value) {
    for (auto shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<char>((value >> static_cast<unsigned>(shift)) & 0xffU));
    }
}

void append_sized(std::string &bytes, std::string_view value) {
    append_u64(bytes, static_cast<std::uint64_t>(value.size()));
    bytes.append(value);
}

[[nodiscard]] std::string encode_archive(const Archive &archive) {
    auto bytes = std::string{"VOLT-COMPILED\0", 14U};
    append_sized(bytes, archive.manifest.dump());
    append_u64(bytes, static_cast<std::uint64_t>(archive.entries.size()));
    for (const auto &entry : archive.entries) {
        append_sized(bytes, entry.path);
        append_sized(bytes, entry.payload);
    }
    return bytes;
}

void refresh_entry_digest(Archive &archive, const ArchiveEntry &payload) {
    for (auto &entry : archive.manifest["entries"]) {
        if (entry.at("path") == payload.path) {
            entry["digest"] = volt::sha256_content_hash(payload.payload).value();
        }
    }
}

void refresh_provenance(Archive &archive) {
    archive.manifest["provenance_digest"] =
        volt::sha256_content_hash(
            Json{
                {"board_name", archive.manifest["board_name"]},
                {"capability_digest", archive.manifest["capability_digest"]},
                {"compiler_build", archive.manifest["compiler_build"]},
                {"compiler_name", archive.manifest["compiler_name"]},
                {"compiler_version", archive.manifest["compiler_version"]},
                {"format", "volt.compiled-board.provenance"},
                {"logical_dependency_digest", archive.manifest["logical_dependency_digest"]},
                {"physical_snapshot_digest", archive.manifest["physical_snapshot_digest"]},
                {"schema_version", archive.manifest["schema_version"]},
                {"selected_closure_digest", archive.manifest["selected_closure_digest"]},
            }
                .dump())
            .value();
}

void refresh_logical_provenance(Archive &archive, const ArchiveEntry &logical) {
    archive.manifest["logical_dependency_digest"] =
        volt::sha256_content_hash(logical.payload).value();
    refresh_entry_digest(archive, logical);
    refresh_provenance(archive);
}

void refresh_capability_provenance(Archive &archive, const ArchiveEntry &capability) {
    archive.manifest["capability_digest"] = volt::sha256_content_hash(capability.payload).value();
    refresh_entry_digest(archive, capability);
    refresh_provenance(archive);
}

[[nodiscard]] bool has_diagnostic(const volt::DiagnosticReport &report, std::string_view code) {
    return std::ranges::any_of(report.diagnostics(), [&](const volt::Diagnostic &diagnostic) {
        return diagnostic.code().value() == code;
    });
}

} // namespace

TEST_CASE("CompiledBoard is deterministic and freezes one historical revision") {
    auto source = fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key, source.unused_key}, source.resolver);
    auto authored = design(source.spec, bundle, source.selected_key);

    const auto first = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                            bundle, baseline_capabilities()));
    const auto second = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                             bundle, baseline_capabilities()));
    const auto old_bytes = std::string{first.bytes()};
    const auto old_identity = first.identity();

    CHECK(std::string{first.bytes()} == std::string{second.bytes()});
    CHECK(first.content_digest() == second.content_digest());
    CHECK(first.identity() == second.identity());
    CHECK(first.board_name().value() == "Main");
    CHECK(first.parts().size() == 1U);
    CHECK_FALSE(first.parts().front().model_3d_bytes().has_value());
    CHECK(first.bytes().find("UNUSED-0603") == std::string_view::npos);
    CHECK(first.logical_dependency_snapshot().find("schematic_symbols") == std::string_view::npos);

    auto changed_board = volt::Board{*authored.circuit, volt::BoardName{"Main"}};
    changed_board.set_capability_profile(profile());
    static_cast<void>(changed_board.place_component(
        volt::ComponentPlacement{authored.component, volt::BoardPoint{8.0, 3.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, false}));
    const auto changed = take_success(
        volt::io::compile_board(*authored.circuit, changed_board, bundle, baseline_capabilities()));
    CHECK_FALSE(changed.identity() == old_identity);
    CHECK(std::string{changed.bytes()} != old_bytes);
    CHECK(std::string{first.bytes()} == old_bytes);
    CHECK(volt::io::open_compiled_board(old_bytes).identity() == old_identity);

    authored.circuit->update(authored.component,
                             volt::SelectLibraryPart{bundle, bundle.require(source.unused_key)});
    const auto logical_change = take_success(volt::io::compile_board(
        *authored.circuit, authored.board, bundle, baseline_capabilities()));
    CHECK_FALSE(logical_change.identity() == old_identity);
    CHECK(logical_change.bytes().find("UNUSED-0603") != std::string_view::npos);
    CHECK(std::string{first.bytes()} == old_bytes);
}

TEST_CASE("CompiledBoard revisions keep named Boards and storage lifetimes independent") {
    auto source = fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key}, source.resolver);
    auto first_design = design(source.spec, bundle, source.selected_key, "First");
    auto second_design = design(source.spec, bundle, source.selected_key, "Second");
    const auto first = take_success(volt::io::compile_board(
        *first_design.circuit, first_design.board, bundle, baseline_capabilities()));
    const auto second = take_success(volt::io::compile_board(
        *second_design.circuit, second_design.board, bundle, baseline_capabilities()));
    CHECK(first.board_name().value() == "First");
    CHECK(second.board_name().value() == "Second");
    CHECK_FALSE(first.identity() == second.identity());

    const auto offline_bytes = [] {
        auto offline_source = fixture();
        const auto offline_bundle = volt::io::PartLibraryBundle::build(
            offline_source.builder, std::array{offline_source.selected_key},
            offline_source.resolver);
        auto offline_design =
            design(offline_source.spec, offline_bundle, offline_source.selected_key, "Offline");
        const auto artifact =
            take_success(volt::io::compile_board(*offline_design.circuit, offline_design.board,
                                                 offline_bundle, baseline_capabilities()));
        return std::string{artifact.bytes()};
    }();
    auto reopened = volt::io::open_compiled_board(offline_bytes);
    auto moved = std::move(reopened);
    CHECK(moved.board().name().value() == "Offline");
    REQUIRE(moved.parts().size() == 1U);
    CHECK(volt::queries::selected_physical_part(moved.board().circuit(),
                                                moved.parts().front().component())
              .has_value());
    CHECK(moved.footprints().definitions().size() == 1U);
}

TEST_CASE(
    "CompiledBoard preserves distinct metadata for definitions sharing one semantic identity") {
    auto source = fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key}, source.resolver);
    auto alternate = source.spec;
    alternate.name = "Alternate compiled resistor";
    alternate.properties =
        volt::PropertyMap{{volt::PropertyKey{"presentation"}, volt::PropertyValue{"alternate"}}};

    auto circuit = volt::Circuit{};
    const auto first_definition = circuit.define_component(source.spec);
    const auto second_definition = circuit.define_component(alternate);
    REQUIRE(circuit.get(first_definition).content_identity() ==
            circuit.get(second_definition).content_identity());
    const auto first = circuit.instantiate_component(
        first_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto second = circuit.instantiate_component(
        second_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R2"}});
    circuit.update(first, volt::SelectLibraryPart{bundle, bundle.require(source.selected_key)});
    circuit.update(second, volt::SelectLibraryPart{bundle, bundle.require(source.selected_key)});
    auto board = volt::Board{circuit, volt::BoardName{"Shared semantic identity"}};
    board.set_capability_profile(profile());

    const auto compiled =
        take_success(volt::io::compile_board(circuit, board, bundle, baseline_capabilities()));
    const auto reopened = volt::io::open_compiled_board(compiled.bytes());
    const auto definitions = reopened.board().circuit().all<volt::ComponentDefId>();
    REQUIRE(definitions.size() == 2U);
    CHECK(reopened.board().circuit().get(volt::ComponentDefId{0}).name() == source.spec.name);
    CHECK(reopened.board().circuit().get(volt::ComponentDefId{1}).name() == alternate.name);
    CHECK(reopened.board()
              .circuit()
              .get(volt::ComponentDefId{1})
              .properties()
              .get(volt::PropertyKey{"presentation"}) == volt::PropertyValue{"alternate"});
    CHECK(reopened.parts().size() == 2U);
}

TEST_CASE("CompiledBoard preserves identity for an implicitly authored component contract") {
    auto source = fixture(false, true);
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key}, source.resolver);
    auto authored = design(source.spec, bundle, source.selected_key, "Implicit contract");

    const auto compiled = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                               bundle, baseline_capabilities()));
    const auto reopened = volt::io::open_compiled_board(compiled.bytes());
    CHECK(reopened.board().circuit().get(volt::ComponentDefId{0}).name() == source.spec.name);
    REQUIRE(reopened.parts().size() == 1U);
    CHECK(volt::queries::selected_physical_part(reopened.board().circuit(),
                                                reopened.parts().front().component())
              .has_value());
}

TEST_CASE("CompiledBoard rejects ownership and incomplete selected closure atomically") {
    auto source = fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key}, source.resolver);
    auto authored = design(source.spec, bundle, source.selected_key);
    auto other = design(source.spec, bundle, source.selected_key, "Other");

    const auto foreign =
        volt::io::compile_board(*authored.circuit, other.board, bundle, baseline_capabilities());
    CHECK_FALSE(foreign.has_artifact());
    REQUIRE(foreign.failure().has_value());
    CHECK(foreign.failure()->code() == volt::ErrorCode::CrossReferenceViolation);

    const auto empty = volt::io::PartLibraryBundle::build(
        source.builder, std::span<const volt::PartKey>{}, source.resolver);
    const auto missing =
        volt::io::compile_board(*authored.circuit, authored.board, empty, baseline_capabilities());
    CHECK_FALSE(missing.has_artifact());
    REQUIRE(missing.failure().has_value());

    auto readiness_circuit = volt::Circuit{};
    const auto readiness_definition = readiness_circuit.define_component(source.spec);
    static_cast<void>(readiness_circuit.instantiate_component(
        readiness_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R-FAIL"}}));
    auto readiness_board = volt::Board{readiness_circuit, volt::BoardName{"Failure diagnostics"}};
    readiness_board.set_capability_profile(profile());
    const auto readiness_failure =
        volt::io::compile_board(*authored.circuit, readiness_board, empty, baseline_capabilities());
    CHECK_FALSE(readiness_failure.has_artifact());
    REQUIRE(readiness_failure.failure().has_value());
    CHECK(has_diagnostic(readiness_failure.diagnostics(), "PHYSICAL_PART_REQUIRED"));
    CHECK(std::ranges::none_of(
        readiness_failure.diagnostics().diagnostics(), [](const volt::Diagnostic &diagnostic) {
            return diagnostic.category() ==
                   volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard};
        }));

    auto bad_source = fixture(true);
    const auto bad_bundle = volt::io::PartLibraryBundle::build(
        bad_source.builder, std::array{bad_source.selected_key}, bad_source.resolver);
    auto bad_design = design(bad_source.spec, bad_bundle, bad_source.selected_key);
    const auto bad_mapping = volt::io::compile_board(*bad_design.circuit, bad_design.board,
                                                     bad_bundle, baseline_capabilities());
    CHECK_FALSE(bad_mapping.has_artifact());
    REQUIRE(bad_mapping.failure().has_value());
}

TEST_CASE("CompiledBoard codec rejects corrupt unsafe stale and incomplete archives") {
    auto source = fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key}, source.resolver);
    auto authored = design(source.spec, bundle, source.selected_key);
    const auto artifact = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                               bundle, models3d_capabilities()));
    const auto canonical = std::string{artifact.bytes()};

    auto corrupt = canonical;
    corrupt.front() = 'X';
    CHECK_THROWS_AS(volt::io::open_compiled_board(corrupt), volt::KernelLogicError);

    auto extraneous = decode_archive(canonical);
    extraneous.manifest["unexpected"] = true;
    CHECK_THROWS_AS(volt::io::open_compiled_board(encode_archive(extraneous)),
                    volt::KernelLogicError);

    auto identity_shape = decode_archive(canonical);
    const auto identity_shape_logical =
        std::ranges::find(identity_shape.entries, "snapshots/logical.json", &ArchiveEntry::path);
    REQUIRE(identity_shape_logical != identity_shape.entries.end());
    auto identity_shape_json = Json::parse(identity_shape_logical->payload);
    identity_shape_json["component_definition_identities"] =
        Json{{"row", identity_shape_json["component_definition_identities"].at(0)}};
    identity_shape_logical->payload = identity_shape_json.dump();
    refresh_logical_provenance(identity_shape, *identity_shape_logical);
    try {
        static_cast<void>(volt::io::open_compiled_board(encode_archive(identity_shape)));
        FAIL("object-shaped component identities were accepted");
    } catch (const volt::KernelLogicError &error) {
        CHECK(std::string{error.what()} ==
              "CompiledBoard component identity metadata must be an array");
    }

    auto origin_shape = decode_archive(canonical);
    const auto origin_shape_logical =
        std::ranges::find(origin_shape.entries, "snapshots/logical.json", &ArchiveEntry::path);
    REQUIRE(origin_shape_logical != origin_shape.entries.end());
    auto origin_shape_json = Json::parse(origin_shape_logical->payload);
    origin_shape_json["component_origins"] = Json::object();
    origin_shape_logical->payload = origin_shape_json.dump();
    refresh_logical_provenance(origin_shape, *origin_shape_logical);
    try {
        static_cast<void>(volt::io::open_compiled_board(encode_archive(origin_shape)));
        FAIL("object-shaped logical origins were accepted");
    } catch (const volt::KernelLogicError &error) {
        CHECK(std::string{error.what()} == "CompiledBoard logical origin metadata must use arrays");
    }

    auto changed_metadata = decode_archive(canonical);
    const auto metadata_logical =
        std::ranges::find(changed_metadata.entries, "snapshots/logical.json", &ArchiveEntry::path);
    REQUIRE(metadata_logical != changed_metadata.entries.end());
    auto metadata_json = Json::parse(metadata_logical->payload);
    metadata_json["circuit"]["component_definitions"].at(0)["name"] = "Changed component metadata";
    metadata_logical->payload = metadata_json.dump();
    refresh_logical_provenance(changed_metadata, *metadata_logical);
    const auto metadata_reopen = volt::io::open_compiled_board(encode_archive(changed_metadata));
    CHECK_FALSE(metadata_reopen.identity() == artifact.identity());
    CHECK(metadata_reopen.board().circuit().get(volt::ComponentDefId{0}).name() ==
          "Changed component metadata");

    auto stale_logical = decode_archive(canonical);
    const auto logical =
        std::ranges::find(stale_logical.entries, "snapshots/logical.json", &ArchiveEntry::path);
    REQUIRE(logical != stale_logical.entries.end());
    auto logical_json = Json::parse(logical->payload);
    logical_json["circuit"]["pin_definitions"].at(0)["name"] = "Forged semantic pin";
    logical->payload = logical_json.dump();
    refresh_logical_provenance(stale_logical, *logical);
    CHECK_THROWS_AS(volt::io::open_compiled_board(encode_archive(stale_logical)),
                    volt::KernelLogicError);

    auto unknown_logical = decode_archive(canonical);
    const auto unknown =
        std::ranges::find(unknown_logical.entries, "snapshots/logical.json", &ArchiveEntry::path);
    REQUIRE(unknown != unknown_logical.entries.end());
    auto unknown_json = Json::parse(unknown->payload);
    unknown_json["circuit"]["ignored_extension"] = true;
    unknown->payload = unknown_json.dump();
    refresh_logical_provenance(unknown_logical, *unknown);
    try {
        static_cast<void>(volt::io::open_compiled_board(encode_archive(unknown_logical)));
        FAIL("non-owner logical snapshot was accepted");
    } catch (const volt::KernelLogicError &error) {
        CHECK(std::string{error.what()} == "CompiledBoard logical snapshot is not owner-canonical");
    }

    auto stale_component = decode_archive(canonical);
    const auto component_payload =
        std::ranges::find_if(stale_component.entries, [](const ArchiveEntry &entry) {
            return entry.path.starts_with("closure/components/");
        });
    REQUIRE(component_payload != stale_component.entries.end());
    auto component_json = Json::parse(component_payload->payload);
    component_json["component_definitions"].at(0)["schematic_symbols"].at(0)["ignored_extension"] =
        true;
    component_payload->payload = component_json.dump();
    refresh_entry_digest(stale_component, *component_payload);
    try {
        static_cast<void>(volt::io::open_compiled_board(encode_archive(stale_component)));
        FAIL("non-owner component payload was accepted");
    } catch (const volt::KernelLogicError &error) {
        CHECK(std::string{error.what()} ==
              "CompiledBoard component payload is not owner-canonical");
    }

    auto stale_capability = decode_archive(canonical);
    const auto capability_payload = std::ranges::find(
        stale_capability.entries, "snapshots/capabilities.json", &ArchiveEntry::path);
    REQUIRE(capability_payload != stale_capability.entries.end());
    auto stale_capability_json = Json::parse(capability_payload->payload);
    stale_capability_json["profile_document"]["profile"]["provenance"]["ignored_extension"] = true;
    capability_payload->payload = stale_capability_json.dump();
    refresh_capability_provenance(stale_capability, *capability_payload);
    try {
        static_cast<void>(volt::io::open_compiled_board(encode_archive(stale_capability)));
        FAIL("non-owner capability profile was accepted");
    } catch (const volt::KernelLogicError &error) {
        CHECK(std::string{error.what()} ==
              "CompiledBoard capability profile is not owner-canonical");
    }

    auto digest_mismatch = canonical;
    digest_mismatch.back() ^= 1;
    CHECK_THROWS_AS(volt::io::open_compiled_board(digest_mismatch), volt::KernelLogicError);

    auto unsafe = decode_archive(canonical);
    unsafe.entries.front().path.front() = '/';
    CHECK_THROWS_AS(volt::io::open_compiled_board(encode_archive(unsafe)), volt::KernelLogicError);

    auto missing_asset = decode_archive(canonical);
    const auto model = std::ranges::find_if(missing_asset.entries, [](const ArchiveEntry &entry) {
        return entry.path.starts_with("closure/assets/models3d/");
    });
    REQUIRE(model != missing_asset.entries.end());
    const auto model_path = model->path;
    missing_asset.entries.erase(model);
    auto &manifest_entries = missing_asset.manifest["entries"];
    manifest_entries.erase(std::ranges::find_if(
        manifest_entries, [&](const Json &entry) { return entry.at("path") == model_path; }));
    CHECK_THROWS_AS(volt::io::open_compiled_board(encode_archive(missing_asset)),
                    volt::KernelLogicError);

    auto unsupported = decode_archive(canonical);
    const auto capability =
        std::ranges::find(unsupported.entries, "snapshots/capabilities.json", &ArchiveEntry::path);
    REQUIRE(capability != unsupported.entries.end());
    auto capability_json = Json::parse(capability->payload);
    capability_json["additional_assets"] = Json::array({"unsupported"});
    capability->payload = capability_json.dump();
    const auto capability_digest = volt::sha256_content_hash(capability->payload);
    unsupported.manifest["capability_digest"] = capability_digest.value();
    for (auto &entry : unsupported.manifest["entries"]) {
        if (entry.at("path") == capability->path) {
            entry["digest"] = capability_digest.value();
        }
    }
    CHECK_THROWS_AS(volt::io::open_compiled_board(encode_archive(unsupported)),
                    volt::KernelLogicError);
}

TEST_CASE("CompiledBoard capabilities freeze exact consumed assets and retain design diagnostics") {
    auto source = fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key, source.unused_key}, source.resolver);
    auto authored = design(source.spec, bundle, source.selected_key, "Diagnostics", false);

    const auto baseline_result =
        volt::io::compile_board(*authored.circuit, authored.board, bundle, baseline_capabilities());
    REQUIRE(baseline_result.has_artifact());
    CHECK(has_diagnostic(baseline_result.diagnostics(), "PCB_COMPONENT_NOT_PLACED"));
    auto routing = design(source.spec, bundle, source.selected_key, "Routing", true, true);
    const auto routing_result =
        volt::io::compile_board(*routing.circuit, routing.board, bundle, baseline_capabilities());
    REQUIRE(routing_result.has_artifact());
    CHECK(has_diagnostic(routing_result.diagnostics(), volt::drc_diagnostic_codes::NetUnrouted));
    const auto baseline = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                               bundle, baseline_capabilities()));
    CHECK_FALSE(baseline.capabilities().has(volt::BoardAssetCapability::Models3D));
    CHECK_FALSE(baseline.parts().front().model_3d_bytes().has_value());

    const auto models = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                             bundle, models3d_capabilities()));
    CHECK(models.capabilities().has(volt::BoardAssetCapability::Models3D));
    REQUIRE(models.parts().front().model_3d_bytes().has_value());
    CHECK(*models.parts().front().model_3d_bytes() == source.model_bytes);
    CHECK(models.parts().size() == 1U);
    CHECK(models.bytes().find("UNUSED-0603") == std::string_view::npos);
    CHECK(models.provenance().compiler_name() == "volt.native.compile-board");
    CHECK(models.provenance().schema_version() == volt::CompiledBoardSchemaVersion::V1);
    CHECK_FALSE(models.identity() == baseline.identity());

    auto dnp_circuit = volt::Circuit{};
    const auto dnp_definition = dnp_circuit.define_component(source.spec);
    const auto dnp_component = dnp_circuit.instantiate_component(
        dnp_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R-DNP"}});
    dnp_circuit.update(dnp_component, volt::SetAssemblyIntent{.dnp = true});
    auto dnp_board = volt::Board{dnp_circuit, volt::BoardName{"DNP"}};
    dnp_board.set_capability_profile(profile());
    const auto empty_bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::span<const volt::PartKey>{}, source.resolver);
    const auto dnp_result =
        volt::io::compile_board(dnp_circuit, dnp_board, empty_bundle, baseline_capabilities());
    CHECK(dnp_result.has_artifact());
    CHECK_FALSE(has_diagnostic(dnp_result.diagnostics(), "PHYSICAL_PART_REQUIRED"));

    auto readiness_circuit = volt::Circuit{};
    const auto readiness_definition = readiness_circuit.define_component(source.spec);
    static_cast<void>(readiness_circuit.instantiate_component(
        readiness_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R-READY"}}));
    auto readiness_board = volt::Board{readiness_circuit, volt::BoardName{"Readiness diagnostics"}};
    readiness_board.set_capability_profile(profile());
    const auto readiness_result = volt::io::compile_board(readiness_circuit, readiness_board,
                                                          empty_bundle, baseline_capabilities());
    REQUIRE(readiness_result.has_artifact());
    CHECK(has_diagnostic(readiness_result.diagnostics(), "PHYSICAL_PART_REQUIRED"));

    CHECK_THROWS_AS((volt::CompiledBoardCapabilities{
                        profile(), {static_cast<volt::BoardAssetCapability>(999)}}),
                    volt::KernelArgumentError);
}
