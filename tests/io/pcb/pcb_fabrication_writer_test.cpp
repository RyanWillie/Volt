#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <locale>
#include <string>
#include <string_view>
#include <utility>
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

class CommaDecimalNumpunct : public std::numpunct<char> {
  protected:
    [[nodiscard]] char do_decimal_point() const override { return ','; }
};

class ScopedLocale {
  public:
    explicit ScopedLocale(std::locale locale) : previous_{std::locale::global(std::move(locale))} {}

    ~ScopedLocale() { std::locale::global(previous_); }

    ScopedLocale(const ScopedLocale &) = delete;
    ScopedLocale &operator=(const ScopedLocale &) = delete;

  private:
    std::locale previous_;
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

[[nodiscard]] std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    REQUIRE(input.is_open());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] const volt::Diagnostic *find_diagnostic(const volt::DiagnosticReport &report,
                                                      std::string_view code,
                                                      std::string_view rule) {
    const auto match = std::find_if(report.diagnostics().begin(), report.diagnostics().end(),
                                    [code, rule](const volt::Diagnostic &diagnostic) {
                                        return diagnostic.code().value() == code &&
                                               diagnostic.rule().has_value() &&
                                               diagnostic.rule().value() == rule;
                                    });
    if (match == report.diagnostics().end()) {
        return nullptr;
    }
    return &*match;
}

[[nodiscard]] bool has_diagnostic(const volt::DiagnosticReport &report, std::string_view code,
                                  std::string_view rule,
                                  const std::vector<volt::EntityRef> &entities) {
    return std::any_of(report.diagnostics().begin(), report.diagnostics().end(),
                       [code, rule, &entities](const volt::Diagnostic &diagnostic) {
                           return diagnostic.code().value() == code &&
                                  diagnostic.rule().has_value() &&
                                  diagnostic.rule().value() == rule &&
                                  diagnostic.entities() == entities;
                       });
}

} // namespace

TEST_CASE("PCB fabrication writer exports deterministic Gerber and Excellon files") {
    const auto fixture = make_fabrication_circuit();
    const auto board = make_fabrication_board(fixture);
    const auto footprints = fabrication_footprints();

    const auto result = volt::io::write_pcb_fabrication_files(board, footprints);
    const auto repeated = volt::io::write_pcb_fabrication_files(board, footprints);

    CHECK_FALSE(result.loss_report.has_warnings());
    CHECK(result.exporter.name == "volt.native_fabrication");
    CHECK(result.exporter.schema_version == 1);
    CHECK(result.exporter.gerber.format == "RS-274X");
    CHECK(result.exporter.gerber.units == "mm");
    CHECK(result.exporter.gerber.coordinate_format == "4.6");
    CHECK(result.exporter.gerber.zero_suppression == "none");
    CHECK(result.exporter.drill.format == "Excellon");
    CHECK(result.exporter.drill.units == "mm");
    CHECK(result.exporter.drill.coordinate_format == "4.6");
    CHECK(result.exporter.drill.pth_npth == "separate-files");
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
    CHECK(contains(silk->text, "X0005000000Y0017000000D02*"));
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

TEST_CASE("PCB fabrication writer matches representative golden native output fixtures") {
    const auto fixture = make_fabrication_circuit();
    const auto board = make_fabrication_board(fixture);
    const auto footprints = fabrication_footprints();

    const auto result = volt::io::write_pcb_fabrication_files(board, footprints);

    REQUIRE_FALSE(result.loss_report.has_warnings());
    const auto expected = std::vector<std::pair<std::string, std::string>>{
        {"Control.GTL", "native_fabrication_control.GTL"},
        {"Control.GBL", "native_fabrication_control.GBL"},
        {"Control.GTS", "native_fabrication_control.GTS"},
        {"Control.GBS", "native_fabrication_control.GBS"},
        {"Control.GTO", "native_fabrication_control.GTO"},
        {"Control.GTP", "native_fabrication_control.GTP"},
        {"Control.GKO", "native_fabrication_control.GKO"},
        {"Control-PTH.TXT", "native_fabrication_control-PTH.TXT"},
        {"Control-NPTH.TXT", "native_fabrication_control-NPTH.TXT"},
    };
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
    for (const auto &[filename, fixture_name] : expected) {
        const auto *file = find_file(result, filename);
        REQUIRE(file != nullptr);
        CHECK(file->text == read_fixture(fixture_name));
    }
}

TEST_CASE("PCB fabrication writer emits unconnected mapped pads without fabrication loss") {
    auto circuit = volt::Circuit{};
    const auto passive = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Optional, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto definition =
        circuit.add_component_definition(volt::ComponentDefinition{"TestPoint", {passive}});
    const auto component =
        circuit.instantiate_component(definition, volt::ReferenceDesignator{"TP1"});
    circuit.select_physical_part(component, volt::PhysicalPart{
                                                volt::ManufacturerPart{"Volt", "TP-SMD"},
                                                volt::PackageRef{"TH"},
                                                volt::FootprintRef{"test", "OnePinThroughHole"},
                                                std::vector{volt::PinPadMapping{passive, "1"}},
                                            });

    auto board = volt::Board{circuit, volt::BoardName{"Control"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{10.0, 10.0}));
    static_cast<void>(board.cache_footprint_definition(through_hole_footprint()));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        component, volt::BoardPoint{5.0, 5.0}, volt::BoardRotation::degrees(0.0)}));

    const auto result = volt::io::write_pcb_fabrication_files(board, fabrication_footprints());

    CHECK_FALSE(result.loss_report.has_warnings());
    const auto *top_copper = find_file(result, "Control.GTL");
    REQUIRE(top_copper != nullptr);
    CHECK(contains(top_copper->text, "%TF.FileFunction,Copper,L1,Top*%"));
}

