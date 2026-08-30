#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/electrical/passive_model.hpp>

namespace {

using namespace volt;

ContentHash evidence(char digit) { return ContentHash{"sha256:" + std::string(64, digit)}; }

ComponentDefinition component(std::vector<std::string> pins = {"A", "B"}) {
    Circuit circuit;
    ComponentSpec spec{.name = "Passive contract"};
    ComponentContractSpec contract{.key = ComponentKey{"test.passive"}, .pin_keys = {}};
    for (const auto &pin : pins) {
        spec.pins.push_back(PinSpec{.name = pin, .number = pin});
        contract.pin_keys.emplace_back(pin);
    }
    spec.contract = std::move(contract);
    return circuit.get(circuit.define_component(std::move(spec)));
}

template <typename Element>
Element element(UnitDimension dimension, double nominal,
                std::optional<Tolerance> tolerance = std::nullopt) {
    return Element{ModelElementKey{"body"}, ModelTerminalKey{"a"}, ModelTerminalKey{"b"},
                   ModelParameter{Quantity{dimension, nominal}, tolerance}};
}

ModelElement resistor(std::string key = "body", ModelEndpoint from = ModelTerminalKey{"a"},
                      ModelEndpoint to = ModelTerminalKey{"b"}) {
    return ResistanceElement{ModelElementKey{std::move(key)}, std::move(from), std::move(to),
                             ModelParameter{Quantity{UnitDimension::Resistance, 330.0}}};
}

std::vector<ModelTerminal> terminals() {
    return {{ModelTerminalKey{"a"}, PinKey{"A"}}, {ModelTerminalKey{"b"}, PinKey{"B"}}};
}

template <typename Element, typename From = PartElectricalModelBuilder::TerminalHandle>
concept BuilderAcceptsElement =
    requires(PartElectricalModelBuilder &builder, From from,
             PartElectricalModelBuilder::TerminalHandle to, ModelParameter parameter) {
        builder.add<Element>(ModelElementKey{"test"}, from, to, parameter);
    };

static_assert(!std::is_same_v<ModelTerminalKey, ModelInternalNodeKey>);
static_assert(!std::is_same_v<ModelTerminalKey, ModelElementKey>);
static_assert(!std::is_same_v<ModelInternalNodeKey, ModelElementKey>);
static_assert(BuilderAcceptsElement<ResistanceElement>);
static_assert(BuilderAcceptsElement<CapacitanceElement>);
static_assert(BuilderAcceptsElement<InductanceElement>);
static_assert(!BuilderAcceptsElement<Quantity>);
static_assert(!BuilderAcceptsElement<ResistanceElement, ModelTerminalKey>);
static_assert(!std::is_copy_constructible_v<PartElectricalModelBuilder>);
static_assert(!std::is_move_constructible_v<PartElectricalModelBuilder>);
static_assert(std::is_copy_constructible_v<PartElectricalModel>);
static_assert(std::is_same_v<decltype(std::declval<PartElectricalModel &>().elements()),
                             const std::vector<ModelElement> &>);

} // namespace

TEST_CASE("Passive model keys reject empty spellings") {
    CHECK_THROWS_AS(ModelTerminalKey{""}, std::invalid_argument);
    CHECK_THROWS_AS(ModelInternalNodeKey{""}, std::invalid_argument);
    CHECK_THROWS_AS(ModelElementKey{""}, std::invalid_argument);
}

TEST_CASE("Passive parameters normalize asymmetric uncertainty and canonical evidence") {
    const auto parameter = ModelParameter{Quantity{UnitDimension::Resistance, 200.0},
                                          Tolerance::percent(0.01, 0.02),
                                          {evidence('c'), evidence('a'), evidence('c')}};
    const auto absolute =
        ModelParameter{Quantity{UnitDimension::Resistance, 200.0},
                       Tolerance::absolute(Quantity{UnitDimension::Resistance, 2.0},
                                           Quantity{UnitDimension::Resistance, 4.0})};
    REQUIRE(parameter.tolerance());
    CHECK(parameter.nominal() == Quantity{UnitDimension::Resistance, 200.0});
    CHECK(parameter.tolerance()->mode() == ToleranceMode::Absolute);
    CHECK(parameter.tolerance()->minus() == absolute.tolerance()->minus());
    CHECK(parameter.tolerance()->plus() == absolute.tolerance()->plus());
    REQUIRE(parameter.bounds());
    CHECK(parameter.bounds()->minimum() == Quantity{UnitDimension::Resistance, 198.0});
    CHECK(parameter.bounds()->maximum() == Quantity{UnitDimension::Resistance, 204.0});
    CHECK(parameter.evidence() == std::vector{evidence('a'), evidence('c')});
}

