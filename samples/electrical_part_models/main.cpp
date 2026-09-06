#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/electrical/passive_model.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/parts/part_definition_reader.hpp>
#include <volt/io/parts/part_definition_writer.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/io/project_bundle_writer.hpp>

namespace {

constexpr auto evidence = std::string_view{
    "Illustrative passive parameters for the Volt authoring example; not measured."};

// This example-local resolver supplies exact bytes to the existing native bundle builder.
class Assets final : public volt::PartAssetResolver {
  public:
    std::map<std::pair<volt::PartAssetKind, std::string>, std::string> bytes;

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto found = bytes.find({reference.kind(), reference.key()});
        return found == bytes.end() ? std::nullopt : std::optional{found->second};
    }
};

void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
    auto output = std::ofstream{};
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] std::string read_bytes(const std::filesystem::path &path) {
    auto input = std::ifstream{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error{"Cannot read " + path.string()};
    }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

template <typename Element>
volt::PartElectricalModel single_element(const volt::ComponentDefinition &component,
                                         volt::ModelParameter parameter) {
    auto builder = volt::PartElectricalModelBuilder{component};
    const auto a = builder.terminal(volt::ModelTerminalKey{"a"}, volt::PinKey{"A"});
    const auto b = builder.terminal(volt::ModelTerminalKey{"b"}, volt::PinKey{"B"});
    builder.add<Element>(volt::ModelElementKey{"body"}, a, b, std::move(parameter));
    return builder.build();
}

void author(const std::filesystem::path &destination) {
    std::filesystem::create_directories(destination / "expected");
    auto circuit = volt::Circuit{};
    const auto spec = volt::ComponentSpec{
        .name = "Illustrative passive contract",
        .pins = {volt::PinSpec{.name = "A", .number = "1"},
                 volt::PinSpec{.name = "B", .number = "2"}},
        .contract = volt::ComponentContractSpec{
            .key = volt::ComponentKey{"volt.samples.electrical_models/passive@1"},
            .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"B"}}}};
    const auto definition = circuit.define_component(spec);
    const auto component = circuit.get(definition);
    const auto digest = volt::sha256_content_hash(evidence);

    const auto resistor = single_element<volt::ResistanceElement>(
        component, volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, 330.0},
                                        volt::Tolerance::percent(0.01),
                                        {digest}});
    const auto capacitor = single_element<volt::CapacitanceElement>(
        component, volt::ModelParameter{volt::Quantity{volt::UnitDimension::Capacitance, 100e-9}});
    const auto inductor = single_element<volt::InductanceElement>(
        component, volt::ModelParameter{volt::Quantity{volt::UnitDimension::Inductance, 10e-6},
                                        volt::Tolerance::percent(0.0)});

    auto composite = volt::PartElectricalModelBuilder{component};
    const auto a = composite.terminal(volt::ModelTerminalKey{"a"}, volt::PinKey{"A"});
    const auto b = composite.terminal(volt::ModelTerminalKey{"b"}, volt::PinKey{"B"});
    const auto x = composite.internal_node(volt::ModelInternalNodeKey{"after_esr"});
    const auto y = composite.internal_node(volt::ModelInternalNodeKey{"after_esl"});
    composite.add<volt::ResistanceElement>(
        volt::ModelElementKey{"esr"}, a, x,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, 0.08}});
    composite.add<volt::InductanceElement>(
        volt::ModelElementKey{"esl"}, x, y,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Inductance, 1e-9}});
    composite.add<volt::CapacitanceElement>(
        volt::ModelElementKey{"storage"}, y, b,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Capacitance, 10e-6},
                             volt::Tolerance::percent(0.2),
                             {digest}});

    const auto footprint_ref = volt::FootprintRef{"volt.samples.electrical_models", "example-2"};
    const auto footprint_bytes = volt::io::write_footprint_asset(volt::FootprintDefinition{
        footprint_ref,
        std::vector{
            volt::FootprintPad::surface_mount("1", volt::FootprintPadShape::Rectangle, {-0.5, 0.0},
                                              {0.5, 0.5}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount("2", volt::FootprintPadShape::Rectangle, {0.5, 0.0},
                                              {0.5, 0.5}, volt::FootprintLayerSet::front_smd())}});
    auto library_builder = volt::PartLibraryBuilder{volt::PartLibraryIdentity{
        "volt.samples.electrical_models", "1", volt::PartLibrarySchemaVersion::V1}};
    library_builder.add_component(spec);
    auto assets = Assets{};
    auto keys = std::vector<volt::PartKey>{};
    const auto models =
        std::vector<std::pair<std::string, std::optional<volt::PartElectricalModel>>>{
            {"R330", resistor},
            {"C-ideal", capacitor},
            {"L-ideal", inductor},
            {"C-ESR-ESL", composite.build()},
            {"unmodeled", std::nullopt}};
    for (const auto &[name, model] : models) {
        const auto part = volt::PartDefinition{
            component,
            volt::PartIdentity{"volt.samples.electrical_models", name, "1"},
            volt::ElectricalRecordSet{2},
            {volt::PinPackageTerminalMapping{volt::PinKey{"A"}, {volt::PackageTerminalKey{"1"}}},
             volt::PinPackageTerminalMapping{volt::PinKey{"B"}, {volt::PackageTerminalKey{"2"}}}},
            {},
            volt::PartProvenance{"", "Volt examples", "Illustrative values only"},
            {},
            volt::OrderablePart{volt::ManufacturerPart{"Volt illustrative examples", name},
                                volt::PackageRef{"ILLUSTRATIVE-2"},
                                volt::HashedFootprintReference{
                                    footprint_ref, volt::sha256_content_hash(footprint_bytes)},
                                {volt::PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5},
                                 volt::PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5}},
                                {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                                                 {volt::FootprintPadKey{"1"}}},
                                 volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                                                 {volt::FootprintPadKey{"2"}}}}},
            model};
        library_builder.add_part(part);
        keys.emplace_back(name);
        for (const auto &reference : volt::part_asset_references(part)) {
            assets.bytes.insert_or_assign({reference.kind(), reference.key()},
                                          reference.kind() == volt::PartAssetKind::Footprint
                                              ? footprint_bytes
                                              : std::string{evidence});
        }
        write_bytes(destination / "expected" / (name + ".part.json"),
                    volt::io::write_part_definition(part));
    }
    const auto library = volt::io::PartLibraryBundle::build(library_builder, keys, assets);
    write_bytes(destination / "library.voltlib", library.bytes());
    const auto positive = circuit.add_net(volt::NetSpec{.name = volt::NetName{"positive"}});
    const auto negative = circuit.add_net(volt::NetSpec{.name = volt::NetName{"negative"}});
    for (const auto &[name, reference] :
         std::vector<std::pair<std::string, std::string>>{{"R330", "R1"},
                                                          {"C-ideal", "C1"},
                                                          {"L-ideal", "L1"},
                                                          {"C-ESR-ESL", "C2"},
                                                          {"unmodeled", "U1"}}) {
        const auto instance = circuit.instantiate_component(
            definition,
            volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{reference}});
        circuit.update(instance,
                       volt::SelectLibraryPart{library, library.require(volt::PartKey{name})});
        circuit.connect(positive, volt::queries::pin_by_number(circuit, instance, "1").value());
        circuit.connect(negative, volt::queries::pin_by_number(circuit, instance, "2").value());
    }
    require(volt::validate_circuit(circuit).empty(), "Example logical validation failed");
    auto project = volt::io::ProjectBundleBuilder{
        volt::io::ProjectIdentity{"electrical-part-models", "1", "Illustrative passive models"},
        volt::io::ProjectRunSummary{true, volt::io::ProjectStatus::Clean, "default", {"design"}},
        volt::io::LogicalInputName{"main.cpp"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"main.cpp"},
                                  read_bytes(std::filesystem::path{__FILE__})}},
        volt::io::ProjectReport{
            R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
    project.add_logical(volt::io::DesignKey{"main"}, circuit, library);
    project.build().write(destination / "project.volt");
}

