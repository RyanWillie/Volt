#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/net_class_resolution.hpp>
#include <volt/circuit/net_classes.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/quantities.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints.hpp>

namespace {

template <typename Model>
concept CanAssignNetClass = requires(Model model, volt::NetId net, volt::NetClassId rule) {
    model.assign_net_class(net, rule);
};

static_assert(!CanAssignNetClass<volt::NetClasses>);

[[nodiscard]] const volt::Diagnostic *find_diagnostic(const volt::DiagnosticReport &report,
                                                      const std::string &code) {
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code() == volt::DiagnosticCode{code}) {
            return &diagnostic;
        }
    }
    return nullptr;
}

[[nodiscard]] volt::ElectricalAttributeSpec net_voltage_spec() {
    return volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };
}

} // namespace

TEST_CASE("NetClasses stores net classes by stable ID and name") {
    auto high_voltage = volt::NetClass{volt::NetClassName{"HighVoltage"}};
    high_voltage.set_maximum_net_voltage(volt::Quantity{volt::UnitDimension::Voltage, 60.0});
    high_voltage.set_copper_clearance_mm(0.6);

    auto model = volt::NetClasses{};
    const auto high_voltage_id = model.add_net_class(std::move(high_voltage));
    const auto logic_id = model.add_net_class(volt::NetClass{volt::NetClassName{"Logic"}});

    REQUIRE(model.net_class_count() == 2);
    CHECK(model.net_class(high_voltage_id).name() == volt::NetClassName{"HighVoltage"});
    REQUIRE(model.net_class(high_voltage_id).maximum_net_voltage().has_value());
    CHECK(model.net_class(high_voltage_id).maximum_net_voltage()->value() == 60.0);
    REQUIRE(model.net_class(high_voltage_id).copper_clearance_mm().has_value());
    CHECK(model.net_class(high_voltage_id).copper_clearance_mm().value() == 0.6);
    CHECK(model.net_class_by_name(volt::NetClassName{"HighVoltage"}) == high_voltage_id);
    CHECK(model.net_class_by_name(volt::NetClassName{"Logic"}) == logic_id);
    CHECK_THROWS_AS(model.add_net_class(volt::NetClass{volt::NetClassName{"Logic"}}),
                    std::logic_error);

    CHECK_FALSE(model.net_class_for_net(volt::NetId{7}).has_value());
    CHECK(model.net_class_assignments().empty());
}

TEST_CASE("NetClass rejects malformed local constraints") {
    CHECK_THROWS_AS(volt::NetClassName{""}, std::invalid_argument);

    auto net_class = volt::NetClass{volt::NetClassName{"Power"}};
    CHECK_THROWS_AS(
        net_class.set_maximum_net_voltage(volt::Quantity{volt::UnitDimension::Current, 1.0}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        net_class.set_maximum_net_voltage(volt::Quantity{volt::UnitDimension::Voltage, -1.0}),
        std::invalid_argument);
    CHECK_THROWS_AS(net_class.set_copper_clearance_mm(-0.1), std::invalid_argument);
    CHECK_THROWS_AS(net_class.set_copper_clearance_mm(std::numeric_limits<double>::infinity()),
                    std::invalid_argument);
}

TEST_CASE("Circuit owns net-class intent and rejects dangling assignments") {
    auto circuit = volt::Circuit{};
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});
    const auto net_class = circuit.add_net_class(volt::NetClass{volt::NetClassName{"PowerRails"}});

    CHECK(circuit.assign_net_class(net, net_class));
    CHECK_FALSE(circuit.assign_net_class(net, net_class));
    CHECK(circuit.net_class_for_net(net) == net_class);
    CHECK(circuit.net_class_assignments() ==
          std::vector<std::pair<volt::NetId, volt::NetClassId>>{{net, net_class}});

    CHECK_THROWS_AS(circuit.assign_net_class(volt::NetId{99}, net_class), std::out_of_range);
    CHECK_THROWS_AS(circuit.assign_net_class(net, volt::NetClassId{99}), std::out_of_range);
}

TEST_CASE("Circuit electrical validation applies assigned net-class voltage limits") {
    auto circuit = volt::Circuit{};
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});
    auto net_class = volt::NetClass{volt::NetClassName{"Logic"}};
    net_class.set_maximum_net_voltage(volt::Quantity{volt::UnitDimension::Voltage, 3.6});
    const auto net_class_id = circuit.add_net_class(std::move(net_class));
    circuit.assign_net_class(net, net_class_id);
    circuit.set_net_electrical_attribute(
        net, net_voltage_spec(),
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});

    const auto report = volt::validate_electrical_rules(circuit);

    const auto *violation = find_diagnostic(report, "NET_CLASS_VOLTAGE_EXCEEDED");
    REQUIRE(violation != nullptr);
    CHECK(violation->severity() == volt::Severity::Error);
    CHECK(violation->entities() == std::vector{volt::EntityRef::net(net)});
}

