#include "support/manufacturing_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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

#include <volt/adapters/kicad/pcb_writer.hpp>
#include <volt/circuit/bom/bom.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/io/assembly/cpl_writer.hpp>
#include <volt/io/bom/bom_writer.hpp>
#include <volt/io/electrical/electrical_input_io.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/parts/part_definition_writer.hpp>
#include <volt/io/pcb/board_completion.hpp>
#include <volt/io/pcb/board_resolution.hpp>
#include <volt/io/pcb/compiled_board.hpp>
#include <volt/io/pcb/compiled_board_consumers.hpp>
#include <volt/io/pcb/pcb_fabrication_writer.hpp>
#include <volt/pcb/queries/board_queries.hpp>
#include <volt/pcb/routing/board_spatial_index.hpp>

#include "../support/project_bundle_v2_board_test_support.hpp"
#include "support/board_completion_fixture.hpp"

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

[[nodiscard]] Fixture fixture(bool bad_footprint_mapping = false, bool implicit_contract = false,
                              std::string model_format = "glb", bool reference_symbol = true,
                              bool rich = false, bool duplicate_alternate = false) {
    auto spec = resistor_spec();
    if (!reference_symbol) {
        spec.schematic_symbols.clear();
    }
    if (implicit_contract) {
        spec.contract.reset();
    }
    if (rich) {
        spec.contract->relations = {volt::ContractDirectedRelation{
            volt::RelationKey{"rating"}, volt::PinKey{"A"}, volt::PinKey{"B"}}};
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
    const auto model_reference = volt::PartModel3DReference{model_format,
                                                            "resistor." + model_format,
                                                            volt::sha256_content_hash(model_bytes),
                                                            {0.0, 0.0, 0.0},
                                                            0.0};

    const auto make_part = [&](std::string key, std::string mpn) {
        const auto &pin_keys = definition.contract().pin_keys();
        auto records = volt::ElectricalRecordSet{2};
        auto electrical_model = std::optional<volt::PartElectricalModel>{};
        if (rich) {
            const auto subject = volt::ElectricalSubject::directed_relation(
                volt::ElectricalPinIndex{0}, volt::ElectricalPinIndex{1});
            records = volt::ElectricalRecordSet{
                2,
                {volt::voltage_record(subject, volt::ElectricalMeaning::AbsoluteLimit,
                                      volt::ElectricalValue{volt::QuantityRange::maximum(
                                          volt::Quantity{volt::UnitDimension::Voltage, 12.0})}),
                 volt::current_record(subject, volt::ElectricalMeaning::AbsoluteLimit,
                                      volt::ElectricalValue{volt::UnknownElectricalValue{}})}};
        }
        if (rich) {
            auto model = volt::PartElectricalModelBuilder{definition};
            const auto a = model.terminal(volt::ModelTerminalKey{"a"}, volt::PinKey{"A"});
            const auto b = model.terminal(volt::ModelTerminalKey{"b"}, volt::PinKey{"B"});
            model.add<volt::ResistanceElement>(
                volt::ModelElementKey{"body"}, a, b,
                volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, 1000.0},
                                     volt::Tolerance::percent(1.0)});
            electrical_model = model.build();
        }
        const auto alternates = duplicate_alternate ? std::vector<std::string>{mpn, "ALT-0603"}
                                                    : std::vector<std::string>{"ALT-0603"};
        return volt::PartDefinition{
            definition,
            volt::PartIdentity{"test.compiled", key, "1"},
            std::move(records),
            {volt::PinPackageTerminalMapping{pin_keys[0], {volt::PackageTerminalKey{"1"}}},
             volt::PinPackageTerminalMapping{pin_keys[1], {volt::PackageTerminalKey{"2"}}}},
            {},
            rich ? volt::PartProvenance{"datasheet revision 2", "native fixture", "test"}
                 : volt::PartProvenance{},
            rich ? std::vector{volt::PartSchematicAssetReference{
                       "retained-symbol", "default", volt::sha256_content_hash("symbol bytes")}}
                 : std::vector<volt::PartSchematicAssetReference>{},
            volt::OrderablePart{
                volt::ManufacturerPart{"Volt", std::move(mpn)},
                volt::PackageRef{"0603"},
                volt::HashedFootprintReference{footprint.ref(), footprint_reference.digest()},
                part_pads(footprint),
                {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                                 {volt::FootprintPadKey{"1"}}},
                 volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                                 {volt::FootprintPadKey{"2"}}}},
                rich ? alternates : std::vector<std::string>{},
                model_reference},
            std::move(electrical_model)};
    };

    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.compiled", "1", volt::PartLibrarySchemaVersion::V1}};
    const auto selected_key = volt::PartKey{"compiled-resistor"};
    const auto unused_key = volt::PartKey{"unused-resistor"};
    builder.add_component(spec)
        .add_part(make_part(selected_key.value(), "USED-0603"))
        .add_part(make_part(unused_key.value(), "UNUSED-0603"));
    auto resolver = MemoryResolver{};
    if (rich) {
        resolver.add(volt::PartAssetReference{volt::PartAssetKind::Schematic,
                                              "symbol:retained-symbol@default",
                                              volt::sha256_content_hash("symbol bytes")},
                     "symbol bytes");
    }
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

struct HierarchicalDesign {
    Design authored;
    volt::ModuleInstanceId bridge_instance;
    volt::PortDefId bridge_port;
    volt::NetId parent;
    volt::NetId alternate;
    volt::NetId isolated;
};

[[nodiscard]] HierarchicalDesign hierarchical_design(const volt::ComponentSpec &spec,
                                                     const volt::io::PartLibraryBundle &bundle,
                                                     const volt::PartKey &key, bool bridge = true) {
    auto circuit = std::make_unique<volt::Circuit>();
    const auto definition = circuit->define_component(spec);
    const auto &pins = circuit->get(definition).pins();
    const auto module = circuit->define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"Child"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"N"}, volt::NetKind::Signal}},
        .components = {volt::ModuleComponentTemplate{definition, volt::ReferenceDesignator{"R"}}},
        .connections = {volt::ModulePinConnectionSpec{volt::NetName{"N"},
                                                      volt::ReferenceDesignator{"R"}, pins[0]},
                        volt::ModulePinConnectionSpec{volt::NetName{"N"},
                                                      volt::ReferenceDesignator{"R"}, pins[1]}},
        .ports = {volt::ModulePortSpec{volt::PortName{"A"}, volt::NetName{"N"},
                                       volt::PortRole::Passive},
                  volt::ModulePortSpec{volt::PortName{"B"}, volt::NetName{"N"},
                                       volt::PortRole::Passive}},
    });
    const auto first_instance = circuit->instantiate_module(
        module, volt::ModuleInstanceSpec{.name = volt::ModuleInstanceName{"M1"}});
    const auto second_instance = circuit->instantiate_module(
        module, volt::ModuleInstanceSpec{.name = volt::ModuleInstanceName{"M2"}});
    const auto template_component = circuit->get(module).components()[0];
    const auto first_child =
        volt::queries::concrete_component_for(*circuit, first_instance, template_component).value();
    const auto second_child =
        volt::queries::concrete_component_for(*circuit, second_instance, template_component)
            .value();
    const auto component = circuit->instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto parent = circuit->add_net(volt::NetSpec{.name = volt::NetName{"PARENT"}});
    const auto alternate = circuit->add_net(volt::NetSpec{.name = volt::NetName{"ALTERNATE"}});
    const auto isolated = circuit->add_net(volt::NetSpec{.name = volt::NetName{"ISOLATED"}});
    static_cast<void>(circuit->add_net(volt::NetSpec{.name = volt::NetName{"SINGLETON"}}));
    for (const auto pin : {"A", "B"}) {
        static_cast<void>(
            circuit->connect(parent, volt::queries::pin_by_name(*circuit, component, pin).value()));
    }
    const auto ports = circuit->get(module).ports();
    static_cast<void>(circuit->bind_port(first_instance, ports[0], parent));
    static_cast<void>(circuit->bind_port(second_instance, ports[0], alternate));
    if (bridge) {
        static_cast<void>(circuit->bind_port(first_instance, ports[1], alternate));
    }
    for (const auto selected : {component, first_child, second_child}) {
        circuit->update(selected, volt::SelectLibraryPart{bundle, bundle.require(key)});
    }
    circuit->update(second_child, volt::SetAssemblyIntent{.dnp = true});
    auto board = volt::Board{*circuit, volt::BoardName{"Hierarchical"}};
    board.set_capability_profile(profile());
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    auto x = 2.0;
    for (const auto placed : {component, first_child, second_child}) {
        static_cast<void>(board.place_component(volt::ComponentPlacement{
            placed, volt::BoardPoint{x, 3.0}, volt::BoardRotation::degrees(0.0)}));
        x += 10.0;
    }
    return HierarchicalDesign{Design{std::move(circuit), component, second_child, std::move(board)},
                              first_instance,
                              ports[1],
                              parent,
                              alternate,
                              isolated};
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

