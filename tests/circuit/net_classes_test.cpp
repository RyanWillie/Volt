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
    const auto small_via =
        board.add_via(volt::BoardVia{net, volt::BoardPoint{12.0, 12.0}, front, back, 0.20, 0.40});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *width = find_diagnostic(report, "PCB_TRACK_WIDTH_BELOW_NET_CLASS");
    REQUIRE(width != nullptr);
    CHECK(width->severity() == volt::Severity::Error);
    CHECK(width->entities() == std::vector{volt::EntityRef::board_track(thin_track),
                                           volt::EntityRef::net(net),
                                           volt::EntityRef::board_layer(front)});

    const auto *drill = find_diagnostic(report, "PCB_VIA_DRILL_BELOW_NET_CLASS");
    REQUIRE(drill != nullptr);
    CHECK(drill->entities() ==
          std::vector{volt::EntityRef::board_via(small_via), volt::EntityRef::net(net)});

    const auto *diameter = find_diagnostic(report, "PCB_VIA_DIAMETER_BELOW_NET_CLASS");
    REQUIRE(diameter != nullptr);

    const auto *layer = find_diagnostic(report, "PCB_COPPER_ON_DISALLOWED_LAYER");
    REQUIRE(layer != nullptr);
    CHECK(layer->severity() == volt::Severity::Error);
    CHECK(layer->entities() == std::vector{volt::EntityRef::board_track(wrong_layer_track),
                                           volt::EntityRef::net(net),
                                           volt::EntityRef::board_layer(back)});
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