TEST_CASE("Board validation applies assigned net-class copper clearance") {
    auto circuit = volt::Circuit{};
    const auto first_net = circuit.add_net(volt::Net{volt::NetName{"HV"}, volt::NetKind::Power});
    const auto second_net =
        circuit.add_net(volt::Net{volt::NetName{"LOGIC"}, volt::NetKind::Signal});
    auto net_class = volt::NetClass{volt::NetClassName{"HighVoltage"}};
    net_class.set_copper_clearance_mm(0.5);
    const auto net_class_id = circuit.add_net_class(std::move(net_class));
    circuit.assign_net_class(first_net, net_class_id);

    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{10.0, 10.0}));
    board.set_design_rules(volt::BoardDesignRules{0.10, 0.05, 0.20, 0.45, 0.0});
    const auto first_track = board.add_track(volt::BoardTrack{
        first_net,
        front,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}},
        0.10,
    });
    const auto second_track = board.add_track(volt::BoardTrack{
        second_net,
        front,
        std::vector{volt::BoardPoint{1.0, 1.45}, volt::BoardPoint{8.0, 1.45}},
        0.10,
    });

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *clearance = find_diagnostic(report, "PCB_COPPER_CLEARANCE_VIOLATION");
    REQUIRE(clearance != nullptr);
    CHECK(clearance->severity() == volt::Severity::Error);
    CHECK(clearance->entities() == std::vector{
                                       volt::EntityRef::board_track(first_track),
                                       volt::EntityRef::board_track(second_track),
                                       volt::EntityRef::net(first_net),
                                       volt::EntityRef::net(second_net),
                                       volt::EntityRef::board_layer(front),
                                   });
}

TEST_CASE("NetClass rejects malformed physical constraints") {
    auto net_class = volt::NetClass{volt::NetClassName{"Power"}};

    CHECK_THROWS_AS(net_class.set_track_width_mm(0.0), std::invalid_argument);
    CHECK_THROWS_AS(net_class.set_track_width_mm(-0.2), std::invalid_argument);
    CHECK_THROWS_AS(net_class.set_track_width_mm(std::numeric_limits<double>::infinity()),
                    std::invalid_argument);
    CHECK_THROWS_AS(net_class.set_via_size_mm(0.0, 0.6), std::invalid_argument);
    CHECK_THROWS_AS(net_class.set_via_size_mm(0.3, 0.3), std::invalid_argument);
    CHECK_THROWS_AS(net_class.set_via_size_mm(0.6, 0.3), std::invalid_argument);
    CHECK_THROWS_AS(net_class.set_allowed_layer_names({}), std::invalid_argument);
    CHECK_THROWS_AS(net_class.set_allowed_layer_names({""}), std::invalid_argument);
    CHECK_THROWS_AS(net_class.set_allowed_layer_names({"F.Cu", "F.Cu"}), std::invalid_argument);

    net_class.set_track_width_mm(0.3);
    net_class.set_via_size_mm(0.3, 0.6);
    net_class.set_allowed_layer_names({"F.Cu", "B.Cu"});
    net_class.set_priority(5);
    net_class.set_default_for_net_kind(volt::NetKind::Power);

    CHECK(net_class.track_width_mm() == 0.3);
    CHECK(net_class.via_drill_mm() == 0.3);
    CHECK(net_class.via_diameter_mm() == 0.6);
    CHECK(net_class.allowed_layer_names() == std::vector<std::string>{"F.Cu", "B.Cu"});
    CHECK(net_class.priority() == 5);
    CHECK(net_class.default_for_net_kind() == volt::NetKind::Power);
}

