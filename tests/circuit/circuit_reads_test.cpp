#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <cstddef>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>

#include <support/circuit_test_helpers.hpp>

namespace {

template <typename Id, typename Entity>
constexpr bool get_returns =
    std::same_as<decltype(std::declval<const volt::Circuit &>().get(std::declval<Id>())),
                 const Entity &>;

static_assert(get_returns<volt::PinDefId, volt::PinDefinition>);
static_assert(get_returns<volt::ComponentDefId, volt::ComponentDefinition>);
static_assert(get_returns<volt::ComponentId, volt::ComponentInstance>);
static_assert(get_returns<volt::PinId, volt::PinInstance>);
static_assert(get_returns<volt::NetId, volt::Net>);
static_assert(get_returns<volt::ModuleDefId, volt::ModuleDefinition>);
static_assert(get_returns<volt::TemplateNetDefId, volt::TemplateNetDefinition>);
static_assert(get_returns<volt::PortDefId, volt::PortDefinition>);
static_assert(get_returns<volt::ModuleComponentId, volt::ModuleComponentTemplate>);
static_assert(get_returns<volt::ModuleInstanceId, volt::ModuleInstance>);
static_assert(get_returns<volt::PortBindingId, volt::PortBinding>);
static_assert(get_returns<volt::NetClassId, volt::NetClass>);

template <typename Circuit>
concept HasRvalueAll = requires(Circuit &&circuit) {
    std::forward<Circuit>(circuit).template all<volt::ComponentId>();
};

template <typename Circuit>
concept ExposesConnectivityModel =
    requires(const Circuit &circuit) { circuit.connectivity_model(); };

template <typename Circuit>
concept ExposesHierarchyModel = requires(const Circuit &circuit) { circuit.hierarchy_model(); };

template <typename Id>
concept SupportsCircuitRead = requires(const volt::Circuit &circuit, Id id) {
    circuit.get(id);
    circuit.template all<Id>();
};

struct UserDefinedCircuitId {
    using type = volt::ComponentInstance;
    [[nodiscard]] static const type &get(const volt::Circuit &circuit, UserDefinedCircuitId id);
    [[nodiscard]] static std::size_t size(const volt::Circuit &circuit) noexcept;
};

static_assert(!HasRvalueAll<volt::Circuit>);
static_assert(!ExposesConnectivityModel<volt::Circuit>);
static_assert(!ExposesHierarchyModel<volt::Circuit>);
static_assert(!SupportsCircuitRead<volt::SymbolDefId>);
static_assert(!volt::CircuitEntityId<UserDefinedCircuitId>);
static_assert(!SupportsCircuitRead<UserDefinedCircuitId>);
static_assert(std::ranges::forward_range<volt::entity_range_t<volt::ComponentId>>);
static_assert(std::ranges::sized_range<volt::entity_range_t<volt::ComponentId>>);
static_assert(std::same_as<std::ranges::range_reference_t<volt::entity_range_t<volt::ComponentId>>,
                           const volt::ComponentInstance &>);
static_assert(!std::is_constructible_v<volt::entity_range_t<volt::ComponentId>::iterator,
                                       volt::Circuit &&, std::size_t>);

struct ReadFixture {
    volt::Circuit circuit;
    volt::PinDefId first_pin_definition;
    volt::PinDefId second_pin_definition;
    volt::ComponentDefId component_definition;
    volt::ComponentId component;
    volt::PinId first_pin;
    volt::NetId parent_net;
    volt::ModuleDefId module_definition;
    volt::TemplateNetDefId template_net;
    volt::PortDefId port_definition;
    volt::ModuleComponentId module_component;
    volt::ModuleInstanceId module_instance;
    volt::PortBindingId port_binding;
    volt::NetClassId net_class;
};

ReadFixture make_read_fixture() {
    auto circuit = volt::Circuit{};
    const auto component_definition = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("A", "1"), volt::test::passive_pin("B", "2")});
    const auto &pins = circuit.get(component_definition).pins();
    const auto first_pin_definition = pins[0];
    const auto second_pin_definition = pins[1];
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"R1"});
    const auto first_pin =
        volt::queries::pin_by_definition(circuit, component, first_pin_definition).value();
    const auto parent_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"PARENT"}, volt::NetKind::Signal});

    const auto module_definition = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"Channel"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"SIGNAL"},
                                                      volt::NetKind::Signal}},
        .components = {volt::ModuleComponentTemplate{component_definition,
                                                     volt::ReferenceDesignator{"R2"}}},
        .connections = {volt::ModulePinConnectionSpec{
            volt::NetName{"SIGNAL"}, volt::ReferenceDesignator{"R2"}, first_pin_definition}},
        .ports = {volt::ModulePortSpec{volt::PortName{"SIGNAL"}, volt::NetName{"SIGNAL"},
                                       volt::PortRole::Passive}},
    });
    const auto template_net = circuit.get(module_definition).template_nets().front();
    const auto port_definition = circuit.get(module_definition).ports().front();
    const auto module_component = circuit.get(module_definition).components().front();
    const auto module_instance =
        circuit.instantiate_root_module(module_definition, volt::ModuleInstanceName{"CHANNEL_A"});
    const auto port_binding = circuit.bind_port(module_instance, port_definition, parent_net);
    const auto net_class = circuit.define_net_class(
        volt::NetClassSpec{.net_class = volt::NetClass{volt::NetClassName{"Default"}}});

    return ReadFixture{std::move(circuit),
                       first_pin_definition,
                       second_pin_definition,
                       component_definition,
                       component,
                       first_pin,
                       parent_net,
                       module_definition,
                       template_net,
                       port_definition,
                       module_component,
                       module_instance,
                       port_binding,
                       net_class};
}