[[nodiscard]] std::vector<std::string> diagnostic_signature(const volt::DiagnosticReport &report) {
    auto result = std::vector<std::string>{};
    result.reserve(report.diagnostics().size());
    for (const auto &diagnostic : report.diagnostics()) {
        result.push_back(std::to_string(static_cast<unsigned int>(diagnostic.severity())) + ":" +
                         diagnostic.code().value() + ":" + diagnostic.message());
    }
    return result;
}

[[nodiscard]] std::vector<std::string>
ratsnest_signature(std::span<const volt::RatsnestEdge> edges) {
    auto result = std::vector<std::string>{};
    result.reserve(edges.size());
    for (const auto &edge : edges) {
        result.push_back(std::to_string(edge.net().index()) + ":" +
                         std::to_string(edge.from().placement().index()) + ":" +
                         std::to_string(edge.from().pad().index()) + ":" +
                         std::to_string(edge.to().placement().index()) + ":" +
                         std::to_string(edge.to().pad().index()));
    }
    return result;
}

} // namespace

TEST_CASE("CompiledBoard V2 preserves chained hierarchical continuity for every physical query") {
    auto archived = std::string{};
    auto expected_partition = std::vector<std::vector<volt::NetId>>{};
    auto expected_edges = std::vector<std::string>{};
    auto expected_diagnostics = std::vector<std::string>{};
    auto expected_svg = std::string{};
    auto source_query_legal = false;
    const auto routed = GENERATE(false, true);
    const auto foreign_short = GENERATE(false, true);
    {
        auto source = fixture();
        const auto bundle = volt::io::PartLibraryBundle::build(
            source.builder, std::array{source.selected_key}, source.resolver);
        auto hierarchy = hierarchical_design(source.spec, bundle, source.selected_key);
        auto &authored = hierarchy.authored;
        const auto front = volt::BoardLayerId{0};
        if (routed) {
            static_cast<void>(authored.board.add_track(
                volt::BoardTrack{hierarchy.parent,
                                 front,
                                 {volt::BoardPoint{1.0, 3.0}, volt::BoardPoint{23.0, 3.0}},
                                 0.25}));
        }
        if (foreign_short) {
            static_cast<void>(authored.board.add_track(
                volt::BoardTrack{hierarchy.isolated,
                                 front,
                                 {volt::BoardPoint{11.175, 2.0}, volt::BoardPoint{11.175, 4.0}},
                                 0.25}));
        }
        const auto resolution = volt::io::resolve_board(
            authored.board, bundle, volt::BoardResolutionCapabilities{profile()});
        const auto live = resolution.view();
        CHECK(live.has_complete_logical_continuity());
        CHECK(live.logical_continuity().is_module_origin_net(volt::NetId{0}));
        CHECK_FALSE(live.logical_continuity().is_module_origin_net(hierarchy.parent));
        expected_partition = live.logical_continuity().partition();
        REQUIRE(expected_partition ==
                std::vector<std::vector<volt::NetId>>{
                    {volt::NetId{0}, volt::NetId{1}, volt::NetId{2}, volt::NetId{3}},
                    {volt::NetId{4}},
                    {volt::NetId{5}}});
        const auto source_edges = volt::queries::ratsnest_edges(live);
        REQUIRE(source_edges.size() == 5U);
        for (const auto &edge : source_edges) {
            CHECK(edge.net() == hierarchy.parent);
        }
        expected_edges = ratsnest_signature(source_edges);
        const auto source_report = volt::validate_board(live);
        CHECK(has_diagnostic(source_report, "PCB_NET_UNROUTED") == !routed);
        CHECK(has_diagnostic(source_report, "PCB_COPPER_CLEARANCE_VIOLATION") == foreign_short);
        expected_diagnostics = diagnostic_signature(source_report);
        const auto candidate = volt::BoardSpatialQueryShape{
            .kind = volt::BoardSpatialQueryShapeKind::Segment,
            .net = hierarchy.alternate,
            .layers = {front},
            .points = {volt::BoardPoint{1.0, 3.0}, volt::BoardPoint{23.0, 3.0}},
            .radius_mm = 0.125,
        };
        source_query_legal = volt::BoardSpatialIndex{live}.query_legality(candidate).legal;
        CHECK(source_query_legal == !foreign_short);
        const auto compiled = take_success(volt::io::compile_board(
            *authored.circuit, authored.board, bundle, baseline_capabilities()));
        CHECK(compiled.provenance().schema_version() == volt::CompiledBoardSchemaVersion::V2);
        CHECK(compiled.provenance().compiler_version() == volt::CompiledBoardCompilerVersion::V2);
        CHECK(compiled.logical_continuity_partition() == expected_partition);
        CHECK(compiled.view().has_complete_logical_continuity());
        CHECK(compiled.board().circuit().all<volt::PortBindingId>().size() == 0U);
        CHECK(compiled.board().circuit().all<volt::ModuleInstanceId>().size() == 0U);
        CHECK(ratsnest_signature(volt::compute_ratsnest(compiled).edges()) == expected_edges);
        CHECK(diagnostic_signature(volt::validate_board(compiled).diagnostics()) ==
              expected_diagnostics);
        CHECK(volt::BoardSpatialIndex{compiled.view()}.query_legality(candidate).legal ==
              source_query_legal);
        expected_svg = std::string{volt::io::render_pcb_svg(compiled).bytes()};
        archived = std::string{compiled.bytes()};
    }
    // Every original Circuit, resolver, selected bundle and fresh artifact is now unavailable.
    const auto reopened = volt::io::open_compiled_board(archived);
    CHECK(reopened.logical_continuity_partition() == expected_partition);
    CHECK(reopened.view().has_complete_logical_continuity());
    CHECK(ratsnest_signature(volt::compute_ratsnest(reopened).edges()) == expected_edges);
    CHECK(diagnostic_signature(volt::validate_board(reopened).diagnostics()) ==
          expected_diagnostics);
    CHECK(volt::io::render_pcb_svg(reopened).bytes() == expected_svg);
    const auto candidate = volt::BoardSpatialQueryShape{
        .kind = volt::BoardSpatialQueryShapeKind::Segment,
        .net = volt::NetId{3},
        .layers = {volt::BoardLayerId{0}},
        .points = {volt::BoardPoint{1.0, 3.0}, volt::BoardPoint{23.0, 3.0}},
        .radius_mm = 0.125,
    };
    CHECK(volt::BoardSpatialIndex{reopened.view()}.query_legality(candidate).legal ==
          source_query_legal);
}

