#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <stdexcept>
#include <utility>

#include <volt/circuit/connectivity/instances.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace {

template <typename Model>
concept CanSetComponentProperty =
    requires(Model model, volt::PropertyKey key, volt::PropertyValue value) {
        model.set_property(std::move(key), std::move(value));
    };

static_assert(!CanSetComponentProperty<volt::ComponentInstance>);

} // namespace

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
    CHECK(component.properties().empty());
}

TEST_CASE("ComponentInstance stores explicit properties") {
    const auto component = volt::ComponentInstance{
        volt::ComponentDefId{3},
        volt::ReferenceDesignator{"R1"},
        volt::PropertyMap{
            {volt::PropertyKey{"value"}, volt::PropertyValue{"330 ohm"}},
            {volt::PropertyKey{"fitted"}, volt::PropertyValue{true}},
        },
    };

    CHECK(component.properties().size() == 2);
    CHECK(component.properties().get(volt::PropertyKey{"value"}) == volt::PropertyValue{"330 ohm"});
    CHECK(component.properties().get(volt::PropertyKey{"fitted"}) == volt::PropertyValue{true});
}

TEST_CASE("PinInstance stores owning component and reusable pin definition") {
    const auto pin = volt::PinInstance{
        volt::ComponentId{7},
        volt::PinDefId{2},
    };

    CHECK(pin.component() == volt::ComponentId{7});
    CHECK(pin.definition() == volt::PinDefId{2});
}