void inspect(const std::filesystem::path &destination) {
    const auto bundle = volt::io::ProjectBundle::open(destination / "project.volt");
    const auto graph = bundle.graph();
    const auto loaded = graph.loaded_project();
    require(loaded.boards().empty() && loaded.schematics().empty(), "Expected logical-only bundle");
    const auto logical = loaded.circuits().at(0);
    const auto &circuit = logical.model();
    require(circuit.all<volt::ComponentId>().size() == 5U, "Expected five physical Parts");
    require(circuit.all<volt::NetId>().size() == 2U, "Expected two external nets");
    for (const auto &instance : circuit.all<volt::ComponentId>()) {
        const auto reference = instance.selected_library_part_ref().value();
        const auto artifact =
            graph.artifact(volt::io::ArtifactId{volt::io::ArtifactKind::PartDefinition, reference})
                .value();
        const auto part = volt::io::read_part_definition_text(artifact.bytes(),
                                                              circuit.get(instance.definition()));
        require(part.content_identity() == reference.part_digest(),
                "Selected Part identity changed");
        require(artifact.bytes() ==
                    read_bytes(destination / "expected" / (part.identity().name() + ".part.json")),
                "Part keys, values, tolerance or evidence changed");
        if (part.identity().name() == "unmodeled") {
            require(!part.electrical_model(), "Absent model became modeled");
            continue;
        }
        const auto &model = part.electrical_model().value();
        require(model.terminals().size() == 2U, "Terminal coverage changed");
        if (part.identity().name() == "C-ESR-ESL") {
            require(model.internal_nodes().size() == 2U && model.elements().size() == 3U,
                    "Composite model changed");
        }
        for (const auto &element : model.elements()) {
            std::visit(
                [&](const auto &value) {
                    std::cout << part.identity().name() << '.' << value.key().value() << " = "
                              << value.parameter().nominal().value() << " SI\n";
                    for (const auto &hash : value.parameter().evidence()) {
                        const auto asset = graph.artifact(volt::io::ArtifactId{
                            volt::io::ArtifactKind::EvidenceAsset,
                            volt::io::LibraryAssetRef{reference.library_namespace(),
                                                      reference.library_version(),
                                                      volt::io::LibraryAssetKind::Evidence,
                                                      reference.library_digest(), hash}});
                        require(asset.has_value() && asset->bytes() == evidence,
                                "Model evidence was not vendored");
                    }
                },
                element);
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: electrical_part_models write|inspect OUTPUT_DIRECTORY\n";
        return 2;
    }
    const auto destination = std::filesystem::path{argv[2]};
    if (std::string_view{argv[1]} == "write") {
        author(destination);
    } else if (std::string_view{argv[1]} == "inspect") {
        inspect(destination);
    } else {
        return 2;
    }
}