TEST_CASE("CompiledBoard V2 freezes bindings while existing authoring views remain live") {
    auto source = fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key}, source.resolver);
    auto hierarchy = hierarchical_design(source.spec, bundle, source.selected_key, false);
    auto &authored = hierarchy.authored;
    const auto resolution = volt::io::resolve_board(authored.board, bundle,
                                                    volt::BoardResolutionCapabilities{profile()});
    const auto live = resolution.view();
    const auto authoring_index = volt::BoardSpatialIndex{live};
    const auto before = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                             bundle, baseline_capabilities()));
    const auto historical_index = volt::BoardSpatialIndex{before.view()};
    const auto partition_before = *before.logical_continuity_partition();
    const auto candidate = volt::BoardSpatialQueryShape{
        .kind = volt::BoardSpatialQueryShapeKind::Segment,
        .net = hierarchy.alternate,
        .layers = {volt::BoardLayerId{0}},
        .points = {volt::BoardPoint{1.0, 3.0}, volt::BoardPoint{23.0, 3.0}},
        .radius_mm = 0.125,
    };
    CHECK_FALSE(authoring_index.query_legality(candidate).legal);
    CHECK_FALSE(historical_index.query_legality(candidate).legal);
    static_cast<void>(authored.circuit->bind_port(hierarchy.bridge_instance, hierarchy.bridge_port,
                                                  hierarchy.alternate));
    CHECK(authoring_index.query_legality(candidate).legal);
    CHECK_FALSE(historical_index.query_legality(candidate).legal);
    CHECK(live.logical_continuity().partition() != partition_before);
    CHECK(*before.logical_continuity_partition() == partition_before);
    const auto after = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                            bundle, baseline_capabilities()));
    CHECK(after.provenance().logical_dependency_digest() !=
          before.provenance().logical_dependency_digest());
    CHECK_FALSE(after.identity() == before.identity());
    CHECK(std::string{after.physical_snapshot()} == std::string{before.physical_snapshot()});
    CHECK(ratsnest_signature(volt::compute_ratsnest(after).edges()) !=
          ratsnest_signature(volt::compute_ratsnest(before).edges()));
    auto equivalent = hierarchical_design(source.spec, bundle, source.selected_key);
    const auto identical = take_success(volt::io::compile_board(
        *equivalent.authored.circuit, equivalent.authored.board, bundle, baseline_capabilities()));
    CHECK(std::string{identical.bytes()} == std::string{after.bytes()});
}

TEST_CASE("CompiledBoard V2 structurally rejects malformed partitions and version combinations") {
    auto source = fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key}, source.resolver);
    auto hierarchy = hierarchical_design(source.spec, bundle, source.selected_key);
    const auto compiled = take_success(volt::io::compile_board(
        *hierarchy.authored.circuit, hierarchy.authored.board, bundle, baseline_capabilities()));
    const auto canonical = std::string{compiled.bytes()};
    const auto malformed = std::vector<Json>{
        Json::array(),
        Json::array({Json::array(), Json::array({"net:0", "net:1", "net:2", "net:3"}),
                     Json::array({"net:4"}), Json::array({"net:5"})}),
        Json::array({Json::array({"net:0", "net:1", "net:2", "net:3"}), Json::array({"net:4"})}),
        Json::array({Json::array({"net:0", "net:1", "net:2", "net:3"}),
                     Json::array({"net:3", "net:4"}), Json::array({"net:5"})}),
        Json::array({Json::array({"net:0", "net:0", "net:1", "net:2", "net:3"}),
                     Json::array({"net:4"}), Json::array({"net:5"})}),
        Json::array({Json::array({"net:0", "net:2", "net:1", "net:3"}), Json::array({"net:4"}),
                     Json::array({"net:5"})}),
        Json::array({Json::array({"net:4"}), Json::array({"net:0", "net:1", "net:2", "net:3"}),
                     Json::array({"net:5"})}),
        Json::array({Json::array({"net:0", "net:1", "net:2", "net:3"}), Json::array({"net:4"}),
                     Json::array({"net:99"})}),
        Json::array({Json::array({0, 1, 2, 3}), Json::array({"net:4"}), Json::array({"net:5"})}),
        Json::array({Json::object()}),
        Json::object(),
    };
    for (const auto &partition : malformed) {
        INFO(partition.dump());
        auto archive = decode_archive(canonical);
        auto logical =
            std::ranges::find(archive.entries, "snapshots/logical.json", &ArchiveEntry::path);
        REQUIRE(logical != archive.entries.end());
        auto wrapper = Json::parse(logical->payload);
        wrapper["realized_net_partition"] = partition;
        logical->payload = wrapper.dump();
        // Refresh every content hash: structural verification, not hashing, must reject these.
        refresh_logical_provenance(archive, *logical);
        CHECK_THROWS_AS(volt::io::open_compiled_board(encode_archive(archive)), volt::KernelError);
    }
    auto tampered = decode_archive(canonical);
    auto logical =
        std::ranges::find(tampered.entries, "snapshots/logical.json", &ArchiveEntry::path);
    REQUIRE(logical != tampered.entries.end());
    auto wrapper = Json::parse(logical->payload);
    wrapper["realized_net_partition"] =
        Json::array({Json::array({"net:0"}), Json::array({"net:1"}), Json::array({"net:2"}),
                     Json::array({"net:3"}), Json::array({"net:4"}), Json::array({"net:5"})});
    logical->payload = wrapper.dump();
    refresh_entry_digest(tampered, *logical);
    CHECK_THROWS_AS(volt::io::open_compiled_board(encode_archive(tampered)),
                    volt::KernelLogicError);
    for (const auto version : {0U, 1U, 3U}) {
        auto mismatched = decode_archive(canonical);
        mismatched.manifest["compiler_version"] = version;
        refresh_provenance(mismatched);
        CHECK_THROWS_AS(volt::io::open_compiled_board(encode_archive(mismatched)),
                        volt::KernelLogicError);
    }
    auto missing = decode_archive(canonical);
    auto missing_logical =
        std::ranges::find(missing.entries, "snapshots/logical.json", &ArchiveEntry::path);
    REQUIRE(missing_logical != missing.entries.end());
    auto missing_wrapper = Json::parse(missing_logical->payload);
    missing_wrapper.erase("realized_net_partition");
    missing_logical->payload = missing_wrapper.dump();
    refresh_logical_provenance(missing, *missing_logical);
    CHECK_THROWS_AS(volt::io::open_compiled_board(encode_archive(missing)), volt::KernelLogicError);

    auto foreign_origin = decode_archive(canonical);
    auto origin_logical =
        std::ranges::find(foreign_origin.entries, "snapshots/logical.json", &ArchiveEntry::path);
    REQUIRE(origin_logical != foreign_origin.entries.end());
    auto origin_wrapper = Json::parse(origin_logical->payload);
    REQUIRE_FALSE(origin_wrapper["net_origins"].empty());
    origin_wrapper["net_origins"][0]["net"] = "net:99";
    origin_logical->payload = origin_wrapper.dump();
    refresh_logical_provenance(foreign_origin, *origin_logical);
    try {
        static_cast<void>(volt::io::open_compiled_board(encode_archive(foreign_origin)));
        FAIL("foreign net origin was accepted");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::CrossReferenceViolation);
    }

    auto legacy_with_partition = decode_archive(canonical);
    legacy_with_partition.manifest["schema_version"] = 1U;
    legacy_with_partition.manifest["compiler_version"] = 1U;
    auto legacy_logical = std::ranges::find(legacy_with_partition.entries, "snapshots/logical.json",
                                            &ArchiveEntry::path);
    REQUIRE(legacy_logical != legacy_with_partition.entries.end());
    auto legacy_wrapper = Json::parse(legacy_logical->payload);
    legacy_wrapper["schema_version"] = 1U;
    legacy_logical->payload = legacy_wrapper.dump();
    refresh_logical_provenance(legacy_with_partition, *legacy_logical);
    CHECK_THROWS_AS(volt::io::open_compiled_board(encode_archive(legacy_with_partition)),
                    volt::KernelError);
}

