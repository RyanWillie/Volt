#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/io/pcb/fabrication_writer.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace {

struct FabCircuit {
    volt::Circuit circuit;
    volt::ComponentId header;
    volt::ComponentId resistor;
    volt::PinDefId header_one;
    volt::PinDefId header_two;
    volt::PinDefId resistor_one;
    volt::PinDefId resistor_two;
    volt::NetId vcc;
    volt::NetId gnd;
};

struct FabBoard {
    volt::Board board;
    volt::BoardLayerId front;
    volt::BoardLayerId back;
    volt::BoardLayerId front_mask;
    volt::BoardLayerId back_mask;
    volt::BoardLayerId front_paste;
    volt::BoardLayerId silk;
    volt::BoardLayerId edge_cuts;
    volt::ComponentPlacementId header_placement;
    volt::ComponentPlacementId resistor_placement;
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

[[nodiscard]] volt::PinDefId add_passive_pin(volt::Circuit &circuit, std::string name) {
    return circuit.add_pin_definition(volt::PinDefinition{
        name, std::move(name), volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive,
        volt::ElectricalSignalDomain::Unspecified, volt::ElectricalDriveKind::Passive});
}

[[nodiscard]] FabCircuit make_fab_circuit() {
    auto circuit = volt::Circuit{};
    const auto header_one = add_passive_pin(circuit, "1");
    const auto header_two = add_passive_pin(circuit, "2");
    const auto resistor_one = add_passive_pin(circuit, "A");
    const auto resistor_two = add_passive_pin(circuit, "B");
    const auto header_definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Header", {header_one, header_two}});
    const auto resistor_definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", {resistor_one, resistor_two}});
    const auto header =
        circuit.instantiate_component(header_definition, volt::ReferenceDesignator{"J1"});
    const auto resistor =
        circuit.instantiate_component(resistor_definition, volt::ReferenceDesignator{"R1"});
    const auto vcc = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
    const auto gnd = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});

    circuit.connect(vcc, volt::queries::pin_by_definition(circuit, header, header_one).value());
    circuit.connect(vcc, volt::queries::pin_by_definition(circuit, resistor, resistor_one).value());
    circuit.connect(gnd, volt::queries::pin_by_definition(circuit, header, header_two).value());
    circuit.connect(gnd, volt::queries::pin_by_definition(circuit, resistor, resistor_two).value());
    circuit.select_physical_part(header, volt::PhysicalPart{
                                             volt::ManufacturerPart{"Generic", "HDR-1x02"},
                                             volt::PackageRef{"2.54mm-1x02"},
                                             volt::FootprintRef{"test", "Header_1x02_Fab"},
                                             std::vector{volt::PinPadMapping{header_one, "1"},
                                                         volt::PinPadMapping{header_two, "2"}},
                                         });
    circuit.select_physical_part(resistor, volt::PhysicalPart{
                                               volt::ManufacturerPart{"Yageo", "RC0603"},
                                               volt::PackageRef{"0603"},
                                               volt::FootprintRef{"test", "R_0603_Fab"},
                                               std::vector{volt::PinPadMapping{resistor_one, "1"},
                                                           volt::PinPadMapping{resistor_two, "2"}},
                                           });

    return FabCircuit{std::move(circuit), header,       resistor, header_one, header_two,
                      resistor_one,       resistor_two, vcc,      gnd};
}

[[nodiscard]] volt::FootprintDefinition header_footprint() {
    return volt::FootprintDefinition{
        volt::FootprintRef{"test", "Header_1x02_Fab"},
        std::vector{
            volt::FootprintPad::through_hole(
                "1", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, -1.27},
                volt::FootprintSize{1.6, 1.6}, volt::FootprintLayerSet::through_hole(),
                volt::FootprintDrill{0.9, volt::FootprintPadPlating::Plated}),
            volt::FootprintPad::through_hole(
                "2", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, 1.27},
                volt::FootprintSize{1.6, 1.6}, volt::FootprintLayerSet::through_hole(),
                volt::FootprintDrill{0.9, volt::FootprintPadPlating::Plated}),
        },
    };
}