TEST_CASE("PCB fabrication writer keeps numeric output locale-stable") {
    const auto fixture = make_fabrication_circuit();
    const auto board = make_fabrication_board(fixture);
    const auto footprints = fabrication_footprints();
    [[maybe_unused]] const auto scoped_locale =
        ScopedLocale{std::locale{std::locale::classic(), new CommaDecimalNumpunct}};

    const auto result = volt::io::write_pcb_fabrication_files(board, footprints);

    const auto *top_copper = find_file(result, "Control.GTL");
    REQUIRE(top_copper != nullptr);
    CHECK(contains(top_copper->text, "%ADD10C,0.250000*%"));
    CHECK_FALSE(contains(top_copper->text, "%ADD10C,0,250000*%"));
    const auto *pth = find_file(result, "Control-PTH.TXT");
    REQUIRE(pth != nullptr);
    CHECK(contains(pth->text, "T02C0.800000"));
    CHECK_FALSE(contains(pth->text, "T02C0,800000"));
}

TEST_CASE("PCB fabrication writer reports unsupported board text glyphs") {
    const auto fixture = make_fabrication_circuit();
    auto board = make_fabrication_board(fixture);
    const auto text = board.add_text(volt::BoardText{"@", volt::BoardPoint{8.0, 18.0},
                                                     volt::BoardRotation::degrees(0.0),
                                                     volt::BoardLayerId{2}, 1.0, true});

    const auto result = volt::io::write_pcb_fabrication_files(board, fabrication_footprints());

    REQUIRE(result.loss_report.warnings().size() == 1);
    CHECK(result.loss_report.warnings().front().construct == "board.text.character");
    CHECK(result.loss_report.warnings().front().fabrication_impact ==
          volt::io::PcbFabricationLossImpact::FabCritical);

    const auto diagnostics = volt::io::fabrication_diagnostics(result.loss_report);
    REQUIRE(diagnostics.count() == 1);
    CHECK(diagnostics.diagnostics().front().code() ==
          volt::DiagnosticCode{
              std::string{volt::pcb_fabrication_diagnostic_codes::NativeFabUnsupportedGeometry}});
    CHECK(diagnostics.diagnostics().front().entities() ==
          std::vector{volt::EntityRef::board_text(text),
                      volt::EntityRef::board_layer(volt::BoardLayerId{2})});
    REQUIRE(diagnostics.diagnostics().front().rule().has_value());
    CHECK(diagnostics.diagnostics().front().rule().value() == "board.text.character");
}

TEST_CASE("PCB fabrication writer reports finished hole diameter loss") {
    const auto fixture = make_fabrication_circuit();
    auto board = make_fabrication_board(fixture);
    static_cast<void>(board.add_feature(
        volt::BoardFeature::hole("FH", volt::BoardPoint{8.0, 4.0}, 2.4, false, "mounting", 2.0)));

    const auto result = volt::io::write_pcb_fabrication_files(board, fabrication_footprints());

    REQUIRE(result.loss_report.warnings().size() == 1);
    CHECK(result.loss_report.warnings().front().construct ==
          "board.feature.hole.finished_diameter");
    CHECK(result.loss_report.warnings().front().fabrication_impact ==
          volt::io::PcbFabricationLossImpact::FabCritical);

    const auto diagnostics = volt::io::fabrication_diagnostics(result.loss_report);
    REQUIRE(diagnostics.count() == 1);
    CHECK(diagnostics.diagnostics().front().code() ==
          volt::DiagnosticCode{
              std::string{volt::pcb_fabrication_diagnostic_codes::NativeFabLossyGeometry}});
    CHECK(diagnostics.diagnostics().front().severity() == volt::Severity::Warning);
    REQUIRE(diagnostics.diagnostics().front().rule().has_value());
    CHECK(diagnostics.diagnostics().front().rule().value() ==
          "board.feature.hole.finished_diameter");
}