TEST_CASE("IPC calculators derive deterministic net-class rule values with provenance") {
    const auto width = volt::ipc2221_trace_width_from_current_mm(
        1.0, 10.0, 1.0, volt::NetClassTraceEnvironment::External);
    CHECK(width.value_mm == Catch::Approx(0.3003762222));
    CHECK(width.derivation.calculator_id == "ipc-2221.trace-width.current");
    CHECK(width.derivation.standard == "IPC-2221");
    REQUIRE(width.derivation.inputs.size() == 4);
    CHECK(width.derivation.inputs[0].name == "current");
    CHECK(width.derivation.inputs[0].value == 1.0);
    CHECK(width.derivation.inputs[0].unit == "A");

    const auto inner_width = volt::ipc2221_trace_width_from_current_mm(
        1.0, 10.0, 1.0, volt::NetClassTraceEnvironment::Internal);
    CHECK(inner_width.value_mm == Catch::Approx(0.7814106717));

    const auto stripline =
        volt::dielectric_height_spacing_mm(0.18, volt::NetClassDielectricSpacingRule::Stripline1H);
    CHECK(stripline.value_mm == Catch::Approx(0.18));
    CHECK(stripline.derivation.calculator_id == "volt.spacing.stripline-1h");

    const auto microstrip =
        volt::dielectric_height_spacing_mm(0.18, volt::NetClassDielectricSpacingRule::Microstrip2H);
    CHECK(microstrip.value_mm == Catch::Approx(0.36));

    const auto voltage_clearance = volt::ipc2221_external_voltage_clearance_mm(600.0);
    CHECK(voltage_clearance.value_mm == Catch::Approx(1.3));
    CHECK(voltage_clearance.derivation.calculator_id == "ipc-2221.clearance.external-voltage");
}

TEST_CASE("NetClass resolves hand-set rule values before derived values") {
    auto net_class = volt::NetClass{volt::NetClassName{"Power"}};
    net_class.derive_track_width(volt::ipc2221_trace_width_from_current_mm(
        1.0, 10.0, 1.0, volt::NetClassTraceEnvironment::External));
    REQUIRE(net_class.track_width_mm().has_value());
    CHECK(net_class.track_width_mm().value() == Catch::Approx(0.3003762222));
    REQUIRE(net_class.derived_track_width().has_value());
    CHECK(net_class.derived_track_width()->value_mm == Catch::Approx(0.3003762222));

    net_class.set_track_width_mm(0.5);
    CHECK(net_class.track_width_mm() == 0.5);
    REQUIRE(net_class.derived_track_width().has_value());
    CHECK(net_class.derived_track_width()->value_mm == Catch::Approx(0.3003762222));

    auto circuit = volt::Circuit{};
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});
    circuit.assign_net_class(net, circuit.add_net_class(std::move(net_class)));

    const auto rules = volt::resolve_net_class_rules(circuit, net);
    CHECK(rules.track_width_mm == 0.5);
    CHECK_FALSE(rules.track_width_derivation.has_value());
    REQUIRE(rules.derived_track_width.has_value());
    CHECK(rules.derived_track_width->value_mm == Catch::Approx(0.3003762222));
}

TEST_CASE("Net class resolution exposes provenance for effective derived rule values") {
    auto circuit = volt::Circuit{};
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});
    auto net_class = volt::NetClass{volt::NetClassName{"DerivedPower"}};
    net_class.derive_track_width(volt::ipc2221_trace_width_from_current_mm(
        1.0, 10.0, 1.0, volt::NetClassTraceEnvironment::External));
    circuit.assign_net_class(net, circuit.add_net_class(std::move(net_class)));

    const auto rules = volt::resolve_net_class_rules(circuit, net);

    CHECK(rules.track_width_mm == Catch::Approx(0.3003762222));
    REQUIRE(rules.track_width_derivation.has_value());
    CHECK(rules.track_width_derivation->calculator_id == "ipc-2221.trace-width.current");
}

TEST_CASE("Net class resolution prefers explicit assignment over intent defaults") {
    auto circuit = volt::Circuit{};
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});

    auto power_default = volt::NetClass{volt::NetClassName{"PowerDefault"}};
    power_default.set_default_for_net_kind(volt::NetKind::Power);
    const auto power_default_id = circuit.add_net_class(std::move(power_default));
    const auto explicit_id = circuit.add_net_class(volt::NetClass{volt::NetClassName{"Explicit"}});

    CHECK(volt::resolve_net_class(circuit, net) == power_default_id);

    CHECK(circuit.assign_net_class(net, explicit_id));
    CHECK(volt::resolve_net_class(circuit, net) == explicit_id);
}

TEST_CASE("Net class resolution picks the highest-priority kind default deterministically") {
    auto circuit = volt::Circuit{};
    const auto power_net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});
    const auto signal_net =
        circuit.add_net(volt::Net{volt::NetName{"DATA"}, volt::NetKind::Signal});

    auto low = volt::NetClass{volt::NetClassName{"LowPriority"}};
    low.set_default_for_net_kind(volt::NetKind::Power);
    low.set_priority(1);
    const auto low_id = circuit.add_net_class(std::move(low));

    auto high = volt::NetClass{volt::NetClassName{"HighPriority"}};
    high.set_default_for_net_kind(volt::NetKind::Power);
    high.set_priority(5);
    const auto high_id = circuit.add_net_class(std::move(high));

    auto tied = volt::NetClass{volt::NetClassName{"TiedPriority"}};
    tied.set_default_for_net_kind(volt::NetKind::Power);
    tied.set_priority(5);
    static_cast<void>(circuit.add_net_class(std::move(tied)));

    CHECK(volt::resolve_net_class(circuit, power_net) == high_id);
    CHECK_FALSE(volt::resolve_net_class(circuit, signal_net).has_value());
    CHECK(low_id != high_id);
}