[[nodiscard]] volt::FootprintDefinition
resistor_footprint(volt::FootprintPadShape shape = volt::FootprintPadShape::Rectangle,
                   bool include_body = false) {
    auto body = std::optional<volt::FootprintPolygon>{};
    if (include_body) {
        body = volt::FootprintPolygon{std::vector{
            volt::FootprintPoint{-1.5, -0.7},
            volt::FootprintPoint{1.5, -0.7},
            volt::FootprintPoint{1.5, 0.7},
            volt::FootprintPoint{-1.5, 0.7},
        }};
    }
    return volt::FootprintDefinition{
        volt::FootprintRef{"test", "R_0603_Fab"},
        std::vector{
            volt::FootprintPad::surface_mount("1", shape, volt::FootprintPoint{-1.0, 0.0},
                                              volt::FootprintSize{1.0, 0.8},
                                              volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount("2", shape, volt::FootprintPoint{1.0, 0.0},
                                              volt::FootprintSize{1.0, 0.8},
                                              volt::FootprintLayerSet::front_smd()),
        },
        std::nullopt,
        std::move(body),
    };
}

[[nodiscard]] volt::FootprintLibrary
make_fab_footprints(volt::FootprintPadShape resistor_shape = volt::FootprintPadShape::Rectangle,
                    bool include_resistor_body = false) {
    auto footprints = volt::FootprintLibrary{};
    footprints.add(header_footprint());
    footprints.add(resistor_footprint(resistor_shape, include_resistor_body));
    return footprints;
}

[[nodiscard]] FabBoard make_fab_board(const FabCircuit &fixture, bool include_outline = true) {
    auto board = volt::Board{fixture.circuit, volt::BoardName{"FabDemo"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    const auto front_mask = board.add_layer(
        volt::BoardLayer{"F.Mask", volt::BoardLayerRole::SolderMask, volt::BoardLayerSide::Top});
    const auto back_mask = board.add_layer(
        volt::BoardLayer{"B.Mask", volt::BoardLayerRole::SolderMask, volt::BoardLayerSide::Bottom});
    const auto front_paste = board.add_layer(
        volt::BoardLayer{"F.Paste", volt::BoardLayerRole::Paste, volt::BoardLayerSide::Top});
    const auto silk = board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});
    const auto edge_cuts = board.add_layer(
        volt::BoardLayer{"Edge.Cuts", volt::BoardLayerRole::EdgeCuts, volt::BoardLayerSide::None});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    if (include_outline) {
        board.set_outline(
            volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 12.0}));
    }
    static_cast<void>(board.add_feature(
        volt::BoardFeature::hole("MH1", volt::BoardPoint{18.0, 3.0}, 2.0, false, "mounting")));
    const auto header_placement = board.place_component(
        volt::ComponentPlacement{fixture.header, volt::BoardPoint{4.0, 6.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, true});
    const auto resistor_placement = board.place_component(
        volt::ComponentPlacement{fixture.resistor, volt::BoardPoint{12.0, 6.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, true});
    static_cast<void>(board.add_track(volt::BoardTrack{
        fixture.vcc,
        front,
        std::vector{volt::BoardPoint{4.0, 4.73}, volt::BoardPoint{8.0, 4.73},
                    volt::BoardPoint{11.0, 6.0}},
        0.25,
    }));
    static_cast<void>(board.add_track(volt::BoardTrack{
        fixture.gnd,
        back,
        std::vector{volt::BoardPoint{4.0, 7.27}, volt::BoardPoint{8.0, 8.0}},
        0.30,
    }));
    static_cast<void>(board.add_via(
        volt::BoardVia{fixture.gnd, volt::BoardPoint{8.0, 8.0}, front, back, 0.35, 0.75}));
    static_cast<void>(board.add_track(volt::BoardTrack{
        fixture.gnd,
        front,
        std::vector{volt::BoardPoint{8.0, 8.0}, volt::BoardPoint{13.0, 6.0}},
        0.25,
    }));
    static_cast<void>(board.add_zone(volt::BoardZone{
        std::vector{volt::BoardPoint{6.0, 9.0}, volt::BoardPoint{14.0, 9.0},
                    volt::BoardPoint{14.0, 11.0}, volt::BoardPoint{6.0, 11.0}},
        std::vector{back},
        fixture.gnd,
    }));
    return FabBoard{std::move(board), front, back,      front_mask,       back_mask,
                    front_paste,      silk,  edge_cuts, header_placement, resistor_placement};
}

[[nodiscard]] const volt::io::PcbFabricationFile *
find_file(const volt::io::PcbFabricationPackage &package, std::string_view name) {
    const auto match = std::find_if(
        package.files.begin(), package.files.end(),
        [name](const volt::io::PcbFabricationFile &file) { return file.name == name; });
    if (match == package.files.end()) {
        return nullptr;
    }
    return &*match;
}

[[nodiscard]] std::vector<std::string> file_names(const volt::io::PcbFabricationPackage &package) {
    auto names = std::vector<std::string>{};
    for (const auto &file : package.files) {
        names.push_back(file.name);
    }
    return names;
}

[[nodiscard]] bool has_diagnostic(const volt::DiagnosticReport &report, std::string_view code,
                                  std::string_view rule) {
    return std::any_of(report.diagnostics().begin(), report.diagnostics().end(),
                       [code, rule](const volt::Diagnostic &diagnostic) {
                           return diagnostic.code().value() == code &&
                                  diagnostic.rule().has_value() &&
                                  diagnostic.rule().value() == rule;
                       });
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

} // namespace

TEST_CASE("native PCB fabrication writer emits deterministic Gerber and drill package") {
    const auto fixture = make_fab_circuit();
    const auto fab = make_fab_board(fixture);
    const auto footprints = make_fab_footprints();

    const auto package = volt::io::write_pcb_fabrication_package(fab.board, footprints);
    const auto repeated = volt::io::write_pcb_fabrication_package(fab.board, footprints);

    CHECK(package.diagnostics.empty());
    CHECK(repeated.files == package.files);
    CHECK(file_names(package) == std::vector<std::string>{
                                     "F_Cu.gbr",
                                     "B_Cu.gbr",
                                     "F_Mask.gbr",
                                     "B_Mask.gbr",
                                     "F_Paste.gbr",
                                     "Edge_Cuts.gbr",
                                     "PTH.drl",
                                     "NPTH.drl",
                                 });

    REQUIRE(find_file(package, "F_Cu.gbr") != nullptr);
    CHECK(find_file(package, "F_Cu.gbr")->contents == R"(G04 Volt Gerber RS-274X F_Cu*
%FSLAX46Y46*%
%MOMM*%
%LPD*%
%TF.FileFunction,Copper,L1,Top*%
%TF.Part,Single*%
%ADD10C,1.600000*%
%ADD11R,1.000000X0.800000*%
%ADD12C,0.250000*%
%ADD13C,0.750000*%
D10*
X4000000Y4730000D03*
X4000000Y7270000D03*
D11*
X11000000Y6000000D03*
X13000000Y6000000D03*
D12*
X4000000Y4730000D02*
X8000000Y4730000D01*
X11000000Y6000000D01*
X8000000Y8000000D02*
X13000000Y6000000D01*
D13*
X8000000Y8000000D03*
M02*
)");

    REQUIRE(find_file(package, "B_Cu.gbr") != nullptr);
    CHECK(find_file(package, "B_Cu.gbr")->contents == R"(G04 Volt Gerber RS-274X B_Cu*
%FSLAX46Y46*%
%MOMM*%
%LPD*%
%TF.FileFunction,Copper,L2,Bot*%
%TF.Part,Single*%
%ADD10C,1.600000*%
%ADD11C,0.300000*%
%ADD12C,0.750000*%
D10*
X4000000Y4730000D03*
X4000000Y7270000D03*
D11*
X4000000Y7270000D02*
X8000000Y8000000D01*
D12*
X8000000Y8000000D03*
G36*
X6000000Y9000000D02*
X14000000Y9000000D01*
X14000000Y11000000D01*
X6000000Y11000000D01*
X6000000Y9000000D01*
G37*
M02*
)");

    REQUIRE(find_file(package, "F_Mask.gbr") != nullptr);
    CHECK(find_file(package, "F_Mask.gbr")->contents.find("%TF.FileFunction,Soldermask,Top*%") !=
          std::string::npos);
    CHECK(find_file(package, "F_Mask.gbr")->contents.find("X11000000Y6000000D03*") !=
          std::string::npos);
    CHECK(find_file(package, "F_Mask.gbr")->contents.find("X8000000Y8000000D03*") ==
          std::string::npos);
    REQUIRE(find_file(package, "B_Mask.gbr") != nullptr);
    CHECK(find_file(package, "B_Mask.gbr")->contents.find("X8000000Y8000000D03*") ==
          std::string::npos);

    REQUIRE(find_file(package, "F_Paste.gbr") != nullptr);
    CHECK(find_file(package, "F_Paste.gbr")->contents.find("%TF.FileFunction,Paste,Top*%") !=
          std::string::npos);
    CHECK(find_file(package, "F_Paste.gbr")->contents.find("X13000000Y6000000D03*") !=
          std::string::npos);

    REQUIRE(find_file(package, "Edge_Cuts.gbr") != nullptr);
    CHECK(find_file(package, "Edge_Cuts.gbr")->contents == R"(G04 Volt Gerber RS-274X Edge_Cuts*
%FSLAX46Y46*%
%MOMM*%
%LPD*%
%TF.FileFunction,Profile,NP*%
%TF.Part,Single*%
%ADD10C,0.100000*%
D10*
X0Y0D02*
X20000000Y0D01*
X20000000Y12000000D01*
X0Y12000000D01*
X0Y0D01*
M02*
)");

    REQUIRE(find_file(package, "PTH.drl") != nullptr);
    CHECK(find_file(package, "PTH.drl")->contents == R"(M48
;DRILL file generated by Volt
METRIC,TZ
T01C0.350000
T02C0.900000
%
G05
T01
X8000000Y8000000
T02
X4000000Y4730000
X4000000Y7270000
M30
)");

    REQUIRE(find_file(package, "NPTH.drl") != nullptr);
    CHECK(find_file(package, "NPTH.drl")->contents == R"(M48
;DRILL file generated by Volt
METRIC,TZ
T01C2.000000
%
G05
T01
X18000000Y3000000
M30
)");
}

TEST_CASE("native PCB fabrication writer keeps decimal output locale-stable") {
    const auto fixture = make_fab_circuit();
    const auto fab = make_fab_board(fixture);
    const auto footprints = make_fab_footprints();
    [[maybe_unused]] const auto scoped_locale =
        ScopedLocale{std::locale{std::locale::classic(), new CommaDecimalNumpunct}};

    const auto package = volt::io::write_pcb_fabrication_package(fab.board, footprints);

    REQUIRE(find_file(package, "F_Cu.gbr") != nullptr);
    CHECK(find_file(package, "F_Cu.gbr")->contents.find("%ADD10C,1.600000*%") != std::string::npos);
    CHECK(find_file(package, "F_Cu.gbr")->contents.find("%ADD10C,1,600000*%") == std::string::npos);
    REQUIRE(find_file(package, "PTH.drl") != nullptr);
    CHECK(find_file(package, "PTH.drl")->contents.find("T02C0.900000") != std::string::npos);
    CHECK(find_file(package, "PTH.drl")->contents.find("T02C0,900000") == std::string::npos);
}

TEST_CASE("native PCB fabrication writer groups Excellon tools by emitted precision") {
    const auto fixture = make_fab_circuit();
    auto fab = make_fab_board(fixture);
    static_cast<void>(fab.board.add_feature(volt::BoardFeature::hole(
        "NPTH_A", volt::BoardPoint{2.0, 2.0}, 0.5000001, false, "mounting")));
    static_cast<void>(fab.board.add_feature(volt::BoardFeature::hole(
        "NPTH_B", volt::BoardPoint{3.0, 2.0}, 0.5000002, false, "mounting")));
    static_cast<void>(fab.board.add_feature(volt::BoardFeature::hole(
        "NPTH_C", volt::BoardPoint{4.0, 2.0}, 0.5000008, false, "mounting")));

    const auto package = volt::io::write_pcb_fabrication_package(fab.board, make_fab_footprints());

    REQUIRE(package.diagnostics.empty());
    REQUIRE(find_file(package, "NPTH.drl") != nullptr);
    CHECK(find_file(package, "NPTH.drl")->contents == R"(M48
;DRILL file generated by Volt
METRIC,TZ
T01C0.500000
T02C0.500001
T03C2.000000
%
G05
T01
X2000000Y2000000
X3000000Y2000000
T02
X4000000Y2000000
T03
X18000000Y3000000
M30
)");
}

TEST_CASE("native PCB fabrication writer reports ambiguous footprint body artwork") {
    const auto fixture = make_fab_circuit();
    const auto fab = make_fab_board(fixture);

    const auto package = volt::io::write_pcb_fabrication_package(
        fab.board, make_fab_footprints(volt::FootprintPadShape::Rectangle, true));

    REQUIRE(package.diagnostics.count() == 1);
    const auto *diagnostic = find_diagnostic(
        package.diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_GEOMETRY", "footprint.body");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->entities() ==
          std::vector{volt::EntityRef::component_placement(fab.resistor_placement)});
    CHECK(find_file(package, "F_SilkS.gbr") == nullptr);
}

TEST_CASE("native PCB fabrication writer reports finished board-hole diameter loss") {
    const auto fixture = make_fab_circuit();
    auto fab = make_fab_board(fixture);
    const auto feature = fab.board.add_feature(
        volt::BoardFeature::hole("FIN", volt::BoardPoint{2.0, 10.0}, 2.0, false, "mounting", 1.8));

    const auto package = volt::io::write_pcb_fabrication_package(fab.board, make_fab_footprints());

    REQUIRE(package.diagnostics.count() == 1);
    const auto *diagnostic = find_diagnostic(package.diagnostics, "PCB_NATIVE_FAB_LOSSY_GEOMETRY",
                                             "board.feature.hole.finished_diameter");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity() == volt::Severity::Warning);
    CHECK(diagnostic->entities() == std::vector{volt::EntityRef::board_feature(feature)});
    REQUIRE(find_file(package, "NPTH.drl") != nullptr);
    CHECK(find_file(package, "NPTH.drl")->contents.find("T01C2.000000") != std::string::npos);
    CHECK(find_file(package, "NPTH.drl")->contents.find("X2000000Y10000000") != std::string::npos);
}