template <typename Id> void check_unknown_id(const volt::Circuit &circuit) {
    try {
        static_cast<void>(circuit.get(Id{999}));
        FAIL("Unknown ID must throw");
    } catch (const volt::KernelRangeError &error) {
        CHECK(error.code() == volt::ErrorCode::UnknownEntity);
        CHECK(std::string{error.what()} == "Volt entity id is out of range");
    }
}

} // namespace

TEST_CASE("Circuit generic reads cover every canonical stable-ID entity family") {
    const auto fixture = make_read_fixture();
    const auto &circuit = fixture.circuit;

    CHECK(circuit.get(fixture.first_pin_definition).name() == "A");
    CHECK(circuit.get(fixture.component_definition).name() == "Resistor");
    CHECK(circuit.get(fixture.component).reference() == volt::ReferenceDesignator{"R1"});
    CHECK(circuit.get(fixture.first_pin).definition() == fixture.first_pin_definition);
    CHECK(circuit.get(fixture.parent_net).name() == volt::NetName{"PARENT"});
    CHECK(circuit.get(fixture.module_definition).name() == volt::ModuleName{"Channel"});
    CHECK(circuit.get(fixture.template_net).name() == volt::NetName{"SIGNAL"});
    CHECK(circuit.get(fixture.port_definition).name() == volt::PortName{"SIGNAL"});
    CHECK(circuit.get(fixture.module_component).reference() == volt::ReferenceDesignator{"R2"});
    CHECK(circuit.get(fixture.module_instance).name() == volt::ModuleInstanceName{"CHANNEL_A"});
    CHECK(circuit.get(fixture.port_binding).port() == fixture.port_definition);
    CHECK(circuit.get(fixture.net_class).name() == volt::NetClassName{"Default"});

    CHECK(circuit.all<volt::PinDefId>().size() == 2);
    CHECK(circuit.all<volt::ComponentDefId>().size() == 1);
    CHECK(circuit.all<volt::ComponentId>().size() == 2);
    CHECK(circuit.all<volt::PinId>().size() == 4);
    CHECK(circuit.all<volt::NetId>().size() == 2);
    CHECK(circuit.all<volt::ModuleDefId>().size() == 1);
    CHECK(circuit.all<volt::TemplateNetDefId>().size() == 1);
    CHECK(circuit.all<volt::PortDefId>().size() == 1);
    CHECK(circuit.all<volt::ModuleComponentId>().size() == 1);
    CHECK(circuit.all<volt::ModuleInstanceId>().size() == 1);
    CHECK(circuit.all<volt::PortBindingId>().size() == 1);
    CHECK(circuit.all<volt::NetClassId>().size() == 1);
}

TEST_CASE("Circuit entity ranges borrow deterministic insertion-ordered entities") {
    const auto fixture = make_read_fixture();
    const auto &circuit = fixture.circuit;
    const auto pins = circuit.all<volt::PinDefId>();

    std::vector<std::string> names;
    for (const auto &pin : pins) {
        names.push_back(pin.name());
    }

    CHECK(names == std::vector<std::string>{"A", "B"});
    CHECK(&*pins.begin() == &circuit.get(volt::PinDefId{0}));
}

TEST_CASE("Circuit generic get preserves unknown and foreign ID error contracts") {
    const auto fixture = make_read_fixture();
    const auto &circuit = fixture.circuit;

    check_unknown_id<volt::PinDefId>(circuit);
    check_unknown_id<volt::ComponentDefId>(circuit);
    check_unknown_id<volt::ComponentId>(circuit);
    check_unknown_id<volt::PinId>(circuit);
    check_unknown_id<volt::NetId>(circuit);
    check_unknown_id<volt::ModuleDefId>(circuit);
    check_unknown_id<volt::TemplateNetDefId>(circuit);
    check_unknown_id<volt::PortDefId>(circuit);
    check_unknown_id<volt::ModuleComponentId>(circuit);
    check_unknown_id<volt::ModuleInstanceId>(circuit);
    check_unknown_id<volt::PortBindingId>(circuit);
    check_unknown_id<volt::NetClassId>(circuit);

    auto owner = volt::Circuit{};
    static_cast<void>(owner.add_net(volt::NetSpec{volt::NetName{"FIRST"}, volt::NetKind::Signal}));
    const auto foreign =
        owner.add_net(volt::NetSpec{volt::NetName{"SECOND"}, volt::NetKind::Signal});
    const auto other = volt::Circuit{};
    try {
        static_cast<void>(other.get(foreign));
        FAIL("Foreign ID must throw");
    } catch (const volt::KernelRangeError &error) {
        CHECK(std::string{error.what()} == "Volt entity id is out of range");
    }
}

TEST_CASE("Circuit net_of distinguishes invalid pins from valid disconnected pins") {
    auto fixture = make_read_fixture();

    CHECK_FALSE(fixture.circuit.net_of(fixture.first_pin).has_value());
    CHECK(fixture.circuit.connect(fixture.parent_net, fixture.first_pin));
    CHECK(fixture.circuit.net_of(fixture.first_pin) == fixture.parent_net);
    CHECK(volt::queries::net_of(fixture.circuit, fixture.first_pin) == fixture.parent_net);

    try {
        static_cast<void>(fixture.circuit.net_of(volt::PinId{999}));
        FAIL("Unknown pin ID must throw");
    } catch (const volt::KernelRangeError &error) {
        CHECK(error.code() == volt::ErrorCode::UnknownEntity);
        CHECK(std::string{error.what()} == "Pin ID does not belong to this circuit");
    }
}