TEST_CASE("Net class pair clearance resolves to the larger class value with a floor") {
    auto circuit = volt::Circuit{};
    const auto first = circuit.add_net(volt::Net{volt::NetName{"HV"}, volt::NetKind::Power});
    const auto second = circuit.add_net(volt::Net{volt::NetName{"LOGIC"}, volt::NetKind::Signal});

    auto wide = volt::NetClass{volt::NetClassName{"Wide"}};
    wide.set_copper_clearance_mm(0.8);
    circuit.assign_net_class(first, circuit.add_net_class(std::move(wide)));

    auto narrow = volt::NetClass{volt::NetClassName{"Narrow"}};
    narrow.set_copper_clearance_mm(0.2);
    circuit.assign_net_class(second, circuit.add_net_class(std::move(narrow)));

    CHECK(volt::resolve_copper_clearance_mm(circuit, first, second, 0.1) == 0.8);
    CHECK(volt::resolve_copper_clearance_mm(circuit, second, second, 0.1) == 0.2);
    CHECK(volt::resolve_copper_clearance_mm(circuit, second, second, 1.5) == 1.5);
}

TEST_CASE("Circuit electrical validation applies kind-default net-class voltage limits") {
    auto circuit = volt::Circuit{};
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});

    auto net_class = volt::NetClass{volt::NetClassName{"PowerDefault"}};
    net_class.set_maximum_net_voltage(volt::Quantity{volt::UnitDimension::Voltage, 3.6});
    net_class.set_default_for_net_kind(volt::NetKind::Power);
    static_cast<void>(circuit.add_net_class(std::move(net_class)));
    circuit.set_net_electrical_attribute(
        net, net_voltage_spec(),
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});

    const auto report = volt::validate_electrical_rules(circuit);

    const auto *violation = find_diagnostic(report, "NET_CLASS_VOLTAGE_EXCEEDED");
    REQUIRE(violation != nullptr);
    CHECK(violation->entities() == std::vector{volt::EntityRef::net(net)});
}

