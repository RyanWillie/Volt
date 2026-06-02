#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/rule_classes.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/quantities.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints.hpp>

namespace {

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

TEST_CASE("RuleClasses stores rule classes and deterministic net assignments") {
    auto high_voltage = volt::RuleClass{volt::RuleClassName{"HighVoltage"}};
    high_voltage.set_maximum_net_voltage(volt::Quantity{volt::UnitDimension::Voltage, 60.0});
    high_voltage.set_copper_clearance_mm(0.6);

    auto model = volt::RuleClasses{};
    const auto high_voltage_id = model.add_rule_class(std::move(high_voltage));
    const auto logic_id = model.add_rule_class(volt::RuleClass{volt::RuleClassName{"Logic"}});

    REQUIRE(model.rule_class_count() == 2);
    CHECK(model.rule_class(high_voltage_id).name() == volt::RuleClassName{"HighVoltage"});
    REQUIRE(model.rule_class(high_voltage_id).maximum_net_voltage().has_value());
    CHECK(model.rule_class(high_voltage_id).maximum_net_voltage()->value() == 60.0);
    REQUIRE(model.rule_class(high_voltage_id).copper_clearance_mm().has_value());
    CHECK(model.rule_class(high_voltage_id).copper_clearance_mm().value() == 0.6);
    CHECK(model.rule_class_by_name(volt::RuleClassName{"HighVoltage"}) == high_voltage_id);
    CHECK_THROWS_AS(model.add_rule_class(volt::RuleClass{volt::RuleClassName{"Logic"}}),
                    std::logic_error);

    CHECK(model.assign_net_rule_class(volt::NetId{7}, high_voltage_id));
    CHECK_FALSE(model.assign_net_rule_class(volt::NetId{7}, high_voltage_id));
    CHECK(model.assign_net_rule_class(volt::NetId{7}, logic_id));

    CHECK(model.rule_class_for_net(volt::NetId{7}) == logic_id);
    CHECK(model.net_rule_class_assignments() ==
          std::vector<std::pair<volt::NetId, volt::RuleClassId>>{{volt::NetId{7}, logic_id}});
    CHECK_THROWS_AS(model.assign_net_rule_class(volt::NetId{8}, volt::RuleClassId{99}),
                    std::out_of_range);
}

TEST_CASE("RuleClass rejects malformed local constraints") {
    CHECK_THROWS_AS(volt::RuleClassName{""}, std::invalid_argument);

    auto rule_class = volt::RuleClass{volt::RuleClassName{"Power"}};
    CHECK_THROWS_AS(
        rule_class.set_maximum_net_voltage(volt::Quantity{volt::UnitDimension::Current, 1.0}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        rule_class.set_maximum_net_voltage(volt::Quantity{volt::UnitDimension::Voltage, -1.0}),
        std::invalid_argument);
    CHECK_THROWS_AS(rule_class.set_copper_clearance_mm(-0.1), std::invalid_argument);
    CHECK_THROWS_AS(rule_class.set_copper_clearance_mm(std::numeric_limits<double>::infinity()),
                    std::invalid_argument);
}

TEST_CASE("Circuit owns rule-class intent and rejects dangling assignments") {
    auto circuit = volt::Circuit{};
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});
    const auto rule_class =
        circuit.add_rule_class(volt::RuleClass{volt::RuleClassName{"PowerRails"}});

    CHECK(circuit.assign_net_rule_class(net, rule_class));
    CHECK_FALSE(circuit.assign_net_rule_class(net, rule_class));
    CHECK(circuit.rule_class_for_net(net) == rule_class);
    CHECK(circuit.net_rule_class_assignments() ==
          std::vector<std::pair<volt::NetId, volt::RuleClassId>>{{net, rule_class}});

    CHECK_THROWS_AS(circuit.assign_net_rule_class(volt::NetId{99}, rule_class), std::out_of_range);
    CHECK_THROWS_AS(circuit.assign_net_rule_class(net, volt::RuleClassId{99}), std::out_of_range);
}

TEST_CASE("Circuit electrical validation applies assigned rule-class voltage limits") {
    auto circuit = volt::Circuit{};
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});
    auto rule_class = volt::RuleClass{volt::RuleClassName{"Logic"}};
    rule_class.set_maximum_net_voltage(volt::Quantity{volt::UnitDimension::Voltage, 3.6});
    const auto rule_class_id = circuit.add_rule_class(std::move(rule_class));
    circuit.assign_net_rule_class(net, rule_class_id);
    circuit.set_net_electrical_attribute(
        net, net_voltage_spec(),
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});

    const auto report = volt::validate_electrical_rules(circuit);

    const auto *violation = find_diagnostic(report, "NET_RULE_CLASS_VOLTAGE_EXCEEDED");
    REQUIRE(violation != nullptr);
    CHECK(violation->severity() == volt::Severity::Error);
    CHECK(violation->entities() == std::vector{volt::EntityRef::net(net)});
}

TEST_CASE("Board validation applies assigned rule-class copper clearance") {
    auto circuit = volt::Circuit{};
    const auto first_net = circuit.add_net(volt::Net{volt::NetName{"HV"}, volt::NetKind::Power});
    const auto second_net =
        circuit.add_net(volt::Net{volt::NetName{"LOGIC"}, volt::NetKind::Signal});
    auto rule_class = volt::RuleClass{volt::RuleClassName{"HighVoltage"}};
    rule_class.set_copper_clearance_mm(0.5);
    const auto rule_class_id = circuit.add_rule_class(std::move(rule_class));
    circuit.assign_net_rule_class(first_net, rule_class_id);

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
