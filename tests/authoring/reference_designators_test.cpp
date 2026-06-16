#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include <volt/authoring/component_library.hpp>
#include <volt/authoring/reference_designators.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/instances.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/properties.hpp>

namespace {

volt::ComponentDefId add_resistor_definition(volt::Circuit &circuit) {
    return volt::authoring::define_component(circuit, volt::authoring::resistor());
}

} // namespace

TEST_CASE("Reference allocation returns the first available prefix number") {
    auto circuit = volt::Circuit{};

    CHECK(volt::authoring::allocate_reference(circuit, "R") == volt::ReferenceDesignator{"R1"});
}

TEST_CASE("Reference allocation is deterministic and skips existing references") {
    auto circuit = volt::Circuit{};
    const auto resistor = add_resistor_definition(circuit);

    [[maybe_unused]] const auto r1 =
        circuit.instantiate_component(resistor, volt::ReferenceDesignator{"R1"});
    [[maybe_unused]] const auto r3 =
        circuit.instantiate_component(resistor, volt::ReferenceDesignator{"R3"});

    CHECK(volt::authoring::allocate_reference(circuit, "R") == volt::ReferenceDesignator{"R2"});
}

TEST_CASE("Reference allocation rejects empty prefixes") {
    auto circuit = volt::Circuit{};

    CHECK_THROWS_AS(volt::authoring::allocate_reference(circuit, ""), std::invalid_argument);
}

TEST_CASE("Authoring instantiate helper preserves explicit references") {
    auto circuit = volt::Circuit{};
    const auto resistor = add_resistor_definition(circuit);

    const auto r10 =
        volt::authoring::instantiate(circuit, resistor, volt::ReferenceDesignator{"R10"});

    CHECK(circuit.component(r10).reference() == volt::ReferenceDesignator{"R10"});
    CHECK(volt::queries::pins_for(circuit, r10).size() == 2);
    CHECK_THROWS_AS(
        volt::authoring::instantiate(circuit, resistor, volt::ReferenceDesignator{"R10"}),
        std::logic_error);
}

TEST_CASE("Authoring instantiate helper allocates deterministic unique references") {
    auto circuit = volt::Circuit{};
    const auto resistor = add_resistor_definition(circuit);

    const auto r1 = volt::authoring::instantiate(circuit, resistor, "R");
    const auto r2 = volt::authoring::instantiate(
        circuit, resistor, "R",
        volt::PropertyMap{{volt::PropertyKey{"value"}, volt::PropertyValue{"10k"}}});

    CHECK(circuit.component(r1).reference() == volt::ReferenceDesignator{"R1"});
    CHECK(circuit.component(r2).reference() == volt::ReferenceDesignator{"R2"});
    CHECK(circuit.component(r2).properties().get(volt::PropertyKey{"value"}).as_string() == "10k");
    CHECK(volt::queries::pins_for(circuit, r1).size() == 2);
    CHECK(volt::queries::pins_for(circuit, r2).size() == 2);
}