TEST_CASE("Board validation applies net-class track width, via size, and layer rules") {
    auto circuit = volt::Circuit{};
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});

    auto net_class = volt::NetClass{volt::NetClassName{"PowerRules"}};
    net_class.set_track_width_mm(0.5);
    net_class.set_via_size_mm(0.4, 0.8);
    net_class.set_allowed_layer_names({"F.Cu"});
    const auto net_class_id = circuit.add_net_class(std::move(net_class));
    circuit.assign_net_class(net, net_class_id);

    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0});

    const auto thin_track = board.add_track(volt::BoardTrack{
        net,
        front,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}},
        0.20,
    });
    const auto wrong_layer_track = board.add_track(volt::BoardTrack{
        net,
        back,
        std::vector{volt::BoardPoint{1.0, 5.0}, volt::BoardPoint{8.0, 5.0}},
        0.50,
    });
    const auto wrong_layer_zone = board.add_zone(volt::BoardZone{
        std::vector{volt::BoardPoint{10.0, 2.0}, volt::BoardPoint{14.0, 2.0},
                    volt::BoardPoint{14.0, 6.0}, volt::BoardPoint{10.0, 6.0}},
        std::vector{back},
        net,
    });
    const auto small_via =
        board.add_via(volt::BoardVia{net, volt::BoardPoint{12.0, 12.0}, front, back, 0.20, 0.40});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *width = find_diagnostic(report, "PCB_TRACK_WIDTH_BELOW_NET_CLASS");
    REQUIRE(width != nullptr);
    CHECK(width->severity() == volt::Severity::Error);
    CHECK(width->entities() == std::vector{volt::EntityRef::board_track(thin_track),
                                           volt::EntityRef::net(net),
                                           volt::EntityRef::board_layer(front)});
    REQUIRE(width->overlays().size() == 1);
    CHECK(width->overlays()[0].kind() == volt::DiagnosticOverlayKind::Segment);
    CHECK(width->overlays()[0].points() ==
          std::vector{volt::DiagnosticPoint{1.0, 1.0}, volt::DiagnosticPoint{8.0, 1.0}});
    CHECK(width->overlays()[0].entities() == std::vector{volt::EntityRef::board_track(thin_track)});
    CHECK(width->overlays()[0].layers() == std::vector{front});
    REQUIRE(width->measurement().has_value());
    CHECK(width->measurement().value() == volt::DiagnosticMeasurement{0.20, 0.50});

    const auto *drill = find_diagnostic(report, "PCB_VIA_DRILL_BELOW_NET_CLASS");
    REQUIRE(drill != nullptr);
    CHECK(drill->entities() ==
          std::vector{volt::EntityRef::board_via(small_via), volt::EntityRef::net(net)});
    REQUIRE(drill->overlays().size() == 1);
    CHECK(drill->overlays()[0].kind() == volt::DiagnosticOverlayKind::Point);
    CHECK(drill->overlays()[0].points() == std::vector{volt::DiagnosticPoint{12.0, 12.0}});
    CHECK(drill->overlays()[0].entities() == std::vector{volt::EntityRef::board_via(small_via)});
    CHECK(drill->overlays()[0].layers() == std::vector{front, back});
    REQUIRE(drill->measurement().has_value());
    CHECK(drill->measurement().value() == volt::DiagnosticMeasurement{0.20, 0.40});

    const auto *diameter = find_diagnostic(report, "PCB_VIA_DIAMETER_BELOW_NET_CLASS");
    REQUIRE(diameter != nullptr);
    REQUIRE(diameter->overlays().size() == 1);
    CHECK(diameter->overlays()[0].kind() == volt::DiagnosticOverlayKind::Point);
    CHECK(diameter->overlays()[0].points() == std::vector{volt::DiagnosticPoint{12.0, 12.0}});
    CHECK(diameter->overlays()[0].entities() == std::vector{volt::EntityRef::board_via(small_via)});
    CHECK(diameter->overlays()[0].layers() == std::vector{front, back});
    REQUIRE(diameter->measurement().has_value());
    CHECK(diameter->measurement().value() == volt::DiagnosticMeasurement{0.40, 0.80});

    const auto *layer = find_diagnostic(report, "PCB_COPPER_ON_DISALLOWED_LAYER");
    REQUIRE(layer != nullptr);
    CHECK(layer->severity() == volt::Severity::Error);
    CHECK(layer->entities() == std::vector{volt::EntityRef::board_track(wrong_layer_track),
                                           volt::EntityRef::net(net),
                                           volt::EntityRef::board_layer(back)});
    REQUIRE(layer->overlays().size() == 1);
    CHECK(layer->overlays()[0].kind() == volt::DiagnosticOverlayKind::Segment);
    CHECK(layer->overlays()[0].points() ==
          std::vector{volt::DiagnosticPoint{1.0, 5.0}, volt::DiagnosticPoint{8.0, 5.0}});
    CHECK(layer->overlays()[0].entities() ==
          std::vector{volt::EntityRef::board_track(wrong_layer_track)});
    CHECK(layer->overlays()[0].layers() == std::vector{back});
    CHECK_FALSE(layer->measurement().has_value());

    auto zone_layers = std::vector<const volt::Diagnostic *>{};
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code() == volt::DiagnosticCode{"PCB_COPPER_ON_DISALLOWED_LAYER"} &&
            diagnostic.entities().front() == volt::EntityRef::board_zone(wrong_layer_zone)) {
            zone_layers.push_back(&diagnostic);
        }
    }
    REQUIRE(zone_layers.size() == 1);
    CHECK(zone_layers[0]->entities() == std::vector{volt::EntityRef::board_zone(wrong_layer_zone),
                                                    volt::EntityRef::net(net),
                                                    volt::EntityRef::board_layer(back)});
    REQUIRE(zone_layers[0]->overlays().size() == 1);
    CHECK(zone_layers[0]->overlays()[0].kind() == volt::DiagnosticOverlayKind::Polygon);
    CHECK(zone_layers[0]->overlays()[0].points() ==
          std::vector{volt::DiagnosticPoint{10.0, 2.0}, volt::DiagnosticPoint{14.0, 2.0},
                      volt::DiagnosticPoint{14.0, 6.0}, volt::DiagnosticPoint{10.0, 6.0}});
    CHECK(zone_layers[0]->overlays()[0].entities() ==
          std::vector{volt::EntityRef::board_zone(wrong_layer_zone)});
    CHECK(zone_layers[0]->overlays()[0].layers() == std::vector{back});
    CHECK_FALSE(zone_layers[0]->measurement().has_value());
}