TEST_CASE("CompiledBoard V1 remains byte-preserving and exportable without inferred continuity") {
    // This Board-only publication has no schematic symbol closure to publish.
    auto source = fixture(false, false, "glb", false);
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key}, source.resolver);
    auto hierarchy = hierarchical_design(source.spec, bundle, source.selected_key);
    const auto compiled = take_success(volt::io::compile_board(
        *hierarchy.authored.circuit, hierarchy.authored.board, bundle, baseline_capabilities()));
    auto legacy = decode_archive(compiled.bytes());
    legacy.manifest["schema_version"] = 1U;
    legacy.manifest["compiler_version"] = 1U;
    auto logical = std::ranges::find(legacy.entries, "snapshots/logical.json", &ArchiveEntry::path);
    REQUIRE(logical != legacy.entries.end());
    auto wrapper = Json::parse(logical->payload);
    wrapper["schema_version"] = 1U;
    wrapper.erase("realized_net_partition");
    logical->payload = wrapper.dump();
    refresh_logical_provenance(legacy, *logical);
    const auto legacy_bytes = encode_archive(legacy);
    const auto reopened = volt::io::open_compiled_board(legacy_bytes);
    CHECK(reopened.provenance().schema_version() == volt::CompiledBoardSchemaVersion::V1);
    const auto completion = volt::io::evaluate_fabrication_completion(reopened);
    CHECK(completion.coverage()[0].state == volt::BoardCompletionState::NotEvaluated);
    CHECK(completion.status() != volt::BoardCompletionStatus::Complete);
    const auto target_process = volt::test::manufacturing_process();
    const auto target_profile = volt::ManufacturingProfile{
        reopened.capabilities().profile(), volt::sha256_content_hash("Synthetic V1 target"),
        target_process};
    const auto target = volt::ManufacturingTarget{reopened.identity(), reopened.content_digest(),
                                                  volt::ManufacturingGoal::Fabrication,
                                                  target_process, target_profile};
    const auto coverage = volt::evaluate_manufacturing_coverage(reopened, target);
    CHECK_FALSE(coverage.ready());
    CHECK(coverage.coverage()[static_cast<std::size_t>(volt::ManufacturingCheck::LogicalContinuity)]
              .state == volt::ManufacturingState::NotEvaluated);

    CHECK(
        coverage.coverage()[static_cast<std::size_t>(volt::ManufacturingCheck::SelectedPartRatings)]
            .state == volt::ManufacturingState::NotEvaluated);
    REQUIRE(coverage.ratings());
    for (const auto &subject : coverage.ratings()->subjects) {
        CHECK(subject.state != volt::SelectedPartRatingState::Pass);
        CHECK(subject.observations.empty());
    }

    CHECK_FALSE(volt::io::write_pcb_fabrication_files(reopened).files.empty());
    CHECK_FALSE(reopened.logical_continuity_partition().has_value());
    CHECK_FALSE(reopened.view().has_complete_logical_continuity());
    const auto validation = volt::validate_board(reopened);
    const auto &report = validation.diagnostics();
    CHECK(std::ranges::count_if(report.diagnostics(), [](const auto &diagnostic) {
              return diagnostic.code().value() == "PCB_LOGICAL_CONTINUITY_UNAVAILABLE" &&
                     diagnostic.severity() == volt::Severity::Warning;
          }) == 1);
    CHECK_FALSE(has_diagnostic(volt::validate_board(compiled).diagnostics(),
                               "PCB_LOGICAL_CONTINUITY_UNAVAILABLE"));
    CHECK(diagnostic_signature(volt::validate_board(reopened.view())) ==
          diagnostic_signature(report));
    CHECK(volt::io::write_compiled_board(reopened) == legacy_bytes);
    for (const auto &part : reopened.parts()) {
        CHECK(volt::io::write_part_definition(
                  reopened.part_definitions().resolve(part.reference())) ==
              volt::io::write_part_definition(bundle.resolve(part.reference())));
    }
    CHECK_FALSE(reopened.identity() == compiled.identity());
    CHECK(volt::compute_ratsnest(reopened).edges().size() == 3U);
    CHECK_FALSE(volt::io::render_pcb_svg(reopened).bytes().empty());
    CHECK_FALSE(volt::io::write_pcb_fabrication_files(reopened).files.empty());
    CHECK_FALSE(volt::adapters::kicad::write_board(reopened).text.empty());

    CHECK(volt::io::compiled_board_logical_dependency_digest(
              *hierarchy.authored.circuit, volt::CompiledBoardSchemaVersion::V1) ==
          reopened.provenance().logical_dependency_digest());
    CHECK(volt::io::compiled_board_logical_dependency_digest(
              *hierarchy.authored.circuit, volt::CompiledBoardSchemaVersion::V2) ==
          compiled.provenance().logical_dependency_digest());
    auto builder = volt::test::project_bundle_v2::project_builder();
    const auto scene = volt::prepare_board_scene(reopened);
    builder.add_logical(volt::io::DesignKey{"main"}, *hierarchy.authored.circuit, bundle);
    builder.add_board(volt::io::DesignKey{"main"}, hierarchy.authored.board, reopened, scene,
                      bundle);
    const auto publication = builder.build();
    const auto &compiled_descriptor = volt::test::project_bundle_v2::descriptor(
        publication, volt::io::ArtifactKind::CompiledBoard);
    CHECK(compiled_descriptor.schema_version() == 1U);
    const auto temporary = volt::test::project_bundle_v2::TempDirectory{};
    const auto path = temporary.path() / "legacy.volt";
    publication.write(path);
    const auto legacy_bundle = volt::io::ProjectBundle::open(path);
    const auto loaded = legacy_bundle.graph().loaded_project().compiled_boards();
    REQUIRE(loaded.size() == 1U);
    CHECK(loaded.front().model().provenance().schema_version() ==
          volt::CompiledBoardSchemaVersion::V1);
    CHECK_FALSE(loaded.front().model().logical_continuity_partition().has_value());
    CHECK(std::string{loaded.front().model().bytes()} == legacy_bytes);
    CHECK_FALSE(volt::io::render_pcb_svg(loaded.front().model()).bytes().empty());
}

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
    const auto original_delivery = volt::adapters::kicad::write_board(first);
    const auto changed_delivery = volt::adapters::kicad::write_board(changed);
    CHECK_FALSE(changed.identity() == old_identity);
    CHECK(original_delivery.source == old_identity);
    CHECK(changed_delivery.source == changed.identity());
    CHECK_FALSE(changed_delivery.source == original_delivery.source);
    CHECK(changed_delivery.text != original_delivery.text);
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

