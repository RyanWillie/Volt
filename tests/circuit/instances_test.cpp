#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include <volt/circuit/instances.hpp>
#include <volt/core/ids.hpp>

TEST_CASE("ReferenceDesignator stores a non-empty component reference") {
    const auto reference = volt::ReferenceDesignator{"R1"};

    CHECK(reference.value() == "R1");
    CHECK(reference == volt::ReferenceDesignator{"R1"});
}

TEST_CASE("ReferenceDesignator rejects empty values") {
    CHECK_THROWS_AS(volt::ReferenceDesignator{""}, std::invalid_argument);
}

TEST_CASE("ComponentInstance stores reusable definition and reference designator") {
    const auto component = volt::ComponentInstance{
        volt::ComponentDefId{3},
        volt::ReferenceDesignator{"U1"},
    };

    CHECK(component.definition() == volt::ComponentDefId{3});
    CHECK(component.reference() == volt::ReferenceDesignator{"U1"});
}

TEST_CASE("PinInstance stores owning component and reusable pin definition") {
    const auto pin = volt::PinInstance{
        volt::ComponentId{7},
        volt::PinDefId{2},
    };

    CHECK(pin.component() == volt::ComponentId{7});
    CHECK(pin.definition() == volt::PinDefId{2});
}