TEST_CASE("native PCB fabrication writer reports missing outline without inventing profile data") {
    const auto fixture = make_fab_circuit();
    const auto fab = make_fab_board(fixture, false);

    const auto package = volt::io::write_pcb_fabrication_package(fab.board, make_fab_footprints());

    CHECK(find_file(package, "Edge_Cuts.gbr") == nullptr);
    REQUIRE(package.diagnostics.count() == 1);
    CHECK(has_diagnostic(package.diagnostics, "PCB_NATIVE_FAB_MISSING_GEOMETRY", "board.outline"));
    CHECK(package.diagnostics.diagnostics().front().severity() == volt::Severity::Error);
    CHECK(package.diagnostics.diagnostics().front().entities() ==
          std::vector{volt::EntityRef::board()});
}

TEST_CASE("native PCB fabrication writer reports missing stackup without inventing copper order") {
    const auto fixture = make_fab_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"NoStack"}};
    static_cast<void>(board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top}));
    static_cast<void>(board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom}));
    static_cast<void>(board.add_layer(
        volt::BoardLayer{"Edge.Cuts", volt::BoardLayerRole::EdgeCuts, volt::BoardLayerSide::None}));
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 12.0}));

    const auto package = volt::io::write_pcb_fabrication_package(board, make_fab_footprints());

    REQUIRE(package.diagnostics.count() == 1);
    CHECK(has_diagnostic(package.diagnostics, "PCB_NATIVE_FAB_MISSING_GEOMETRY",
                         "board.layer_stack"));
    CHECK(find_file(package, "F_Cu.gbr") == nullptr);
    CHECK(find_file(package, "B_Cu.gbr") == nullptr);
    REQUIRE(find_file(package, "Edge_Cuts.gbr") != nullptr);
}

