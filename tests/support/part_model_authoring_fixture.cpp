#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/electrical/passive_model.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/parts/part_definition_writer.hpp>
#include <volt/io/parts/part_library_bundle.hpp>

namespace {

using namespace volt;

constexpr auto first_evidence = std::string_view{"Illustrative passive parameter evidence A"};
constexpr auto second_evidence = std::string_view{"Illustrative passive parameter evidence B"};

class Assets final : public PartAssetResolver {
  public:
    std::map<PartAssetKind, std::map<std::string, std::string>> values;

    [[nodiscard]] std::optional<std::string>
    resolve(const PartAssetReference &reference) const override {
        const auto kind = values.find(reference.kind());
        if (kind == values.end()) {
            return std::nullopt;
        }
        const auto found = kind->second.find(reference.digest().value());
        return found == kind->second.end() ? std::nullopt : std::optional{found->second};
    }
};

void write(const std::filesystem::path &path, std::string_view bytes) {
    auto stream = std::ofstream{};
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.open(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

ComponentSpec component_spec() {
    return ComponentSpec{
        .name = "Passive authoring proof",
        .pins = {PinSpec{.name = "A", .number = "1"}, PinSpec{.name = "B", .number = "2"}},
        .source = DefinitionSource{"test.authoring", "parity", "1"},
        .contract = ComponentContractSpec{.key = ComponentKey{"test.authoring/passive@1"},
                                          .pin_keys = {PinKey{"A"}, PinKey{"B"}}}};
}

std::optional<PartElectricalModel> model(const ComponentDefinition &component,
                                         const std::string &variant) {
    if (variant == "absent") {
        return std::nullopt;
    }
    auto builder = PartElectricalModelBuilder{component};
    const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
    const auto evidence =
        std::vector{sha256_content_hash(first_evidence), sha256_content_hash(second_evidence)};
    if (variant.starts_with("composite")) {
        const auto x = builder.internal_node(ModelInternalNodeKey{"after_esr"});
        const auto y = builder.internal_node(ModelInternalNodeKey{"after_esl"});
        builder.add<ResistanceElement>(
            ModelElementKey{"esr"}, a, x,
            ModelParameter{Quantity{UnitDimension::Resistance, 0.08}, std::nullopt, evidence});
        builder.add<InductanceElement>(
            ModelElementKey{"esl"}, x, y,
            ModelParameter{Quantity{UnitDimension::Inductance, 1e-9}, Tolerance::percent(0.0)});
        builder.add<CapacitanceElement>(ModelElementKey{"storage"}, y, b,
                                        ModelParameter{Quantity{UnitDimension::Capacitance, 10e-6},
                                                       Tolerance::percent(0.1, 0.2), evidence});
    } else if (variant == "capacitor") {
        builder.add<CapacitanceElement>(ModelElementKey{"body"}, a, b,
                                        ModelParameter{Quantity{UnitDimension::Capacitance, 2e-6},
                                                       Tolerance::percent(0.0, 0.2), evidence});
    } else if (variant == "inductor") {
        builder.add<InductanceElement>(
            ModelElementKey{"body"}, a, b,
            ModelParameter{Quantity{UnitDimension::Inductance, 3e-3}, std::nullopt, evidence});
    } else {
        const bool zero = variant.starts_with("zero");
        const auto tolerance =
            variant == "zero_unspecified" || variant == "unspecified"
                ? std::nullopt
                : std::optional{Tolerance::percent(zero ? 0.0 : 0.01, zero ? 0.0 : 0.02)};
        builder.add<ResistanceElement>(
            ModelElementKey{variant == "renamed" ? "renamed" : "body"},
            variant == "reversed" ? b : a, variant == "reversed" ? a : b,
            ModelParameter{Quantity{UnitDimension::Resistance, zero                   ? -0.0
                                                               : variant == "changed" ? 201.0
                                                                                      : 200.0},
                           tolerance,
                           variant == "evidence_changed"
                               ? std::vector{sha256_content_hash(first_evidence)}
                               : evidence});
    }
    return builder.build();
}

PartDefinition part(const ComponentDefinition &component, const std::string &footprint_bytes,
                    std::optional<PartElectricalModel> value) {
    return PartDefinition{
        component,
        PartIdentity{"test.authoring", "parity", "1"},
        ElectricalRecordSet{2},
        {PinPackageTerminalMapping{PinKey{"A"}, {PackageTerminalKey{"1"}}},
         PinPackageTerminalMapping{PinKey{"B"}, {PackageTerminalKey{"2"}}}},
        {},
        PartProvenance{"", "volt.tests", "illustrative native parity fixture"},
        {},
        OrderablePart{
            ManufacturerPart{"Test", "PARITY-2"},
            PackageRef{"TEST-2"},
            HashedFootprintReference{FootprintRef{"test.authoring", "passive"},
                                     sha256_content_hash(footprint_bytes)},
            {PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5}, PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5}},
            {PackageTerminalPadMapping{PackageTerminalKey{"1"}, {FootprintPadKey{"1"}}},
             PackageTerminalPadMapping{PackageTerminalKey{"2"}, {FootprintPadKey{"2"}}}}},
        std::move(value)};
}

void rejected(std::string &output, std::string_view name, const std::function<void()> &operation) {
    try {
        operation();
    } catch (const KernelError &error) {
        output += std::string{name} + "\t" + std::string{error_code_name(error.code())} + "\t" +
                  error.what() + "\n";
        return;
    } catch (const std::invalid_argument &error) {
        output += std::string{name} + "\tValueError\t" + error.what() + "\n";
        return;
    }
    throw std::runtime_error{"Native failure fixture unexpectedly accepted: " + std::string{name}};
}

std::string errors(const ComponentDefinition &component) {
    auto output = std::string{};
    const auto parameter = ModelParameter{Quantity{UnitDimension::Resistance, 1.0}};
    const auto resistance = [](Quantity quantity,
                               std::optional<Tolerance> tolerance = std::nullopt) {
        return ResistanceElement{ModelElementKey{"body"}, ModelTerminalKey{"a"},
                                 ModelTerminalKey{"b"}, ModelParameter{quantity, tolerance}};
    };
    rejected(output, "wrong_dimension",
             [&] { static_cast<void>(resistance(Quantity{UnitDimension::Voltage, 1.0})); });
    rejected(output, "negative_r",
             [&] { static_cast<void>(resistance(Quantity{UnitDimension::Resistance, -1.0})); });
    rejected(output, "zero_c", [&] {
        static_cast<void>(CapacitanceElement{
            ModelElementKey{"body"}, ModelTerminalKey{"a"}, ModelTerminalKey{"b"},
            ModelParameter{Quantity{UnitDimension::Capacitance, 0.0}}});
    });
    rejected(output, "zero_l", [&] {
        static_cast<void>(
            InductanceElement{ModelElementKey{"body"}, ModelTerminalKey{"a"}, ModelTerminalKey{"b"},
                              ModelParameter{Quantity{UnitDimension::Inductance, 0.0}}});
    });
    rejected(output, "nan", [&] {
        static_cast<void>(
            Quantity{UnitDimension::Resistance, std::numeric_limits<double>::quiet_NaN()});
    });
    rejected(output, "infinity", [&] {
        static_cast<void>(Tolerance::percent(std::numeric_limits<double>::infinity()));
    });
    rejected(output, "negative_tolerance", [&] { static_cast<void>(Tolerance::percent(-0.01)); });
    rejected(output, "tolerance_dimension", [&] {
        static_cast<void>(
            ModelParameter{Quantity{UnitDimension::Resistance, 1.0},
                           Tolerance::absolute(Quantity{UnitDimension::Voltage, 0.0},
                                               Quantity{UnitDimension::Voltage, 0.0})});
    });
    rejected(output, "overflow", [&] {
        static_cast<void>(
            ModelParameter{Quantity{UnitDimension::Resistance, std::numeric_limits<double>::max()},
                           Tolerance::percent(0.0, 1.0)});
    });
    rejected(output, "bounds", [&] {
        static_cast<void>(
            resistance(Quantity{UnitDimension::Resistance, 1.0}, Tolerance::percent(1.01, 0.0)));
    });
    auto builder = PartElectricalModelBuilder{component};
    auto foreign = PartElectricalModelBuilder{component};
    const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
    const auto foreign_a = foreign.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    rejected(output, "foreign_handle", [&] {
        builder.add<ResistanceElement>(ModelElementKey{"body"}, foreign_a, b, parameter);
    });
    rejected(output, "duplicate_terminal",
             [&] { static_cast<void>(builder.terminal(ModelTerminalKey{"a"}, PinKey{"B"})); });
    rejected(output, "duplicate_pin",
             [&] { static_cast<void>(builder.terminal(ModelTerminalKey{"other"}, PinKey{"A"})); });
    rejected(output, "foreign_pin",
             [&] { static_cast<void>(builder.terminal(ModelTerminalKey{"other"}, PinKey{"C"})); });
    rejected(output, "same_endpoint",
             [&] { builder.add<ResistanceElement>(ModelElementKey{"body"}, a, a, parameter); });
    builder.add<ResistanceElement>(ModelElementKey{"body"}, a, b, parameter);
    rejected(output, "duplicate_element",
             [&] { builder.add<ResistanceElement>(ModelElementKey{"body"}, a, b, parameter); });
    rejected(output, "missing_terminal", [&] {
        auto incomplete = PartElectricalModelBuilder{component};
        const auto one = incomplete.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
        const auto node = incomplete.internal_node(ModelInternalNodeKey{"x"});
        incomplete.add<ResistanceElement>(ModelElementKey{"body"}, one, node, parameter);
        static_cast<void>(incomplete.build());
    });
    rejected(output, "unused_node", [&] {
        static_cast<void>(builder.internal_node(ModelInternalNodeKey{"unused"}));
        static_cast<void>(builder.build());
    });
    return output;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    const auto destination = std::filesystem::path{argv[1]};
    std::filesystem::create_directory(destination);
    auto circuit = Circuit{};
    const auto spec = component_spec();
    const auto component = circuit.get(circuit.define_component(spec));
    const auto footprint_bytes = io::write_footprint_asset(FootprintDefinition{
        FootprintRef{"test.authoring", "passive"},
        {FootprintPad::surface_mount("1", FootprintPadShape::Rectangle, {-0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd()),
         FootprintPad::surface_mount("2", FootprintPadShape::Rectangle, {0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd())}});
    auto assets = Assets{};
    assets.values[PartAssetKind::Footprint].emplace(sha256_content_hash(footprint_bytes).value(),
                                                    footprint_bytes);
    for (const auto bytes : {first_evidence, second_evidence}) {
        assets.values[PartAssetKind::Evidence].emplace(sha256_content_hash(bytes).value(), bytes);
    }
    for (const auto *variant :
         {"absent", "resistor", "zero", "zero_unspecified", "capacitor", "inductor", "composite",
          "reversed", "renamed", "changed", "unspecified", "evidence_changed"}) {
        const auto exact = part(component, footprint_bytes, model(component, variant));
        write(destination / (std::string{variant} + ".part.json"),
              io::write_part_definition(exact));
        write(destination / (std::string{variant} + ".digest"), exact.content_identity().value());
        auto builder = PartLibraryBuilder{
            PartLibraryIdentity{"test.authoring", "1", PartLibrarySchemaVersion::V1}};
        builder.add_component(spec).add_part(exact);
        const auto selected = std::vector{PartKey{"parity"}};
        const auto components = std::vector{component.contract().key()};
        const auto bundle = io::PartLibraryBundle::build_with_component_roots(builder, selected,
                                                                              components, assets);
        write(destination / (std::string{variant} + ".voltlib"), bundle.bytes());
    }
    write(destination / "component.digest", component.content_identity().value());
    write(destination / "errors.tsv", errors(component));
}