TEST_CASE("KiCad and fabrication delivery are deterministic from a reopened CompiledBoard") {
    auto archive_bytes = std::string{};
    auto expected_kicad = std::string{};
    auto expected_fabrication = std::vector<volt::io::PcbFabricationFile>{};
    auto source_identity =
        volt::CompiledBoardIdentity{volt::BoardName{"Main"}, volt::sha256_content_hash("pending")};

    {
        auto source = fixture();
        const auto bundle = volt::io::PartLibraryBundle::build(
            source.builder, std::array{source.selected_key}, source.resolver);
        auto authored = design(source.spec, bundle, source.selected_key);
        auto compiled = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                             bundle, baseline_capabilities()));
        const auto kicad = volt::adapters::kicad::write_board(compiled);
        const auto fabrication = volt::io::write_pcb_fabrication_files(compiled);

        source_identity = compiled.identity();
        archive_bytes = std::string{compiled.bytes()};
        expected_kicad = kicad.text;
        expected_fabrication = fabrication.files;
        CHECK(kicad.source == source_identity);
        CHECK(fabrication.source == source_identity);
    }

    const auto reopened = volt::io::open_compiled_board(archive_bytes);
    const auto kicad = volt::adapters::kicad::write_board(reopened);
    const auto fabrication = volt::io::write_pcb_fabrication_files(reopened);
    const auto repeated = volt::io::write_pcb_fabrication_files(reopened);

    CHECK(reopened.identity() == source_identity);
    CHECK(kicad.source == source_identity);
    CHECK(kicad.text == expected_kicad);
    CHECK(fabrication.source == source_identity);
    REQUIRE(fabrication.files.size() == expected_fabrication.size());
    REQUIRE(repeated.files.size() == fabrication.files.size());
    for (auto index = std::size_t{0}; index < fabrication.files.size(); ++index) {
        CHECK(fabrication.files[index].filename == expected_fabrication[index].filename);
        CHECK(fabrication.files[index].text == expected_fabrication[index].text);
        CHECK(repeated.files[index].filename == fabrication.files[index].filename);
        CHECK(repeated.files[index].text == fabrication.files[index].text);
    }
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
    CHECK(volt::adapters::kicad::write_board(first).source == first.identity());
    CHECK(volt::adapters::kicad::write_board(second).source == second.identity());
    CHECK(volt::io::write_pcb_fabrication_files(first).source == first.identity());
    CHECK(volt::io::write_pcb_fabrication_files(second).source == second.identity());

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
    REQUIRE(moved.part(moved.parts().front().component()) != nullptr);
    CHECK(moved.part(moved.parts().front().component())->physical_part().manufacturer_part() ==
          moved.parts().front().physical_part().manufacturer_part());
    CHECK(moved.footprints().definitions().size() == 1U);
    REQUIRE(moved.placements().size() == 1U);
    CHECK(moved.placements().front().placement() == volt::ComponentPlacementId{0});
    CHECK(moved.placements().front().status() == volt::CompiledBoardPlacementStatus::Resolved);
    REQUIRE(moved.placements().front().footprint().has_value());
    CHECK(moved.placements().front().footprint()->ref().name() ==
          volt::passive_0603_footprint().ref().name());
    CHECK(moved.placements().front().pad_resolutions().size() == 2U);
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
    REQUIRE(reopened.part(reopened.parts().front().component()) != nullptr);
    CHECK(reopened.part(reopened.parts().front().component())->reference() ==
          reopened.parts().front().reference());
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
    CHECK(models.provenance().schema_version() == volt::CompiledBoardSchemaVersion::V2);
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

TEST_CASE("CompiledBoard consumers reopen offline with deterministic identity-preserving parity") {
    struct Witness {
        std::string archive;
        std::vector<std::string> diagnostics;
        std::vector<std::string> ratsnest;
        std::string cpl_json;
        std::string svg;
        std::string layer_svg;
        std::string model_bytes;
    };

    const auto witness = [] {
        auto source = fixture();
        const auto bundle = volt::io::PartLibraryBundle::build(
            source.builder, std::array{source.selected_key, source.unused_key}, source.resolver);
        auto authored = design(source.spec, bundle, source.selected_key);
        auto artifact = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                             bundle, models3d_capabilities()));

        const auto validation = volt::validate_board(artifact);
        const auto view_validation = volt::validate_board(artifact.view());
        CHECK(validation.source() == artifact.identity());
        CHECK(diagnostic_signature(validation.diagnostics()) ==
              diagnostic_signature(view_validation));
        CHECK(has_diagnostic(validation.diagnostics(), "PCB_RULE_AT_CAPABILITY_MINIMUM"));
        CHECK(has_diagnostic(validation.diagnostics(), "PCB_NET_UNROUTED"));

        const auto ratsnest = volt::compute_ratsnest(artifact);
        const auto view_ratsnest = volt::queries::ratsnest_edges(artifact.view());
        CHECK(ratsnest.source() == artifact.identity());
        CHECK(ratsnest_signature(ratsnest.edges()) == ratsnest_signature(view_ratsnest));

        const auto cpl = volt::project_cpl(artifact);
        const auto view_cpl = volt::project_cpl(artifact.view());
        CHECK(cpl.source() == artifact.identity());
        CHECK(volt::io::write_cpl_json(cpl.cpl()) == volt::io::write_cpl_json(view_cpl));

        const auto svg = volt::io::render_pcb_svg(artifact);
        const auto view_svg = volt::io::write_pcb_placement_svg(artifact.view());
        CHECK(svg.source() == artifact.identity());
        CHECK_FALSE(svg.layer().has_value());
        CHECK(svg.bytes() == view_svg);

        const auto layer = volt::io::render_pcb_svg(
            artifact, volt::io::PcbPlacementSvgOptions{.layer_filter = volt::BoardLayerId{0}});
        CHECK(layer.source() == artifact.identity());
        REQUIRE(layer.layer().has_value());
        CHECK(*layer.layer() == volt::BoardLayerId{0});

        const auto scene = volt::prepare_board_scene(artifact);
        CHECK(scene.source() == artifact.identity());
        CHECK(scene.geometry().outline.has_value());
        REQUIRE(scene.placements().size() == 1U);
        REQUIRE(scene.models().size() == 1U);
        REQUIRE(scene.placements().front().model().has_value());
        CHECK(*scene.placements().front().model() == scene.models().front().reference());
        CHECK(std::string{volt::resolve_board_scene_model(
                  scene, artifact, scene.models().front().reference())} == source.model_bytes);

        const auto archive = std::string{artifact.bytes()};
        authored.board.move(volt::BoardPlacementMove{
            volt::ComponentPlacementId{0},
            volt::BoardPoint{42.0, 24.0},
            volt::BoardRotation::degrees(180.0),
            volt::BoardSide::Bottom,
        });
        CHECK(std::string{artifact.bytes()} == archive);
        CHECK(volt::io::render_pcb_svg(artifact).bytes() == svg.bytes());

        return Witness{
            archive,
            diagnostic_signature(validation.diagnostics()),
            ratsnest_signature(ratsnest.edges()),
            volt::io::write_cpl_json(cpl.cpl()),
            svg.bytes(),
            layer.bytes(),
            source.model_bytes,
        };
    }();

    auto reopened = volt::io::open_compiled_board(witness.archive);
    const auto reopened_scene = volt::prepare_board_scene(reopened);
    const auto reopened_reference = reopened_scene.models().front().reference();
    CHECK(diagnostic_signature(volt::validate_board(reopened).diagnostics()) ==
          witness.diagnostics);
    CHECK(ratsnest_signature(volt::compute_ratsnest(reopened).edges()) == witness.ratsnest);
    CHECK(volt::io::write_cpl_json(volt::project_cpl(reopened).cpl()) == witness.cpl_json);
    CHECK(volt::io::render_pcb_svg(reopened).bytes() == witness.svg);
    CHECK(volt::io::render_pcb_svg(
              reopened, volt::io::PcbPlacementSvgOptions{.layer_filter = volt::BoardLayerId{0}})
              .bytes() == witness.layer_svg);
    CHECK(std::string{volt::resolve_board_scene_model(reopened_scene, reopened,
                                                      reopened_reference)} == witness.model_bytes);

    auto moved = std::move(reopened);
    CHECK(std::string{volt::resolve_board_scene_model(reopened_scene, moved, reopened_reference)} ==
          witness.model_bytes);
    CHECK(volt::io::render_pcb_svg(moved).bytes() == witness.svg);
}