TEST_CASE("Passive uncertainty absence differs from explicit zero and normalizes signed zero") {
    const auto absent = ModelParameter{Quantity{UnitDimension::Resistance, -0.0}};
    const auto zero =
        ModelParameter{Quantity{UnitDimension::Resistance, -0.0}, Tolerance::percent(-0.0, 0.0)};
    const auto absolute_zero =
        ModelParameter{Quantity{UnitDimension::Resistance, 0.0},
                       Tolerance::absolute(Quantity{UnitDimension::Resistance, -0.0},
                                           Quantity{UnitDimension::Resistance, -0.0})};
    CHECK_FALSE(absent.tolerance());
    CHECK_FALSE(absent.bounds());
    CHECK_FALSE(std::signbit(absent.nominal().value()));
    REQUIRE(zero.tolerance());
    CHECK_FALSE(std::signbit(zero.nominal().value()));
    CHECK_FALSE(std::signbit(zero.tolerance()->minus().value()));
    CHECK_FALSE(std::signbit(zero.tolerance()->plus().value()));
    CHECK_FALSE(std::signbit(zero.bounds()->minimum()->value()));
    CHECK_FALSE(std::signbit(zero.bounds()->maximum()->value()));
    CHECK_FALSE(std::signbit(absolute_zero.tolerance()->minus().value()));
    CHECK_FALSE(std::signbit(absolute_zero.tolerance()->plus().value()));
    CHECK(zero.bounds()->minimum() == zero.bounds()->maximum());
}

TEST_CASE("Passive element constructors enforce exactly their native dimensions") {
    for (const auto dimension :
         {UnitDimension::Resistance, UnitDimension::Capacitance, UnitDimension::Inductance,
          UnitDimension::Voltage, UnitDimension::Current, UnitDimension::Power,
          UnitDimension::Frequency, UnitDimension::Time, UnitDimension::Temperature,
          UnitDimension::Ratio}) {
        CAPTURE(dimension);
        if (dimension != UnitDimension::Resistance) {
            CHECK_THROWS_AS(element<ResistanceElement>(dimension, 1.0), std::invalid_argument);
        }
        if (dimension != UnitDimension::Capacitance) {
            CHECK_THROWS_AS(element<CapacitanceElement>(dimension, 1.0), std::invalid_argument);
        }
        if (dimension != UnitDimension::Inductance) {
            CHECK_THROWS_AS(element<InductanceElement>(dimension, 1.0), std::invalid_argument);
        }
    }
    CHECK_NOTHROW(element<ResistanceElement>(UnitDimension::Resistance, 1.0));
    CHECK_NOTHROW(element<CapacitanceElement>(UnitDimension::Capacitance, 1.0));
    CHECK_NOTHROW(element<InductanceElement>(UnitDimension::Inductance, 1.0));
    CHECK_THROWS_AS(ModelParameter(Quantity{UnitDimension::Resistance, 1.0},
                                   Tolerance::absolute(Quantity{UnitDimension::Voltage, 0.0},
                                                       Quantity{UnitDimension::Voltage, 0.0})),
                    std::invalid_argument);
}