TEST_CASE("PCB fabrication writer reports unsupported geometry and native diagnostics") {
    const auto fixture = make_fabrication_circuit();
    auto board = make_fabrication_board(fixture);
    const auto slot = board.add_feature(volt::BoardFeature::slot(
        "SLOT", volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{5.0, 1.0}, 1.0, false));
    const auto cutout = board.add_feature(volt::BoardFeature::cutout(
        "CUT", std::vector{volt::BoardPoint{22.0, 1.0}, volt::BoardPoint{25.0, 1.0},
                           volt::BoardPoint{25.0, 4.0}, volt::BoardPoint{22.0, 4.0}}));
    const auto circle =
        board.add_feature(volt::BoardFeature::circle("FID", volt::BoardPoint{26.0, 5.0}, 1.0));

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
    const auto *slot_diagnostic =
        find_diagnostic(diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_GEOMETRY", "board.feature.slot");
    REQUIRE(slot_diagnostic != nullptr);
    CHECK(slot_diagnostic->entities() == std::vector{volt::EntityRef::board_feature(slot)});
    const auto *cutout_diagnostic =
        find_diagnostic(diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_GEOMETRY", "board.feature.cutout");
    REQUIRE(cutout_diagnostic != nullptr);
    CHECK(cutout_diagnostic->entities() == std::vector{volt::EntityRef::board_feature(cutout)});
    const auto *circle_diagnostic =
        find_diagnostic(diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_GEOMETRY", "board.feature.circle");
    REQUIRE(circle_diagnostic != nullptr);
    CHECK(circle_diagnostic->entities() == std::vector{volt::EntityRef::board_feature(circle)});
}

TEST_CASE("PCB fabrication writer reports unsupported copper layer data") {
    const auto fixture = make_fabrication_circuit();
    auto board = make_fabrication_board(fixture);
    const auto duplicate_top = board.add_layer(
        volt::BoardLayer{"TopAux", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto inner = board.add_layer(
        volt::BoardLayer{"In1.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Inner});
    const auto duplicate_track = board.add_track(volt::BoardTrack{
        fixture.ground,
        duplicate_top,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{2.0, 1.0}},
        0.25,
    });
    const auto inner_track = board.add_track(volt::BoardTrack{
        fixture.ground,
        inner,
        std::vector{volt::BoardPoint{1.0, 2.0}, volt::BoardPoint{2.0, 2.0}},
        0.25,
    });

    const auto result = volt::io::write_pcb_fabrication_files(board, fabrication_footprints());

    REQUIRE(result.loss_report.warnings().size() >= 4);
    CHECK(result.loss_report.has_fab_critical_warnings());

    const auto diagnostics = volt::io::fabrication_diagnostics(result.loss_report);
    CHECK(has_diagnostic(diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_LAYER",
                         "board.layer.copper_stack_membership",
                         std::vector{volt::EntityRef::board_layer(duplicate_top)}));
    CHECK(has_diagnostic(diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_LAYER",
                         "board.layer.copper_stack_membership",
                         std::vector{volt::EntityRef::board_layer(inner)}));
    CHECK(has_diagnostic(diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_LAYER", "board.track.layer",
                         std::vector{volt::EntityRef::board_track(duplicate_track),
                                     volt::EntityRef::board_layer(duplicate_top)}));
    CHECK(has_diagnostic(diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_LAYER", "board.track.layer",
                         std::vector{volt::EntityRef::board_track(inner_track),
                                     volt::EntityRef::board_layer(inner)}));
}

TEST_CASE("PCB fabrication writer derives copper output from two-layer board stack") {
    const auto fixture = make_fabrication_circuit();
    auto board = make_fabrication_board(fixture);
    const auto duplicate_top = board.add_layer(
        volt::BoardLayer{"TopAux", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_layer_stack(
        volt::LayerStack{{volt::BoardLayerId{0}, duplicate_top, volt::BoardLayerId{1}}, 1.6});

    const auto result = volt::io::write_pcb_fabrication_files(board, fabrication_footprints());

    const auto diagnostics = volt::io::fabrication_diagnostics(result.loss_report);
    const auto *diagnostic = find_diagnostic(diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_LAYER",
                                             "board.layer_stack.copper_count");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->entities() ==
          std::vector{volt::EntityRef::board_layer(volt::BoardLayerId{0}),
                      volt::EntityRef::board_layer(duplicate_top),
                      volt::EntityRef::board_layer(volt::BoardLayerId{1})});
    CHECK(find_file(result, "Control.GTL") == nullptr);
    CHECK(find_file(result, "Control.GBL") == nullptr);
}

TEST_CASE("PCB fabrication writer reports missing stackup without inventing copper files") {
    const auto fixture = make_fabrication_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"NoStack"}};
    static_cast<void>(board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top}));
    static_cast<void>(board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom}));
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));

    const auto result = volt::io::write_pcb_fabrication_files(board, fabrication_footprints());

    const auto diagnostics = volt::io::fabrication_diagnostics(result.loss_report);
    const auto *diagnostic =
        find_diagnostic(diagnostics, "PCB_NATIVE_FAB_MISSING_GEOMETRY", "board.layer_stack");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->entities() == std::vector{volt::EntityRef::board()});
    CHECK(find_file(result, "NoStack.GTL") == nullptr);
    CHECK(find_file(result, "NoStack.GBL") == nullptr);
}
