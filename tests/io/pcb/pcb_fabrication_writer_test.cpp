#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/io/pcb/pcb_fabrication_writer.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace {

struct FabricationCircuit {
    volt::Circuit circuit;
    volt::ComponentId resistor;
    volt::ComponentId header;
    volt::NetId signal;
    volt::NetId ground;
};

[[nodiscard]] FabricationCircuit make_fabrication_circuit() {
    auto circuit = volt::Circuit{};
    const auto passive_a = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto passive_b = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto header_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});

    const auto resistor_definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", {passive_a, passive_b}});
    const auto header_definition =
        circuit.add_component_definition(volt::ComponentDefinition{"Header", {header_pin}});
    const auto resistor =
        circuit.instantiate_component(resistor_definition, volt::ReferenceDesignator{"R1"});
    const auto header =
        circuit.instantiate_component(header_definition, volt::ReferenceDesignator{"J1"});

    const auto signal = circuit.add_net(volt::Net{volt::NetName{"SIGNAL"}, volt::NetKind::Signal});
    const auto ground = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});
    circuit.connect(signal, volt::queries::pin_by_definition(circuit, resistor, passive_a).value());
    circuit.connect(signal, volt::queries::pin_by_definition(circuit, header, header_pin).value());
    circuit.connect(ground, volt::queries::pin_by_definition(circuit, resistor, passive_b).value());

    circuit.select_physical_part(resistor, volt::PhysicalPart{
                                               volt::ManufacturerPart{"Volt", "RECT-0603"},
                                               volt::PackageRef{"0603"},
                                               volt::FootprintRef{"test", "RectSmd"},
                                               std::vector{volt::PinPadMapping{passive_a, "1"},
                                                           volt::PinPadMapping{passive_b, "2"}},
                                           });
    circuit.select_physical_part(header, volt::PhysicalPart{
                                             volt::ManufacturerPart{"Volt", "TH-1"},
                                             volt::PackageRef{"TH"},
                                             volt::FootprintRef{"test", "OnePinThroughHole"},
                                             std::vector{volt::PinPadMapping{header_pin, "1"}},
                                         });

    return FabricationCircuit{std::move(circuit), resistor, header, signal, ground};
}

[[nodiscard]] volt::FootprintDefinition rect_smd_footprint() {
    return volt::FootprintDefinition{
        volt::FootprintRef{"test", "RectSmd"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-0.5, 0.0},
                volt::FootprintSize{1.0, 0.8}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.5, 0.0},
                volt::FootprintSize{1.0, 0.8}, volt::FootprintLayerSet::front_smd()),
        },
        std::nullopt,
        volt::FootprintPolygon{std::vector{
            volt::FootprintPoint{-1.2, -0.5},
            volt::FootprintPoint{1.2, -0.5},
            volt::FootprintPoint{1.2, 0.5},
            volt::FootprintPoint{-1.2, 0.5},
        }},
    };
}

[[nodiscard]] volt::FootprintDefinition through_hole_footprint() {
    return volt::FootprintDefinition{
        volt::FootprintRef{"test", "OnePinThroughHole"},
        std::vector{volt::FootprintPad::through_hole(
            "1", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{1.6, 1.6}, volt::FootprintLayerSet::through_hole(),
            volt::FootprintDrill{0.8, volt::FootprintPadPlating::Plated})},
    };
}

[[nodiscard]] volt::FootprintLibrary fabrication_footprints() {
    auto footprints = volt::FootprintLibrary{};
    footprints.add(rect_smd_footprint());
    footprints.add(through_hole_footprint());
    return footprints;
}

[[nodiscard]] volt::Board make_fabrication_board(const FabricationCircuit &fixture) {
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    const auto silk = board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    static_cast<void>(board.add_feature(
        volt::BoardFeature::hole("MH1", volt::BoardPoint{4.0, 4.0}, 2.4, false, "mounting")));
    static_cast<void>(board.cache_footprint_definition(rect_smd_footprint()));
    static_cast<void>(board.cache_footprint_definition(through_hole_footprint()));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.resistor, volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)}));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.header, volt::BoardPoint{20.0, 10.0}, volt::BoardRotation::degrees(0.0)}));
    static_cast<void>(board.add_track(volt::BoardTrack{
        fixture.signal,
        front,
        std::vector{volt::BoardPoint{10.5, 10.0}, volt::BoardPoint{15.0, 10.0},
                    volt::BoardPoint{20.0, 10.0}},
        0.25,
    }));
    static_cast<void>(board.add_via(
        volt::BoardVia{fixture.ground, volt::BoardPoint{15.0, 15.0}, front, back, 0.35, 0.75}));
    static_cast<void>(board.add_zone(
        volt::BoardZone{std::vector{volt::BoardPoint{5.0, 12.0}, volt::BoardPoint{12.0, 12.0},
                                    volt::BoardPoint{12.0, 17.0}, volt::BoardPoint{5.0, 17.0}},
                        std::vector{back}, fixture.ground}));
    static_cast<void>(board.add_text(volt::BoardText{
        "REV A", volt::BoardPoint{5.0, 18.0}, volt::BoardRotation::degrees(0.0), silk, 1.0, true}));
    return board;
}