TEST_CASE("Net class layer scope and explicit layer names are mutually exclusive") {
    auto scoped = volt::NetClass{volt::NetClassName{"Scoped"}};
    scoped.set_layer_scope(volt::NetClassLayerScope::OuterOnly);
    CHECK(scoped.layer_scope() == volt::NetClassLayerScope::OuterOnly);
    CHECK_THROWS_AS(scoped.set_allowed_layer_names({"F.Cu"}), std::logic_error);

    auto named = volt::NetClass{volt::NetClassName{"Named"}};
    named.set_allowed_layer_names({"In2.Cu"});
    CHECK_THROWS_AS(named.set_layer_scope(volt::NetClassLayerScope::InnerOnly), std::logic_error);
    named.set_layer_scope(volt::NetClassLayerScope::AnyCopper);
}

TEST_CASE("Board validation applies semantic layer scopes on a four-layer board") {
    auto circuit = volt::Circuit{};
    const auto outer_net = circuit.add_net(volt::Net{volt::NetName{"RF"}, volt::NetKind::Signal});
    const auto inner_net =
        circuit.add_net(volt::Net{volt::NetName{"QUIET"}, volt::NetKind::Signal});

    auto outer_class = volt::NetClass{volt::NetClassName{"OuterOnly"}};
    outer_class.set_layer_scope(volt::NetClassLayerScope::OuterOnly);
    circuit.assign_net_class(outer_net, circuit.add_net_class(std::move(outer_class)));

    auto inner_class = volt::NetClass{volt::NetClassName{"InnerOnly"}};
    inner_class.set_layer_scope(volt::NetClassLayerScope::InnerOnly);
    circuit.assign_net_class(inner_net, circuit.add_net_class(std::move(inner_class)));

    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto in1 = board.add_layer(
        volt::BoardLayer{"In1.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Inner});
    const auto in2 = board.add_layer(
        volt::BoardLayer{"In2.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Inner});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));

    const auto outer_ok = board.add_track(volt::BoardTrack{
        outer_net, back, std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}}, 0.2});
    const auto outer_bad = board.add_track(volt::BoardTrack{
        outer_net, in1, std::vector{volt::BoardPoint{1.0, 4.0}, volt::BoardPoint{8.0, 4.0}}, 0.2});
    const auto inner_ok = board.add_track(volt::BoardTrack{
        inner_net, in2, std::vector{volt::BoardPoint{1.0, 8.0}, volt::BoardPoint{8.0, 8.0}}, 0.2});
    const auto inner_bad = board.add_track(volt::BoardTrack{
        inner_net, front, std::vector{volt::BoardPoint{1.0, 12.0}, volt::BoardPoint{8.0, 12.0}},
        0.2});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    auto disallowed = std::vector<volt::EntityRef>{};
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code() == volt::DiagnosticCode{"PCB_COPPER_ON_DISALLOWED_LAYER"}) {
            disallowed.push_back(diagnostic.entities().front());
        }
    }
    CHECK(disallowed == std::vector{volt::EntityRef::board_track(outer_bad),
                                    volt::EntityRef::board_track(inner_bad)});
    CHECK(outer_ok != outer_bad);
    CHECK(inner_ok != inner_bad);
}

TEST_CASE("Board validation applies clearance-matrix pair rules") {
    auto circuit = volt::Circuit{};
    const auto first_net = circuit.add_net(volt::Net{volt::NetName{"A"}, volt::NetKind::Signal});
    const auto second_net = circuit.add_net(volt::Net{volt::NetName{"B"}, volt::NetKind::Signal});

    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    auto rules = volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0};
    rules.set_clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Track, 0.5);
    board.set_design_rules(rules);

    const auto first_track = board.add_track(volt::BoardTrack{
        first_net, front, std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}},
        0.10});
    const auto second_track = board.add_track(volt::BoardTrack{
        second_net, front, std::vector{volt::BoardPoint{1.0, 1.4}, volt::BoardPoint{8.0, 1.4}},
        0.10});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *violation = find_diagnostic(report, "PCB_COPPER_CLEARANCE_VIOLATION");
    REQUIRE(violation != nullptr);
    CHECK(violation->message() == "Copper on different nets violates required "
                                  "track-to-track clearance");
    CHECK(violation->entities() == std::vector{
                                       volt::EntityRef::board_track(first_track),
                                       volt::EntityRef::board_track(second_track),
                                       volt::EntityRef::net(first_net),
                                       volt::EntityRef::net(second_net),
                                       volt::EntityRef::board_layer(front),
                                   });
}