TEST_CASE("Passive resistance permits an exact zero constraint but no negative nominal or bound") {
    CHECK_NOTHROW(element<ResistanceElement>(UnitDimension::Resistance, 0.0));
    CHECK_NOTHROW(
        element<ResistanceElement>(UnitDimension::Resistance, -0.0, Tolerance::percent(0.0)));
    CHECK_NOTHROW(
        element<ResistanceElement>(UnitDimension::Resistance, 1.0, Tolerance::percent(1.0)));
    CHECK_THROWS_AS(element<ResistanceElement>(UnitDimension::Resistance, -1.0),
                    std::invalid_argument);
    CHECK_THROWS_AS(element<ResistanceElement>(UnitDimension::Resistance, -1.0e-300),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        element<ResistanceElement>(UnitDimension::Resistance, 1.0, Tolerance::percent(1.01, 0.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        element<ResistanceElement>(UnitDimension::Resistance, 0.0,
                                   Tolerance::absolute(Quantity{UnitDimension::Resistance, 1.0},
                                                       Quantity{UnitDimension::Resistance, 0.0})),
        std::invalid_argument);
}

TEST_CASE(
    "Passive storage elements require positive nominal and bounds but allow zero deviations") {
    for (const auto value : {0.0, -0.0, -1.0, -1.0e-300}) {
        CAPTURE(value);
        CHECK_THROWS_AS(element<CapacitanceElement>(UnitDimension::Capacitance, value),
                        std::invalid_argument);
        CHECK_THROWS_AS(element<InductanceElement>(UnitDimension::Inductance, value),
                        std::invalid_argument);
    }
    const double smallest = std::numeric_limits<double>::denorm_min();
    CHECK_NOTHROW(element<CapacitanceElement>(UnitDimension::Capacitance, smallest));
    CHECK_NOTHROW(element<InductanceElement>(UnitDimension::Inductance, smallest));
    for (const auto tolerance :
         {Tolerance::percent(0.0), Tolerance::percent(0.0, 0.2), Tolerance::percent(0.2, 0.0)}) {
        CHECK_NOTHROW(element<CapacitanceElement>(UnitDimension::Capacitance, 1.0, tolerance));
        CHECK_NOTHROW(element<InductanceElement>(UnitDimension::Inductance, 1.0, tolerance));
    }
    CHECK_THROWS_AS(
        element<CapacitanceElement>(UnitDimension::Capacitance, 1.0, Tolerance::percent(1.0, 0.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        element<InductanceElement>(UnitDimension::Inductance, 1.0, Tolerance::percent(1.0, 0.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        element<CapacitanceElement>(UnitDimension::Capacitance, 1.0, Tolerance::percent(2.0, 0.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        element<InductanceElement>(UnitDimension::Inductance, 1.0, Tolerance::percent(2.0, 0.0)),
        std::invalid_argument);
}

TEST_CASE("Passive parameters reject nonfinite input and normalization overflow") {
    const auto maximum = std::numeric_limits<double>::max();
    for (const auto value :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity()}) {
        CHECK_THROWS_AS(ModelParameter(Quantity{UnitDimension::Resistance, value}),
                        std::invalid_argument);
        CHECK_THROWS_AS(Tolerance::percent(value), std::invalid_argument);
        CHECK_THROWS_AS(Tolerance::absolute(Quantity{UnitDimension::Resistance, value},
                                            Quantity{UnitDimension::Resistance, 0.0}),
                        std::invalid_argument);
    }
    CHECK_THROWS_AS(Tolerance::percent(-0.01), std::invalid_argument);
    CHECK_THROWS_AS(Tolerance::absolute(Quantity{UnitDimension::Resistance, -1.0},
                                        Quantity{UnitDimension::Resistance, 0.0}),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        ModelParameter(Quantity{UnitDimension::Resistance, maximum}, Tolerance::percent(2.0, 0.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        ModelParameter(Quantity{UnitDimension::Resistance, maximum}, Tolerance::percent(0.0, 2.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        ModelParameter(Quantity{UnitDimension::Resistance, maximum}, Tolerance::percent(0.0, 1.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        ModelParameter(Quantity{UnitDimension::Resistance, maximum},
                       Tolerance::absolute(Quantity{UnitDimension::Resistance, 0.0},
                                           Quantity{UnitDimension::Resistance, maximum})),
        std::invalid_argument);
    CHECK_THROWS_AS(ModelParameter(Quantity{UnitDimension::Resistance, -maximum},
                                   Tolerance::absolute(Quantity{UnitDimension::Resistance, maximum},
                                                       Quantity{UnitDimension::Resistance, 0.0})),
                    std::invalid_argument);
    CHECK_NOTHROW(element<ResistanceElement>(UnitDimension::Resistance, maximum));
}

TEST_CASE("Passive builder produces resistor and ideal storage models with portable endpoints") {
    for (const auto dimension :
         {UnitDimension::Resistance, UnitDimension::Capacitance, UnitDimension::Inductance}) {
        PartElectricalModelBuilder builder{component()};
        const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
        const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
        const auto parameter = ModelParameter{Quantity{dimension, 1.0}};
        if (dimension == UnitDimension::Resistance) {
            builder.add<ResistanceElement>(ModelElementKey{"body"}, a, b, parameter);
        } else if (dimension == UnitDimension::Capacitance) {
            builder.add<CapacitanceElement>(ModelElementKey{"body"}, a, b, parameter);
        } else {
            builder.add<InductanceElement>(ModelElementKey{"body"}, a, b, parameter);
        }
        const auto model = builder.build();
        REQUIRE(model.elements().size() == 1U);
        CHECK(model.internal_nodes().empty());
        CHECK(model.terminals().size() == 2U);
        CHECK(model.implemented_component() == component().content_identity());
        std::visit(
            [&](const auto &value) {
                CHECK(value.key() == ModelElementKey{"body"});
                CHECK(value.from() == ModelEndpoint{ModelTerminalKey{"a"}});
                CHECK(value.to() == ModelEndpoint{ModelTerminalKey{"b"}});
                CHECK(value.parameter().nominal().dimension() == dimension);
            },
            model.elements().front());
    }
}

TEST_CASE("Passive builder composes capacitor ESR and ESL into one exact portable model") {
    PartElectricalModelBuilder builder{component()};
    const auto p = builder.terminal(ModelTerminalKey{"p"}, PinKey{"A"});
    const auto n = builder.terminal(ModelTerminalKey{"n"}, PinKey{"B"});
    const auto x = builder.internal_node(ModelInternalNodeKey{"after_esr"});
    const auto y = builder.internal_node(ModelInternalNodeKey{"after_esl"});
    builder.add<ResistanceElement>(ModelElementKey{"esr"}, p, x,
                                   ModelParameter{Quantity{UnitDimension::Resistance, 0.08}});
    builder.add<InductanceElement>(ModelElementKey{"esl"}, x, y,
                                   ModelParameter{Quantity{UnitDimension::Inductance, 1.0e-9}});
    builder.add<CapacitanceElement>(
        ModelElementKey{"storage"}, y, n,
        ModelParameter{Quantity{UnitDimension::Capacitance, 10.0e-6}, Tolerance::percent(0.2)});
    const auto model = builder.build();
    REQUIRE(model.elements().size() == 3U);
    CHECK(model.terminals()[0].key() == ModelTerminalKey{"n"});
    CHECK(model.internal_nodes()[0].key() == ModelInternalNodeKey{"after_esl"});
    const auto &esl = std::get<InductanceElement>(model.elements()[0]);
    CHECK(esl.key() == ModelElementKey{"esl"});
    CHECK(esl.from() == ModelEndpoint{ModelInternalNodeKey{"after_esr"}});
    CHECK(esl.to() == ModelEndpoint{ModelInternalNodeKey{"after_esl"}});
    const auto &esr = std::get<ResistanceElement>(model.elements()[1]);
    CHECK(esr.from() == ModelEndpoint{ModelTerminalKey{"p"}});
    const auto &storage = std::get<CapacitanceElement>(model.elements()[2]);
    CHECK(storage.to() == ModelEndpoint{ModelTerminalKey{"n"}});
    CHECK(storage.parameter().tolerance()->mode() == ToleranceMode::Absolute);
}

TEST_CASE("Passive builder supports multi-terminal networks and disconnected used subnetworks") {
    SECTION("Three terminal network shares one explicit common terminal") {
        PartElectricalModelBuilder builder{component({"A", "B", "COM"})};
        const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
        const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
        const auto common = builder.terminal(ModelTerminalKey{"common"}, PinKey{"COM"});
        builder.add<ResistanceElement>(ModelElementKey{"ra"}, a, common,
                                       ModelParameter{Quantity{UnitDimension::Resistance, 1000.0}});
        builder.add<ResistanceElement>(ModelElementKey{"rb"}, b, common,
                                       ModelParameter{Quantity{UnitDimension::Resistance, 2000.0}});
        const auto model = builder.build();
        CHECK(model.terminals().size() == 3U);
        CHECK(model.elements().size() == 2U);
    }
    SECTION("Floating used islands remain diagnosable data") {
        PartElectricalModelBuilder builder{component({"A", "B", "C", "D"})};
        const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
        const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
        const auto c = builder.terminal(ModelTerminalKey{"c"}, PinKey{"C"});
        const auto d = builder.terminal(ModelTerminalKey{"d"}, PinKey{"D"});
        const auto x = builder.internal_node(ModelInternalNodeKey{"x"});
        const auto y = builder.internal_node(ModelInternalNodeKey{"y"});
        const auto parameter = ModelParameter{Quantity{UnitDimension::Resistance, 1.0}};
        builder.add<ResistanceElement>(ModelElementKey{"ab"}, a, b, parameter);
        builder.add<ResistanceElement>(ModelElementKey{"cd"}, c, d, parameter);
        builder.add<ResistanceElement>(ModelElementKey{"private"}, x, y, parameter);
        CHECK_NOTHROW(builder.build());
    }
}

TEST_CASE("Passive builder rejects foreign handles even when their local spelling matches") {
    PartElectricalModelBuilder first{component()};
    PartElectricalModelBuilder second{component()};
    const auto a = first.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    const auto b = first.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
    const auto x = first.internal_node(ModelInternalNodeKey{"x"});
    const auto foreign_a = second.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    const auto foreign_x = second.internal_node(ModelInternalNodeKey{"x"});
    const auto parameter = ModelParameter{Quantity{UnitDimension::Resistance, 1.0}};
    CHECK_THROWS_AS(
        first.add<ResistanceElement>(ModelElementKey{"foreign_a"}, foreign_a, b, parameter),
        std::invalid_argument);
    CHECK_THROWS_AS(
        first.add<ResistanceElement>(ModelElementKey{"foreign_x"}, a, foreign_x, parameter),
        std::invalid_argument);
    CHECK_THROWS_AS(PartElectricalModelBuilder::TerminalHandle(first, ModelTerminalKey{"missing"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        PartElectricalModelBuilder::InternalNodeHandle(first, ModelInternalNodeKey{"missing"}),
        std::invalid_argument);
    first.add<ResistanceElement>(ModelElementKey{"ax"}, a, x, parameter);
    first.add<ResistanceElement>(ModelElementKey{"xb"}, x, b, parameter);
    CHECK(first.build().elements().size() == 2U);
}

TEST_CASE("Passive builder rejects duplicate declarations and unchanged endpoints at mutation") {
    PartElectricalModelBuilder builder{component()};
    const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    CHECK_THROWS_AS(builder.terminal(ModelTerminalKey{"a"}, PinKey{"B"}), std::invalid_argument);
    CHECK_THROWS_AS(builder.terminal(ModelTerminalKey{"another"}, PinKey{"A"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(builder.terminal(ModelTerminalKey{"foreign"}, PinKey{"missing"}),
                    std::invalid_argument);
    const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
    const auto x = builder.internal_node(ModelInternalNodeKey{"x"});
    CHECK_THROWS_AS(builder.internal_node(ModelInternalNodeKey{"x"}), std::invalid_argument);
    const auto parameter = ModelParameter{Quantity{UnitDimension::Resistance, 1.0}};
    CHECK_THROWS_AS(builder.add<ResistanceElement>(ModelElementKey{"same"}, a, a, parameter),
                    std::invalid_argument);
    CHECK_THROWS_AS(builder.add<ResistanceElement>(ModelElementKey{"same"}, x, x, parameter),
                    std::invalid_argument);
    builder.add<ResistanceElement>(ModelElementKey{"body"}, a, b, parameter);
    CHECK_THROWS_AS(
        builder.add<CapacitanceElement>(ModelElementKey{"body"}, a, b,
                                        ModelParameter{Quantity{UnitDimension::Capacitance, 1.0}}),
        std::invalid_argument);
}

TEST_CASE("Passive portable construction rejects missing duplicate foreign and unused content") {
    const auto definition = component();
    SECTION("At least one element is required") {
        CHECK_THROWS_AS(PartElectricalModel(definition, terminals(), {}, {}),
                        std::invalid_argument);
    }
    SECTION("Every logical pin needs exactly one model terminal") {
        CHECK_THROWS_AS(PartElectricalModel(definition, {{ModelTerminalKey{"a"}, PinKey{"A"}}}, {},
                                            {resistor()}),
                        std::invalid_argument);
        CHECK_THROWS_AS(PartElectricalModel(definition,
                                            {{ModelTerminalKey{"a"}, PinKey{"A"}},
                                             {ModelTerminalKey{"b"}, PinKey{"A"}}},
                                            {}, {resistor()}),
                        std::invalid_argument);
        CHECK_THROWS_AS(PartElectricalModel(definition,
                                            {{ModelTerminalKey{"a"}, PinKey{"A"}},
                                             {ModelTerminalKey{"b"}, PinKey{"missing"}}},
                                            {}, {resistor()}),
                        std::invalid_argument);
    }
    SECTION("All typed collections reject duplicate keys") {
        CHECK_THROWS_AS(PartElectricalModel(definition,
                                            {{ModelTerminalKey{"a"}, PinKey{"A"}},
                                             {ModelTerminalKey{"a"}, PinKey{"B"}}},
                                            {}, {resistor()}),
                        std::invalid_argument);
        CHECK_THROWS_AS(PartElectricalModel(definition, terminals(),
                                            {ModelInternalNode{ModelInternalNodeKey{"x"}},
                                             ModelInternalNode{ModelInternalNodeKey{"x"}}},
                                            {resistor()}),
                        std::invalid_argument);
        CHECK_THROWS_AS(PartElectricalModel(definition, terminals(), {}, {resistor(), resistor()}),
                        std::invalid_argument);
    }
    SECTION("An endpoint must resolve to a declared key of the same type") {
        CHECK_THROWS_AS(PartElectricalModel(
                            definition, terminals(), {},
                            {resistor("r", ModelTerminalKey{"a"}, ModelTerminalKey{"missing"})}),
                        std::invalid_argument);
        CHECK_THROWS_AS(
            PartElectricalModel(definition, terminals(), {},
                                {resistor("r", ModelTerminalKey{"a"}, ModelInternalNodeKey{"b"})}),
            std::invalid_argument);
    }
    SECTION("Every terminal and private node must be incident on an element") {
        CHECK_THROWS_AS(PartElectricalModel(definition, terminals(),
                                            {ModelInternalNode{ModelInternalNodeKey{"x"}}},
                                            {resistor()}),
                        std::invalid_argument);
        CHECK_THROWS_AS(PartElectricalModel(component({"A", "B", "COM"}),
                                            {{ModelTerminalKey{"a"}, PinKey{"A"}},
                                             {ModelTerminalKey{"b"}, PinKey{"B"}},
                                             {ModelTerminalKey{"common"}, PinKey{"COM"}}},
                                            {}, {resistor()}),
                        std::invalid_argument);
    }
}

TEST_CASE("Passive builder finalization checks complete terminal coverage and use") {
    PartElectricalModelBuilder builder{component()};
    const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    const auto x = builder.internal_node(ModelInternalNodeKey{"x"});
    const auto parameter = ModelParameter{Quantity{UnitDimension::Resistance, 1.0}};
    builder.add<ResistanceElement>(ModelElementKey{"ax"}, a, x, parameter);
    CHECK_THROWS_AS(builder.build(), std::invalid_argument);
    const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
    CHECK_THROWS_AS(builder.build(), std::invalid_argument);
    builder.add<ResistanceElement>(ModelElementKey{"xb"}, x, b, parameter);
    CHECK_NOTHROW(builder.build());
    static_cast<void>(builder.internal_node(ModelInternalNodeKey{"unused"}));
    CHECK_THROWS_AS(builder.build(), std::invalid_argument);
}

TEST_CASE("Passive typed key collections may share spellings without joining nodes") {
    PartElectricalModelBuilder builder{component()};
    const auto a = builder.terminal(ModelTerminalKey{"shared"}, PinKey{"A"});
    const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
    const auto x = builder.internal_node(ModelInternalNodeKey{"shared"});
    const auto parameter = ModelParameter{Quantity{UnitDimension::Resistance, 1.0}};
    builder.add<ResistanceElement>(ModelElementKey{"shared"}, a, x, parameter);
    builder.add<ResistanceElement>(ModelElementKey{"xb"}, x, b, parameter);
    const auto model = builder.build();
    const auto &first = std::get<ResistanceElement>(model.elements().front());
    CHECK(first.from() != first.to());
    CHECK(std::get<ModelTerminalKey>(first.from()).value() ==
          std::get<ModelInternalNodeKey>(first.to()).value());
}

TEST_CASE(
    "Passive portable collections canonicalize declaration order while preserving orientation") {
    const auto definition = component();
    auto pins = terminals();
    std::ranges::reverse(pins);
    const auto model =
        PartElectricalModel{definition,
                            std::move(pins),
                            {ModelInternalNode{ModelInternalNodeKey{"y"}},
                             ModelInternalNode{ModelInternalNodeKey{"x"}}},
                            {resistor("z", ModelInternalNodeKey{"y"}, ModelTerminalKey{"b"}),
                             resistor("m", ModelInternalNodeKey{"x"}, ModelInternalNodeKey{"y"}),
                             resistor("a", ModelTerminalKey{"a"}, ModelInternalNodeKey{"x"})}};
    CHECK(model.terminals().front().key() == ModelTerminalKey{"a"});
    CHECK(model.internal_nodes().front().key() == ModelInternalNodeKey{"x"});
    const auto &first = std::get<ResistanceElement>(model.elements().front());
    CHECK(first.key() == ModelElementKey{"a"});
    CHECK(first.from() == ModelEndpoint{ModelTerminalKey{"a"}});
    CHECK(first.to() == ModelEndpoint{ModelInternalNodeKey{"x"}});
}

TEST_CASE("Passive finalized models and copied values outlive builders and source components") {
    const auto model = [] {
        PartElectricalModelBuilder builder{component()};
        const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
        const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
        builder.add<ResistanceElement>(ModelElementKey{"body"}, a, b,
                                       ModelParameter{Quantity{UnitDimension::Resistance, 330.0},
                                                      Tolerance::percent(0.01),
                                                      {evidence('a')}});
        const auto finalized = builder.build();
        builder.add<CapacitanceElement>(
            ModelElementKey{"later"}, a, b,
            ModelParameter{Quantity{UnitDimension::Capacitance, 1.0e-9}});
        CHECK(builder.build().elements().size() == 2U);
        CHECK(finalized.elements().size() == 1U);
        return finalized;
    }();
    auto copy = model;
    CHECK(copy.implemented_component() == component().content_identity());
    REQUIRE(copy.elements().size() == 1U);
    const auto &resistance = std::get<ResistanceElement>(copy.elements().front());
    CHECK(resistance.parameter().nominal() == Quantity{UnitDimension::Resistance, 330.0});
    CHECK(resistance.parameter().evidence() == std::vector{evidence('a')});
    copy = PartElectricalModel{component(), terminals(), {}, {resistor("replacement")}};
    CHECK(std::get<ResistanceElement>(model.elements().front()).key() == ModelElementKey{"body"});
}

TEST_CASE("Passive handles surviving a destroyed builder never belong to a subsequent builder") {
    const auto stale = [] {
        PartElectricalModelBuilder builder{component()};
        return builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    }();
    PartElectricalModelBuilder next{component()};
    static_cast<void>(next.terminal(ModelTerminalKey{"a"}, PinKey{"A"}));
    const auto b = next.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
    CHECK_THROWS_AS(
        next.add<ResistanceElement>(ModelElementKey{"body"}, stale, b,
                                    ModelParameter{Quantity{UnitDimension::Resistance, 1.0}}),
        std::invalid_argument);
}

TEST_CASE("Passive handle ownership survives builder storage address reuse") {
    std::optional<PartElectricalModelBuilder> storage{std::in_place, component()};
    const auto *address = &*storage;
    const auto stale = storage->terminal(ModelTerminalKey{"a"}, PinKey{"A"});
    storage.reset();
    storage.emplace(component());
    REQUIRE(&*storage == address);
    static_cast<void>(storage->terminal(ModelTerminalKey{"a"}, PinKey{"A"}));
    const auto b = storage->terminal(ModelTerminalKey{"b"}, PinKey{"B"});
    CHECK_FALSE(stale.belongs_to(*storage));
    CHECK_THROWS_AS(
        storage->add<ResistanceElement>(ModelElementKey{"body"}, stale, b,
                                        ModelParameter{Quantity{UnitDimension::Resistance, 1.0}}),
        std::invalid_argument);
}