TEST_CASE("BoardScene accepts only the exact compiled revision GLB closure") {
    auto source = fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key, source.unused_key}, source.resolver);
    auto unplaced = design(source.spec, bundle, source.selected_key, "Unplaced", false);
    auto unplaced_compiled = take_success(volt::io::compile_board(*unplaced.circuit, unplaced.board,
                                                                  bundle, models3d_capabilities()));
    CHECK(has_diagnostic(volt::project_cpl(unplaced_compiled).cpl().diagnostics(),
                         "ASSEMBLY_COMPONENT_UNPLACED"));
    const auto unplaced_scene = volt::prepare_board_scene(unplaced_compiled);
    CHECK(unplaced_scene.placements().empty());
    CHECK(unplaced_scene.models().size() == 1U);

    auto main = design(source.spec, bundle, source.selected_key, "Main", true, true);
    auto baseline = take_success(
        volt::io::compile_board(*main.circuit, main.board, bundle, baseline_capabilities()));
    const auto baseline_scene = volt::prepare_board_scene(baseline);
    CHECK(baseline_scene.models().empty());

    auto models = take_success(
        volt::io::compile_board(*main.circuit, main.board, bundle, models3d_capabilities()));
    const auto scene = volt::prepare_board_scene(models);
    REQUIRE(scene.models().size() == 1U);
    REQUIRE(scene.placements().size() == 2U);
    CHECK(scene.placements()[0].placement() == volt::ComponentPlacementId{0});
    CHECK(scene.placements()[0].component() == main.component);
    CHECK(scene.placements()[0].reference() == "R1");
    CHECK(scene.placements()[0].transform() == std::array{1.0, 0.0, 0.0, 2.0, 0.0, 1.0, 0.0, 3.0,
                                                          0.0, 0.0, 1.0, 0.8, 0.0, 0.0, 0.0, 1.0});
    REQUIRE(scene.placements()[0].model().has_value());
    REQUIRE(scene.placements()[1].model().has_value());
    CHECK(*scene.placements()[0].model() == *scene.placements()[1].model());
    const auto reference = scene.models().front().reference();
    CHECK(std::string{volt::resolve_board_scene_model(scene, models, reference)} ==
          source.model_bytes);

    auto alternative = design(source.spec, bundle, source.selected_key, "Alternative");
    auto alternative_models = take_success(volt::io::compile_board(
        *alternative.circuit, alternative.board, bundle, models3d_capabilities()));
    const auto alternative_scene = volt::prepare_board_scene(alternative_models);
    REQUIRE(alternative_scene.models().size() == 1U);
    CHECK_THROWS_AS(volt::resolve_board_scene_model(scene, alternative_models, reference),
                    volt::KernelLogicError);
    CHECK_THROWS_AS(volt::resolve_board_scene_model(scene, models,
                                                    alternative_scene.models().front().reference()),
                    volt::KernelLogicError);

    main.board.move(volt::BoardPlacementMove{
        volt::ComponentPlacementId{0},
        volt::BoardPoint{9.0, 9.0},
        volt::BoardRotation::degrees(90.0),
        volt::BoardSide::Top,
    });
    auto changed = take_success(
        volt::io::compile_board(*main.circuit, main.board, bundle, models3d_capabilities()));
    CHECK_FALSE(changed.identity() == models.identity());
    const auto changed_scene = volt::prepare_board_scene(changed);
    CHECK_THROWS_AS(volt::resolve_board_scene_model(changed_scene, changed, reference),
                    volt::KernelLogicError);

    CHECK_THROWS_AS((volt::BoardSceneModelRef{models, volt::ComponentId{999}}),
                    volt::KernelLogicError);
    CHECK_THROWS_AS((volt::BoardSceneModelRef{baseline, baseline.parts().front().component()}),
                    volt::KernelLogicError);

    auto step_source = fixture(false, false, "step");
    const auto step_bundle = volt::io::PartLibraryBundle::build(
        step_source.builder, std::array{step_source.selected_key}, step_source.resolver);
    auto step_design = design(step_source.spec, step_bundle, step_source.selected_key, "STEP");
    auto step = take_success(volt::io::compile_board(*step_design.circuit, step_design.board,
                                                     step_bundle, models3d_capabilities()));
    REQUIRE(step.parts().front().model_3d_bytes().has_value());
    CHECK(volt::prepare_board_scene(step).models().empty());
}

namespace {

template <typename T>
concept TemporaryPartResolver = requires(T value) { std::move(value).part_definitions(); };
template <typename T>
concept TemporarySelectedComponent =
    requires(T value) { std::move(value).selected_component_definition(volt::ComponentId{0}); };
static_assert(!TemporaryPartResolver<volt::CompiledBoard>);
static_assert(!TemporarySelectedComponent<volt::CompiledBoard>);

void check_same_diagnostics(const volt::DiagnosticReport &actual,
                            const volt::DiagnosticReport &expected) {
    REQUIRE(actual.diagnostics().size() == expected.diagnostics().size());
    for (std::size_t index = 0; index < actual.diagnostics().size(); ++index) {
        CHECK(actual.diagnostics()[index].code() == expected.diagnostics()[index].code());
        CHECK(actual.diagnostics()[index].entities() == expected.diagnostics()[index].entities());
    }
}

} // namespace

