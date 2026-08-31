#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/electrical/passive_model.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/parts/part_definition_writer.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/project_bundle_writer.hpp>

namespace {

constexpr auto shared_evidence = std::string_view{"native shared model and V/I evidence"};
constexpr auto vi_evidence = std::string_view{"native V/I-only evidence"};
constexpr auto unrelated_evidence = std::string_view{"unrelated catalogue evidence"};

class AssetResolver final : public volt::PartAssetResolver {
  public:
    void add(const volt::PartAssetReference &reference, std::string bytes) {
        assets_.emplace(std::pair{reference.kind(), reference.key()}, std::move(bytes));
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto found = assets_.find(std::pair{reference.kind(), reference.key()});
        return found == assets_.end() ? std::nullopt : std::optional{found->second};
    }

  private:
    std::map<std::pair<volt::PartAssetKind, std::string>, std::string> assets_;
};

void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
    auto output = std::ofstream{};
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    const auto destination = std::filesystem::path{argv[1]};
    std::filesystem::create_directory(destination);
    auto circuit = volt::Circuit{};
    const auto spec = volt::ComponentSpec{.name = "Passive transport contract",
                                          .pins = {volt::PinSpec{.name = "A", .number = "1"},
                                                   volt::PinSpec{.name = "B", .number = "2"}},
                                          .contract = volt::ComponentContractSpec{
                                              .key = volt::ComponentKey{"test.transport/passive@1"},
                                              .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"B"}}}};
    const auto definition = circuit.define_component(spec);
    const auto component = circuit.get(definition);
    auto model = volt::PartElectricalModelBuilder{component};
    const auto a = model.terminal(volt::ModelTerminalKey{"a"}, volt::PinKey{"A"});
    const auto b = model.terminal(volt::ModelTerminalKey{"b"}, volt::PinKey{"B"});
    const auto x = model.internal_node(volt::ModelInternalNodeKey{"after_esr"});
    const auto y = model.internal_node(volt::ModelInternalNodeKey{"after_esl"});
    const auto shared_digest = volt::sha256_content_hash(shared_evidence);
    model.add<volt::ResistanceElement>(
        volt::ModelElementKey{"esr"}, a, x,
        volt::ModelParameter{
            volt::Quantity{volt::UnitDimension::Resistance, 0.08}, std::nullopt, {shared_digest}});
    model.add<volt::InductanceElement>(
        volt::ModelElementKey{"esl"}, x, y,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Inductance, 1.0e-9},
                             volt::Tolerance::percent(0.0),
                             {shared_digest}});
    model.add<volt::CapacitanceElement>(
        volt::ModelElementKey{"storage"}, y, b,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Capacitance, 10.0e-6},
                             volt::Tolerance::percent(0.1, 0.2),
                             {shared_digest}});

    const auto footprint_ref = volt::FootprintRef{"test.transport", "passive"};
    const auto footprint_bytes = volt::io::write_footprint_asset(volt::FootprintDefinition{
        footprint_ref,
        std::vector{volt::FootprintPad::surface_mount(
                        "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-0.5, 0.0},
                        volt::FootprintSize{0.5, 0.5}, volt::FootprintLayerSet::front_smd()),
                    volt::FootprintPad::surface_mount(
                        "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.5, 0.0},
                        volt::FootprintSize{0.5, 0.5}, volt::FootprintLayerSet::front_smd())}});
    const auto vi_reference = volt::PartAssetReference{
        volt::PartAssetKind::Evidence, "evidence:vi-only", volt::sha256_content_hash(vi_evidence)};
    const auto make_part = [&](std::string key, std::optional<volt::PartElectricalModel> value) {
        auto records = volt::ElectricalRecordSet{2};
        if (value.has_value()) {
            records = records.with_record(
                volt::voltage_record(volt::ElectricalSubject::directed_relation(
                                         volt::ElectricalPinIndex{0}, volt::ElectricalPinIndex{1}),
                                     volt::ElectricalMeaning::AbsoluteLimit,
                                     volt::ElectricalValue{volt::QuantityRange::bounded(
                                         volt::Quantity{volt::UnitDimension::Voltage, -5.0},
                                         volt::Quantity{volt::UnitDimension::Voltage, 5.0})},
                                     {}, {shared_digest, vi_reference.digest()}));
        }
        return volt::PartDefinition{
            component,
            volt::PartIdentity{"test.transport", key, "1"},
            std::move(records),
            {volt::PinPackageTerminalMapping{volt::PinKey{"A"}, {volt::PackageTerminalKey{"1"}}},
             volt::PinPackageTerminalMapping{volt::PinKey{"B"}, {volt::PackageTerminalKey{"2"}}}},
            {},
            volt::PartProvenance{"", "volt.tests", "native transport fixture"},
            {},
            volt::OrderablePart{volt::ManufacturerPart{"Test", key},
                                volt::PackageRef{"TEST-2"},
                                volt::HashedFootprintReference{
                                    footprint_ref, volt::sha256_content_hash(footprint_bytes)},
                                {volt::PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5},
                                 volt::PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5}},
                                {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                                                 {volt::FootprintPadKey{"1"}}},
                                 volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                                                 {volt::FootprintPadKey{"2"}}}}},
            std::move(value)};
    };
    const auto modeled = make_part("modeled", model.build());
    const auto absent = make_part("absent", std::nullopt);
    auto resolver = AssetResolver{};
    for (const auto &reference : volt::part_asset_references(modeled)) {
        resolver.add(reference, reference.kind() == volt::PartAssetKind::Footprint
                                    ? footprint_bytes
                                    : std::string{shared_evidence});
    }
    resolver.add(vi_reference, std::string{vi_evidence});
    const auto unrelated_reference =
        volt::PartAssetReference{volt::PartAssetKind::Evidence, "evidence:unrelated",
                                 volt::sha256_content_hash(unrelated_evidence)};
    resolver.add(unrelated_reference, std::string{unrelated_evidence});
    auto library_builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.transport", "1", volt::PartLibrarySchemaVersion::V1}};
    library_builder.add_component(spec).add_part(modeled).add_part(absent);
    const auto library = volt::io::PartLibraryBundle::build(
        library_builder, std::vector{volt::PartKey{"modeled"}, volt::PartKey{"absent"}}, resolver,
        std::vector{
            volt::io::PartLibraryBundleAttachment{volt::PartKey{"modeled"}, vi_reference},
            volt::io::PartLibraryBundleAttachment{volt::PartKey{"modeled"}, unrelated_reference}});
    const auto positive = circuit.add_net(volt::NetSpec{.name = volt::NetName{"positive"}});
    const auto negative = circuit.add_net(volt::NetSpec{.name = volt::NetName{"negative"}});
    for (const auto &[reference, key] : std::vector<std::pair<std::string, std::string>>{
             {"C1", "modeled"}, {"C2", "modeled"}, {"U1", "absent"}}) {
        const auto instance = circuit.instantiate_component(
            definition,
            volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{reference}});
        circuit.update(instance,
                       volt::SelectLibraryPart{library, library.require(volt::PartKey{key})});
        circuit.connect(positive, volt::queries::pin_by_number(circuit, instance, "1").value());
        circuit.connect(negative, volt::queries::pin_by_number(circuit, instance, "2").value());
    }
    auto project = volt::io::ProjectBundleBuilder{
        volt::io::ProjectIdentity{"native transport", std::nullopt, std::nullopt},
        volt::io::ProjectRunSummary{true, volt::io::ProjectStatus::Clean, "default", {"design"}},
        volt::io::LogicalInputName{"project.py"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"project.py"}, "native fixture"}},
        volt::io::ProjectReport{
            R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
    project.add_logical(volt::io::DesignKey{"main"}, circuit, library);
    project.build().write(destination / "project.volt");
    write_bytes(destination / "library.voltlib", library.bytes());
    write_bytes(destination / "modeled.part.json", volt::io::write_part_definition(modeled));
    write_bytes(destination / "absent.part.json", volt::io::write_part_definition(absent));
}