TEST_CASE("Board validation lets same-room clearance replace net-class and matrix clearances") {
    auto circuit = volt::Circuit{};
    const auto first_net = circuit.add_net(volt::Net{volt::NetName{"A"}, volt::NetKind::Signal});
    const auto second_net = circuit.add_net(volt::Net{volt::NetName{"B"}, volt::NetKind::Signal});

    auto net_class = volt::NetClass{volt::NetClassName{"Local"}};
    net_class.set_copper_clearance_mm(0.10);
    circuit.assign_net_class(first_net, circuit.add_net_class(std::move(net_class)));

    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    auto rules = volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0};
    rules.set_clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Track, 0.10);
    board.set_design_rules(rules);

    auto room = volt::BoardRoom{
        "BGA escape",
        volt::BoardOutline::rectangle(volt::BoardPoint{0.5, 0.5}, volt::BoardSize{8.0, 2.0}),
        std::vector{front},
    };
    room.set_copper_clearance_mm(0.50);
    const auto room_id = board.add_room(std::move(room));

    const auto first_track = board.add_track(volt::BoardTrack{
        first_net, front, std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}},
        0.10});
    const auto second_track = board.add_track(volt::BoardTrack{
        second_net, front, std::vector{volt::BoardPoint{1.0, 1.4}, volt::BoardPoint{8.0, 1.4}},
        0.10});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *violation = find_diagnostic(report, "PCB_COPPER_CLEARANCE_VIOLATION");
    REQUIRE(violation != nullptr);
    CHECK(violation->message() == "Copper on different nets violates required "
                                  "track-to-track clearance");
    CHECK(violation->entities() == std::vector{
                                       volt::EntityRef::board_track(first_track),
                                       volt::EntityRef::board_track(second_track),
                                       volt::EntityRef::net(first_net),
                                       volt::EntityRef::net(second_net),
                                       volt::EntityRef::board_layer(front),
                                       volt::EntityRef::board_room(room_id),
                                   });
}

TEST_CASE("Board validation lets lower same-room clearance suppress net-class clearance") {
    auto circuit = volt::Circuit{};
    const auto first_net = circuit.add_net(volt::Net{volt::NetName{"A"}, volt::NetKind::Signal});
    const auto second_net = circuit.add_net(volt::Net{volt::NetName{"B"}, volt::NetKind::Signal});

    auto net_class = volt::NetClass{volt::NetClassName{"Local"}};
    net_class.set_copper_clearance_mm(0.50);
    circuit.assign_net_class(first_net, circuit.add_net_class(std::move(net_class)));

    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    auto rules = volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0};
    rules.set_clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Track, 0.10);
    board.set_design_rules(rules);

    auto room = volt::BoardRoom{
        "BGA escape",
        volt::BoardOutline::rectangle(volt::BoardPoint{0.5, 0.5}, volt::BoardSize{8.0, 2.0}),
        std::vector{front},
    };
    room.set_copper_clearance_mm(0.10);
    [[maybe_unused]] const auto room_id = board.add_room(std::move(room));

    [[maybe_unused]] const auto first_track = board.add_track(volt::BoardTrack{
        first_net, front, std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}},
        0.10});
    [[maybe_unused]] const auto second_track = board.add_track(volt::BoardTrack{
        second_net, front, std::vector{volt::BoardPoint{1.0, 1.4}, volt::BoardPoint{8.0, 1.4}},
        0.10});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    CHECK(find_diagnostic(report, "PCB_COPPER_CLEARANCE_VIOLATION") == nullptr);

    auto no_room_board = volt::Board{circuit};
    const auto no_room_front = no_room_board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    no_room_board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    auto no_room_rules = volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0};
    no_room_rules.set_clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Track,
                                   0.10);
    no_room_board.set_design_rules(no_room_rules);

    [[maybe_unused]] const auto no_room_first_track = no_room_board.add_track(volt::BoardTrack{
        first_net, no_room_front,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}}, 0.10});
    [[maybe_unused]] const auto no_room_second_track = no_room_board.add_track(volt::BoardTrack{
        second_net, no_room_front,
        std::vector{volt::BoardPoint{1.0, 1.4}, volt::BoardPoint{8.0, 1.4}}, 0.10});

    const auto no_room_report =
        volt::validate_board(no_room_board, volt::builtin_footprint_library());

    CHECK(find_diagnostic(no_room_report, "PCB_COPPER_CLEARANCE_VIOLATION") != nullptr);
}

TEST_CASE("Board validation ignores room clearance when only one shape is inside") {
    auto circuit = volt::Circuit{};
    const auto first_net = circuit.add_net(volt::Net{volt::NetName{"A"}, volt::NetKind::Signal});
    const auto second_net = circuit.add_net(volt::Net{volt::NetName{"B"}, volt::NetKind::Signal});

    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0});

    auto room = volt::BoardRoom{
        "One track only",
        volt::BoardOutline::rectangle(volt::BoardPoint{0.5, 0.5}, volt::BoardSize{8.0, 0.7}),
        std::vector{front},
    };
    room.set_copper_clearance_mm(0.50);
    [[maybe_unused]] const auto room_id = board.add_room(std::move(room));

    [[maybe_unused]] const auto first_track = board.add_track(volt::BoardTrack{
        first_net, front, std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}},
        0.10});
    [[maybe_unused]] const auto second_track = board.add_track(volt::BoardTrack{
        second_net, front, std::vector{volt::BoardPoint{1.0, 1.4}, volt::BoardPoint{8.0, 1.4}},
        0.10});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    CHECK(find_diagnostic(report, "PCB_COPPER_CLEARANCE_VIOLATION") == nullptr);
}