TEST_CASE(
    "CompiledBoard retains full exact parts and native BOM diagnostics without source owners") {
    const auto bad_design = GENERATE(false, true);

    struct Witness {
        std::string archive;
        volt::LibraryPartRef reference;
        std::string part_bytes;
        volt::ContentHash component_identity;
        std::string bom;
        volt::DiagnosticReport diagnostics;
    };

    const auto witness = [&] {
        auto source = fixture(false, false, "glb", true, true, bad_design);
        const auto bundle = volt::io::PartLibraryBundle::build(
            source.builder, std::array{source.selected_key}, source.resolver);
        auto authored = design(source.spec, bundle, source.selected_key, "Main", false, true);
        if (!bad_design) {
            authored.circuit->update(authored.component, volt::SetAssemblyIntent{.dnp = false});
        }
        authored.circuit->update(*authored.second_component, volt::SetAssemblyIntent{.dnp = true});
        const auto definition = authored.circuit->get(authored.component).definition();
        const auto reference = bundle.require(source.selected_key);
        const auto bytes = volt::io::write_part_definition(bundle.resolve(reference));
        auto fresh = take_success(volt::io::compile_board(*authored.circuit, authored.board, bundle,
                                                          baseline_capabilities()));
        CHECK(volt::io::write_part_definition(fresh.part_definitions().resolve(reference)) ==
              bytes);
        const auto *component = fresh.selected_component_definition(authored.component);
        REQUIRE(component != nullptr);
        CHECK(component->content_identity() ==
              authored.circuit->get(definition).content_identity());
        CHECK(component->contract().relations().size() == 1U);
        CHECK(component->schematic_symbols().size() == 1U);
        CHECK(fresh.board().circuit().get(definition).schematic_symbols().empty());
        CHECK(fresh.part_definitions().resolve(reference).implemented_component() !=
              fresh.board().circuit().get(definition).content_identity());
        CHECK(fresh.selected_component_definition(*authored.second_component) == component);
        CHECK_FALSE(fresh.capabilities().has(volt::BoardAssetCapability::Models3D));
        CHECK_FALSE(fresh.part(authored.component)->model_3d_bytes().has_value());
        CHECK(fresh.part_definitions().resolve(reference).orderable_part().model_3d().has_value());
        const auto expected = volt::validate_bom_readiness(*authored.circuit, bundle);
        check_same_diagnostics(
            volt::validate_bom_readiness(fresh.board().circuit(), fresh.part_definitions()),
            expected);
        CHECK(expected.diagnostics().empty() == !bad_design);
        const auto bom = volt::io::write_bom_json(volt::project_bom(*authored.circuit, bundle));
        CHECK(volt::io::write_bom_json(
                  volt::project_bom(fresh.board().circuit(), fresh.part_definitions())) == bom);
        return Witness{std::string{fresh.bytes()},    reference, bytes,
                       component->content_identity(), bom,       expected};
    }();
    auto reopened = volt::io::open_compiled_board(witness.archive);
    const auto &resolver = reopened.part_definitions();
    const auto &part = resolver.resolve(witness.reference);
    CHECK(volt::io::write_part_definition(part) == witness.part_bytes);
    REQUIRE(part.electrical_records().records().size() == 2U);
    CHECK(part.schematic_assets().size() == 1U);
    CHECK(part.electrical_model().has_value());
    CHECK(part.electrical_records().records()[1].value().kind() ==
          volt::ElectricalValueKind::Unknown);
    CHECK(reopened.selected_component_definition(volt::ComponentId{0})->content_identity() ==
          witness.component_identity);
    check_same_diagnostics(volt::validate_bom_readiness(reopened.board().circuit(), resolver),
                           witness.diagnostics);
    const auto bom = volt::project_bom(reopened.board().circuit(), resolver);
    CHECK(volt::io::write_bom_json(bom) == witness.bom);
    REQUIRE(bom.lines().size() == 2U);
    CHECK(bom.lines()[0].quantity() == 1U);
    CHECK(bom.lines()[1].quantity() == 0U);
    CHECK(volt::io::write_compiled_board(reopened) == witness.archive);
    // Strict electrical input identity must still reject the symbol-stripped projection.
    CHECK_THROWS_AS(volt::io::prepare_electrical_input(reopened.board().circuit(), resolver),
                    volt::KernelLogicError);
    const auto *component_borrow = reopened.selected_component_definition(volt::ComponentId{0});
    auto moved = std::move(reopened);
    CHECK(moved.selected_component_definition(volt::ComponentId{0}) == component_borrow);
    CHECK(&moved.part_definitions() == &resolver);
    CHECK(&resolver.resolve(witness.reference) == &part);
    auto destination = volt::io::open_compiled_board(witness.archive);
    CHECK(&destination.part_definitions().resolve(witness.reference) != &part);
    destination = std::move(moved);
    CHECK(destination.selected_component_definition(volt::ComponentId{0}) == component_borrow);
    CHECK(&destination.part_definitions().resolve(witness.reference) == &part);
    CHECK(volt::io::write_part_definition(part) == witness.part_bytes);
}

TEST_CASE("CompiledBoard exact lookup rejects every foreign identity field and invalid component") {
    auto source = fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key, source.unused_key}, source.resolver);
    auto authored = design(source.spec, bundle, source.selected_key);
    const auto unselected = authored.circuit->instantiate_component(
        authored.circuit->get(authored.component).definition(),
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R3"}});
    const auto artifact = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                               bundle, baseline_capabilities()));
    CHECK(artifact.selected_component_definition(unselected) == nullptr);
    try {
        static_cast<void>(artifact.selected_component_definition(volt::ComponentId{999}));
        FAIL("invalid component accepted");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::UnknownEntity);
    }
    const auto ref = bundle.require(source.selected_key);
    const auto foreign_hash = volt::sha256_content_hash("foreign");
    const auto foreign =
        std::array{volt::LibraryPartRef{"foreign", ref.library_version(), ref.part_key(),
                                        ref.library_digest(), ref.part_digest()},
                   volt::LibraryPartRef{ref.library_namespace(), "foreign", ref.part_key(),
                                        ref.library_digest(), ref.part_digest()},
                   volt::LibraryPartRef{ref.library_namespace(), ref.library_version(),
                                        source.unused_key, ref.library_digest(), ref.part_digest()},
                   volt::LibraryPartRef{ref.library_namespace(), ref.library_version(),
                                        ref.part_key(), foreign_hash, ref.part_digest()},
                   volt::LibraryPartRef{ref.library_namespace(), ref.library_version(),
                                        ref.part_key(), ref.library_digest(), foreign_hash},
                   bundle.require(source.unused_key)};
    for (const auto &reference : foreign) {
        try {
            static_cast<void>(artifact.part_definitions().resolve(reference));
            FAIL("foreign reference accepted");
        } catch (const volt::KernelError &error) {
            CHECK(error.code() == volt::ErrorCode::UnknownEntity);
        }
    }
}

TEST_CASE("CompiledBoard retained component pin IDs stay payload-local across definitions") {
    const auto artifact = [] {
        auto source = volt::test::project_bundle_v2::mixed_footprint_board_fixture();
        return volt::io::open_compiled_board(source.compiled.bytes());
    }();
    const auto &circuit = artifact.board().circuit();
    const auto *retained = artifact.selected_component_definition(volt::ComponentId{1});
    REQUIRE(retained != nullptr);
    const auto &projected = circuit.get(circuit.get(volt::ComponentId{1}).definition());
    CHECK(retained->content_identity() == projected.content_identity());
    CHECK(retained->pins().front() == volt::PinDefId{0});
    CHECK(projected.pins().front() != retained->pins().front());
    CHECK(retained->contract().pin_keys() == projected.contract().pin_keys());
}