[[nodiscard]] const volt::io::PcbFabricationFile *
find_file(const volt::io::PcbFabricationExportResult &result, std::string_view filename) {
    const auto match = std::find_if(
        result.files.begin(), result.files.end(),
        [filename](const volt::io::PcbFabricationFile &file) { return file.filename == filename; });
    if (match == result.files.end()) {
        return nullptr;
    }
    return &*match;
}

[[nodiscard]] std::vector<std::string>
file_names(const volt::io::PcbFabricationExportResult &result) {
    auto names = std::vector<std::string>{};
    names.reserve(result.files.size());
    for (const auto &file : result.files) {
        names.push_back(file.filename);
    }
    return names;
}

[[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

} // namespace

TEST_CASE("PCB fabrication writer exports deterministic Gerber and Excellon files") {
    const auto fixture = make_fabrication_circuit();
    const auto board = make_fabrication_board(fixture);
    const auto footprints = fabrication_footprints();

    const auto result = volt::io::write_pcb_fabrication_files(board, footprints);
    const auto repeated = volt::io::write_pcb_fabrication_files(board, footprints);

    CHECK_FALSE(result.loss_report.has_warnings());
    CHECK(file_names(result) == std::vector<std::string>{
                                    "Control.GTL",
                                    "Control.GBL",
                                    "Control.GTS",
                                    "Control.GBS",
                                    "Control.GTO",
                                    "Control.GTP",
                                    "Control.GKO",
                                    "Control-PTH.TXT",
                                    "Control-NPTH.TXT",
                                });
    REQUIRE(result.files.size() == repeated.files.size());
    for (std::size_t index = 0; index < result.files.size(); ++index) {
        CHECK(result.files[index].filename == repeated.files[index].filename);
        CHECK(result.files[index].text == repeated.files[index].text);
    }

    const auto *top_copper = find_file(result, "Control.GTL");
    REQUIRE(top_copper != nullptr);
    CHECK(contains(top_copper->text, "%TF.FileFunction,Copper,L1,Top*%"));
    CHECK(contains(top_copper->text, "%ADD10C,0.250000*%"));
    CHECK(contains(top_copper->text, "X0010500000Y0010000000D02*"));
    CHECK(contains(top_copper->text, "X0020000000Y0010000000D01*"));
    CHECK(contains(top_copper->text, "G36*"));

    const auto *bottom_copper = find_file(result, "Control.GBL");
    REQUIRE(bottom_copper != nullptr);
    CHECK(contains(bottom_copper->text, "%TF.FileFunction,Copper,L2,Bot*%"));
    CHECK(contains(bottom_copper->text, "X0015000000Y0015000000D03*"));
    CHECK(contains(bottom_copper->text, "X0005000000Y0012000000D02*"));

    const auto *top_mask = find_file(result, "Control.GTS");
    REQUIRE(top_mask != nullptr);
    CHECK(contains(top_mask->text, "%TF.FileFunction,Soldermask,Top*%"));
    CHECK(contains(top_mask->text, "X0009000000Y0009600000D02*"));
    CHECK(contains(top_mask->text, "X0020000000Y0010000000D03*"));

    const auto *bottom_mask = find_file(result, "Control.GBS");
    REQUIRE(bottom_mask != nullptr);
    CHECK(contains(bottom_mask->text, "%TF.FileFunction,Soldermask,Bot*%"));
    CHECK(contains(bottom_mask->text, "X0020000000Y0010000000D03*"));

    const auto *silk = find_file(result, "Control.GTO");
    REQUIRE(silk != nullptr);
    CHECK(contains(silk->text, "%TF.FileFunction,Legend,Top*%"));
    CHECK(contains(silk->text, "G04 TEXT REV A*"));
    CHECK(contains(silk->text, "X0008800000Y0009500000D02*"));

    const auto *paste = find_file(result, "Control.GTP");
    REQUIRE(paste != nullptr);
    CHECK(contains(paste->text, "%TF.FileFunction,Paste,Top*%"));
    CHECK(contains(paste->text, "X0009000000Y0009600000D02*"));
    CHECK_FALSE(contains(paste->text, "X0020000000Y0010000000D03*"));

    const auto *outline = find_file(result, "Control.GKO");
    REQUIRE(outline != nullptr);
    CHECK(contains(outline->text, "%TF.FileFunction,Profile,NP*%"));
    CHECK(contains(outline->text, "X0000000000Y0000000000D02*"));
    CHECK(contains(outline->text, "X0030000000Y0000000000D01*"));

    const auto *pth = find_file(result, "Control-PTH.TXT");
    REQUIRE(pth != nullptr);
    CHECK(contains(pth->text, ";TYPE=PLATED"));
    CHECK(contains(pth->text, "T01C0.350000"));
    CHECK(contains(pth->text, "T02C0.800000"));
    CHECK(contains(pth->text, "X0015000000Y0015000000"));
    CHECK(contains(pth->text, "X0020000000Y0010000000"));

    const auto *npth = find_file(result, "Control-NPTH.TXT");
    REQUIRE(npth != nullptr);
    CHECK(contains(npth->text, ";TYPE=NON_PLATED"));
    CHECK(contains(npth->text, "T01C2.400000"));
    CHECK(contains(npth->text, "X0004000000Y0004000000"));
}

TEST_CASE("PCB fabrication writer reports unsupported geometry and native diagnostics") {
    const auto fixture = make_fabrication_circuit();
    auto board = make_fabrication_board(fixture);
    static_cast<void>(board.add_feature(volt::BoardFeature::slot(
        "SLOT", volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{5.0, 1.0}, 1.0, false)));
    static_cast<void>(board.add_feature(volt::BoardFeature::cutout(
        "CUT", std::vector{volt::BoardPoint{22.0, 1.0}, volt::BoardPoint{25.0, 1.0},
                           volt::BoardPoint{25.0, 4.0}, volt::BoardPoint{22.0, 4.0}})));
    static_cast<void>(
        board.add_feature(volt::BoardFeature::circle("FID", volt::BoardPoint{26.0, 5.0}, 1.0)));

    const auto result = volt::io::write_pcb_fabrication_files(board, fabrication_footprints());

    REQUIRE(result.loss_report.warnings().size() == 3);
    CHECK(result.loss_report.warnings().at(0).construct == "board.feature.slot");
    CHECK(result.loss_report.warnings().at(1).construct == "board.feature.cutout");
    CHECK(result.loss_report.warnings().at(2).construct == "board.feature.circle");
    for (const auto &warning : result.loss_report.warnings()) {
        CHECK(warning.fabrication_impact == volt::io::PcbFabricationLossImpact::FabCritical);
    }

    const auto diagnostics = volt::io::fabrication_diagnostics(result.loss_report);
    REQUIRE(diagnostics.count() == 3);
    CHECK(diagnostics.diagnostics().front().category() ==
          volt::DiagnosticCategory{volt::diagnostic_categories::PcbFabrication});
    CHECK(diagnostics.diagnostics().front().code() ==
          volt::DiagnosticCode{
              std::string{volt::pcb_fabrication_diagnostic_codes::NativeFabExportLoss}});
    REQUIRE(diagnostics.diagnostics().front().rule().has_value());
    CHECK(diagnostics.diagnostics().front().rule().value() == "board.feature.slot");
}

TEST_CASE("PCB fabrication writer reports unsupported copper layer data") {
    const auto fixture = make_fabrication_circuit();
    auto board = make_fabrication_board(fixture);
    const auto duplicate_top = board.add_layer(
        volt::BoardLayer{"TopAux", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto inner = board.add_layer(
        volt::BoardLayer{"In1.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Inner});
    static_cast<void>(board.add_track(volt::BoardTrack{
        fixture.ground,
        duplicate_top,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{2.0, 1.0}},
        0.25,
    }));
    static_cast<void>(board.add_track(volt::BoardTrack{
        fixture.ground,
        inner,
        std::vector{volt::BoardPoint{1.0, 2.0}, volt::BoardPoint{2.0, 2.0}},
        0.25,
    }));

    const auto result = volt::io::write_pcb_fabrication_files(board, fabrication_footprints());

    REQUIRE(result.loss_report.warnings().size() >= 2);
    CHECK(result.loss_report.warnings().at(0).construct == "board.layer.copper_side");
    CHECK(result.loss_report.warnings().at(1).construct == "board.layer.mapping");
    CHECK(result.loss_report.has_fab_critical_warnings());
}