TEST_CASE("Board validation lets room track width replace resolved net-class width") {
    auto circuit = volt::Circuit{};
    const auto net = circuit.add_net(volt::Net{volt::NetName{"DATA"}, volt::NetKind::Signal});

    auto net_class = volt::NetClass{volt::NetClassName{"Signal"}};
    net_class.set_track_width_mm(0.10);
    circuit.assign_net_class(net, circuit.add_net_class(std::move(net_class)));

    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0});

    auto room = volt::BoardRoom{
        "Fine pitch escape",
        volt::BoardOutline::rectangle(volt::BoardPoint{0.5, 0.5}, volt::BoardSize{8.0, 2.0}),
        std::vector{front},
    };
    room.set_track_width_mm(0.20);
    const auto room_id = board.add_room(std::move(room));

    const auto track = board.add_track(volt::BoardTrack{
        net, front, std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}}, 0.15});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *width = find_diagnostic(report, "PCB_TRACK_WIDTH_BELOW_NET_CLASS");
    REQUIRE(width != nullptr);
    CHECK(width->entities() ==
          std::vector{volt::EntityRef::board_track(track), volt::EntityRef::net(net),
                      volt::EntityRef::board_layer(front), volt::EntityRef::board_room(room_id)});
    REQUIRE(width->overlays().size() == 1);
    CHECK(width->overlays()[0].kind() == volt::DiagnosticOverlayKind::Segment);
    CHECK(width->overlays()[0].points() ==
          std::vector{volt::DiagnosticPoint{1.0, 1.0}, volt::DiagnosticPoint{8.0, 1.0}});
    CHECK(width->overlays()[0].entities() == std::vector{volt::EntityRef::board_track(track)});
    CHECK(width->overlays()[0].layers() == std::vector{front});
    REQUIRE(width->measurement().has_value());
    CHECK(width->measurement().value() == volt::DiagnosticMeasurement{0.15, 0.20});
    CHECK(find_diagnostic(report, "PCB_TRACK_WIDTH_BELOW_MINIMUM") == nullptr);
}

TEST_CASE("Board validation resolves overlapping rooms by priority then lowest ID") {
    auto circuit = volt::Circuit{};
    const auto first_net = circuit.add_net(volt::Net{volt::NetName{"A"}, volt::NetKind::Signal});
    const auto second_net = circuit.add_net(volt::Net{volt::NetName{"B"}, volt::NetKind::Signal});

    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0});
    const auto outline =
        volt::BoardOutline::rectangle(volt::BoardPoint{0.5, 0.5}, volt::BoardSize{8.0, 2.0});

    auto low_priority = volt::BoardRoom{"Low", outline, std::vector{front}, 1};
    low_priority.set_copper_clearance_mm(0.20);
    [[maybe_unused]] const auto low = board.add_room(std::move(low_priority));

    auto high_priority = volt::BoardRoom{"High", outline, std::vector{front}, 5};
    high_priority.set_copper_clearance_mm(0.50);
    const auto high = board.add_room(std::move(high_priority));

    auto high_tie = volt::BoardRoom{"High tie", outline, std::vector{front}, 5};
    high_tie.set_copper_clearance_mm(0.20);
    [[maybe_unused]] const auto tie = board.add_room(std::move(high_tie));

    const auto first_track = board.add_track(volt::BoardTrack{
        first_net, front, std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}},
        0.10});
    const auto second_track = board.add_track(volt::BoardTrack{
        second_net, front, std::vector{volt::BoardPoint{1.0, 1.4}, volt::BoardPoint{8.0, 1.4}},
        0.10});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *violation = find_diagnostic(report, "PCB_COPPER_CLEARANCE_VIOLATION");
    REQUIRE(violation != nullptr);
    CHECK(violation->entities() == std::vector{
                                       volt::EntityRef::board_track(first_track),
                                       volt::EntityRef::board_track(second_track),
                                       volt::EntityRef::net(first_net),
                                       volt::EntityRef::net(second_net),
                                       volt::EntityRef::board_layer(front),
                                       volt::EntityRef::board_room(high),
                                   });
}