TEST_CASE(
    "CompiledBoard resolves same-key digest-distinct closures without a single library identity") {
    const auto make_archive = [](bool alternate) {
        auto source = fixture(false, false, "glb", true, true, alternate);
        const auto bundle = volt::io::PartLibraryBundle::build(
            source.builder, std::array{source.selected_key}, source.resolver);
        auto authored = design(source.spec, bundle, source.selected_key, "Main", false, true);
        const auto compiled = take_success(volt::io::compile_board(
            *authored.circuit, authored.board, bundle, baseline_capabilities()));
        return decode_archive(compiled.bytes());
    };
    auto archive = make_archive(false);
    const auto other = make_archive(true);
    const auto first_row = archive.manifest["selected_parts"][0];
    const auto second_row = other.manifest["selected_parts"][0];
    REQUIRE(first_row["reference"]["part_key"] == second_row["reference"]["part_key"]);
    REQUIRE(first_row["reference"]["library_digest"] != second_row["reference"]["library_digest"]);
    REQUIRE(first_row["reference"]["part_digest"] != second_row["reference"]["part_digest"]);
    // Reauthor a structurally valid two-revision archive using native canonical payloads.
    // Refreshed hashes prove integrity only, not historical authoring authenticity.
    archive.manifest["selected_parts"].push_back(second_row);
    std::sort(archive.manifest["selected_parts"].begin(), archive.manifest["selected_parts"].end(),
              [](const auto &lhs, const auto &rhs) {
                  return lhs["reference"].dump() < rhs["reference"].dump();
              });
    for (const auto &entry : other.entries) {
        if (!entry.path.starts_with("closure/")) {
            continue;
        }
        if (std::ranges::find(archive.entries, entry.path, &ArchiveEntry::path) ==
            archive.entries.end()) {
            archive.entries.push_back(entry);
            const auto descriptor =
                std::ranges::find_if(other.manifest["entries"],
                                     [&](const auto &row) { return row["path"] == entry.path; });
            REQUIRE(descriptor != other.manifest["entries"].end());
            archive.manifest["entries"].push_back(*descriptor);
        }
    }
    std::ranges::sort(archive.entries, {}, &ArchiveEntry::path);
    std::sort(archive.manifest["entries"].begin(), archive.manifest["entries"].end(),
              [](const auto &lhs, const auto &rhs) { return lhs["path"] < rhs["path"]; });
    const auto logical =
        std::ranges::find(archive.entries, "snapshots/logical.json", &ArchiveEntry::path);
    REQUIRE(logical != archive.entries.end());
    auto wrapper = Json::parse(logical->payload);
    wrapper["circuit"]["components"][1]["selected_library_part"] = second_row["reference"];
    logical->payload = wrapper.dump();
    archive.manifest["selected_closure_digest"] =
        volt::sha256_content_hash(Json{{"format", "volt.compiled-board.selected-closure"},
                                       {"schema_version", 1U},
                                       {"selected_parts", archive.manifest["selected_parts"]}}
                                      .dump())
            .value();
    refresh_logical_provenance(archive, *logical);
    const auto bytes = encode_archive(archive);
    const auto compiled = volt::io::open_compiled_board(bytes);
    const auto &circuit = compiled.board().circuit();
    const auto &first = *circuit.get(volt::ComponentId{0}).selected_library_part_ref();
    const auto &second = *circuit.get(volt::ComponentId{1}).selected_library_part_ref();
    const auto &parts = compiled.part_definitions();
    CHECK(&parts.resolve(first) != &parts.resolve(second));
    CHECK(parts.resolve(first).content_identity() == first.part_digest());
    CHECK(parts.resolve(second).content_identity() == second.part_digest());
    CHECK(compiled.selected_component_definition(volt::ComponentId{0}) ==
          compiled.selected_component_definition(volt::ComponentId{1}));
    CHECK(parts.resolve(first).orderable_part().approved_alternate_mpns().size() == 1U);
    CHECK(parts.resolve(second).orderable_part().approved_alternate_mpns().size() == 2U);
    CHECK(volt::io::write_compiled_board(compiled) == bytes);
}

TEST_CASE("CompiledBoard rejects rehashed malformed full-part and mismatched component payloads") {
    auto source = fixture(false, false, "glb", true, true);
    const auto bundle = volt::io::PartLibraryBundle::build(
        source.builder, std::array{source.selected_key}, source.resolver);
    auto authored = design(source.spec, bundle, source.selected_key);
    const auto compiled = take_success(volt::io::compile_board(*authored.circuit, authored.board,
                                                               bundle, baseline_capabilities()));
    const auto fault = GENERATE(0, 1, 2);
    auto archive = decode_archive(compiled.bytes());
    const auto payload = std::ranges::find_if(archive.entries, [](const auto &entry) {
        return entry.path.starts_with("closure/parts/");
    });
    REQUIRE(payload != archive.entries.end());
    auto document = Json::parse(payload->payload);
    if (fault == 0) {
        document.erase("electrical_records");
    }
    if (fault == 1) {
        document["implements"] = volt::sha256_content_hash("foreign contract").value();
    }
    if (fault == 2) {
        document["pin_terminal_mappings"][0]["pin_key"] = "foreign-pin";
    }
    payload->payload = document.dump();
    refresh_entry_digest(archive, *payload);
    // This reaches native payload validation despite the refreshed envelope hash.
    try {
        static_cast<void>(volt::io::open_compiled_board(encode_archive(archive)));
        FAIL("malformed retained payload accepted");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == (fault == 2 ? volt::ErrorCode::CrossReferenceViolation
                                          : volt::ErrorCode::InvalidArgument));
    }
}

TEST_CASE("ProjectBundle owns reopened exact definitions and BOM inputs without source owners") {
    const auto temporary = volt::test::project_bundle_v2::TempDirectory{};
    const auto path = temporary.path() / "exact-parts.volt";
    const auto witness = [&] {
        auto source = volt::test::project_bundle_v2::mixed_footprint_board_fixture();
        auto builder = volt::test::project_bundle_v2::project_builder();
        builder.add_logical(volt::io::DesignKey{"main"}, *source.circuit, source.bundle);
        builder.add_board(volt::io::DesignKey{"main"}, source.board, source.compiled, source.scene,
                          source.bundle);
        builder.build().write(path);
        return volt::io::write_bom_json(volt::project_bom(*source.circuit, source.bundle));
    }();
    const auto reopened = volt::io::ProjectBundle::open(path);
    const auto compiled = reopened.graph().loaded_project().compiled_boards();
    REQUIRE(compiled.size() == 1U);
    const auto &artifact = compiled.front().model();
    const auto &circuit = artifact.board().circuit();
    for (std::size_t index = 0; index < circuit.all<volt::ComponentId>().size(); ++index) {
        const auto component = volt::ComponentId{index};
        const auto &reference = *circuit.get(component).selected_library_part_ref();
        CHECK(artifact.part_definitions().resolve(reference).content_identity() ==
              reference.part_digest());
        CHECK(artifact.selected_component_definition(component) != nullptr);
    }
    CHECK(volt::io::write_bom_json(volt::project_bom(circuit, artifact.part_definitions())) ==
          witness);
}

TEST_CASE("CompiledBoard V1 completion preserves proven isolated contact failure") {
    auto options = volt::test::CompletionFixtureOptions{};
    options.components = 1;
    options.connected = false;
    options.explicit_nc = true;
    options.zone = false;
    options.nc_contact = true;
    const auto compiled = volt::test::completion_fixture(options);
    auto legacy = decode_archive(compiled.bytes());
    legacy.manifest["schema_version"] = 1U;
    legacy.manifest["compiler_version"] = 1U;
    auto logical = std::ranges::find(legacy.entries, "snapshots/logical.json", &ArchiveEntry::path);
    REQUIRE(logical != legacy.entries.end());
    auto wrapper = Json::parse(logical->payload);
    wrapper["schema_version"] = 1U;
    wrapper.erase("realized_net_partition");
    logical->payload = wrapper.dump();
    refresh_logical_provenance(legacy, *logical);
    const auto reopened = volt::io::open_compiled_board(encode_archive(legacy));
    const auto result = volt::io::evaluate_fabrication_completion(reopened);
    CHECK(result.status() == volt::BoardCompletionStatus::Incomplete);
    CHECK(result.coverage()[0].state == volt::BoardCompletionState::NotEvaluated);
    CHECK(result.coverage()[4].state == volt::BoardCompletionState::Failed);
    CHECK(result.coverage()[4].reason == "Inspect exact diagnostics for this mandatory check");
    CHECK(has_diagnostic(result.diagnostics(),
                         volt::pcb_completion_diagnostic_codes::IsolatedContact));
    CHECK_FALSE(volt::io::write_pcb_fabrication_files(reopened).files.empty());
}