TEST_CASE("native PCB fabrication writer reports unsupported board layers deterministically") {
    const auto fixture = make_fab_circuit();
    auto fab = make_fab_board(fixture);
    const auto inner = fab.board.add_layer(
        volt::BoardLayer{"In1.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Inner});
    const auto track = fab.board.add_track(volt::BoardTrack{
        fixture.vcc,
        inner,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{2.0, 1.0}},
        0.2,
    });

    const auto package = volt::io::write_pcb_fabrication_package(fab.board, make_fab_footprints());

    REQUIRE(package.diagnostics.count() == 2);
    const auto *layer_diagnostic =
        find_diagnostic(package.diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_LAYER",
                        "board.layer.copper_stack_membership");
    REQUIRE(layer_diagnostic != nullptr);
    CHECK(layer_diagnostic->entities() == std::vector{volt::EntityRef::board_layer(inner)});
    CHECK(has_diagnostic(package.diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_LAYER",
                         "board.track.layer"));
    const auto *diagnostic = find_diagnostic(
        package.diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_LAYER", "board.track.layer");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->entities() ==
          std::vector{volt::EntityRef::board_track(track), volt::EntityRef::board_layer(inner)});
    CHECK(find_file(package, "F_Cu.gbr")->contents.find("X1000000Y1000000") == std::string::npos);
}

TEST_CASE("native PCB fabrication writer derives copper output layers from the board stack") {
    const auto fixture = make_fab_circuit();
    auto fab = make_fab_board(fixture);
    const auto duplicate_front = fab.board.add_layer(
        volt::BoardLayer{"TopAux", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    fab.board.set_layer_stack(volt::LayerStack{{fab.front, duplicate_front, fab.back}, 1.6});

    const auto package = volt::io::write_pcb_fabrication_package(fab.board, make_fab_footprints());

    const auto *diagnostic = find_diagnostic(
        package.diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_LAYER", "board.layer_stack.copper_count");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->entities() == std::vector{volt::EntityRef::board_layer(fab.front),
                                                volt::EntityRef::board_layer(duplicate_front),
                                                volt::EntityRef::board_layer(fab.back)});
    CHECK(find_file(package, "F_Cu.gbr") == nullptr);
}

TEST_CASE("native PCB fabrication writer reports unsupported profile geometry deterministically") {
    const auto fixture = make_fab_circuit();
    auto fab = make_fab_board(fixture);
    const auto slot = fab.board.add_feature(volt::BoardFeature::slot(
        "SLOT", volt::BoardPoint{2.0, 2.0}, volt::BoardPoint{4.0, 2.0}, 0.8, false, "mounting"));

    const auto package = volt::io::write_pcb_fabrication_package(fab.board, make_fab_footprints());

    REQUIRE(package.diagnostics.count() == 1);
    CHECK(has_diagnostic(package.diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_GEOMETRY",
                         "board.feature.slot"));
    const auto *diagnostic = find_diagnostic(
        package.diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_GEOMETRY", "board.feature.slot");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->entities() == std::vector{volt::EntityRef::board_feature(slot)});
}

TEST_CASE("native PCB fabrication writer rejects rounded-rectangle pad geometry instead of "
          "approximating it") {
    const auto fixture = make_fab_circuit();
    const auto fab = make_fab_board(fixture);

    const auto package = volt::io::write_pcb_fabrication_package(
        fab.board, make_fab_footprints(volt::FootprintPadShape::RoundedRectangle));

    REQUIRE(package.diagnostics.count() == 2);
    CHECK(has_diagnostic(package.diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_GEOMETRY",
                         "footprint.pad.rounded_rectangle"));
    const auto *diagnostic =
        find_diagnostic(package.diagnostics, "PCB_NATIVE_FAB_UNSUPPORTED_GEOMETRY",
                        "footprint.pad.rounded_rectangle");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->entities() ==
          std::vector{volt::EntityRef::component_placement(fab.resistor_placement),
                      volt::EntityRef::footprint_pad(volt::FootprintPadId{0})});
    CHECK(find_file(package, "F_Cu.gbr")->contents.find("%ADD11R,1.000000X0.800000*%") ==
          std::string::npos);
}
