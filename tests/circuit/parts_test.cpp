#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include <volt/circuit/parts.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

TEST_CASE("ManufacturerPart stores selected manufacturer identity") {
    const auto part = volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"};

    CHECK(part.manufacturer() == "Yageo");
    CHECK(part.part_number() == "RC0603FR-07330RL");
}

TEST_CASE("ManufacturerPart rejects empty structural labels") {
    CHECK_THROWS_AS(volt::ManufacturerPart("", "RC0603FR-07330RL"), std::invalid_argument);
    CHECK_THROWS_AS(volt::ManufacturerPart("Yageo", ""), std::invalid_argument);
}

TEST_CASE("PackageRef stores a selected physical package label") {
    const auto package = volt::PackageRef{"0603"};

    CHECK(package.value() == "0603");
}

TEST_CASE("PackageRef rejects empty labels") {
    CHECK_THROWS_AS(volt::PackageRef{""}, std::invalid_argument);
}

TEST_CASE("FootprintRef stores a library-qualified footprint reference") {
    const auto footprint = volt::FootprintRef{"passives", "R_0603_1608Metric"};

    CHECK(footprint.library() == "passives");
    CHECK(footprint.name() == "R_0603_1608Metric");
}

TEST_CASE("FootprintRef rejects empty structural labels") {
    CHECK_THROWS_AS(volt::FootprintRef("", "R_0603_1608Metric"), std::invalid_argument);
    CHECK_THROWS_AS(volt::FootprintRef("passives", ""), std::invalid_argument);
}

TEST_CASE("PinPadMapping stores the logical-pin to physical-pad relationship") {
    const auto mapping = volt::PinPadMapping{volt::PinDefId{1}, "2"};

    CHECK(mapping.pin() == volt::PinDefId{1});
    CHECK(mapping.pad() == "2");
}

TEST_CASE("PinPadMapping rejects empty pad labels") {
    CHECK_THROWS_AS(volt::PinPadMapping(volt::PinDefId{0}, ""), std::invalid_argument);
}

TEST_CASE("PhysicalPart stores manufacturer, package, footprint, mappings, and properties") {
    const auto physical_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::PinPadMapping{volt::PinDefId{0}, "1"},
            volt::PinPadMapping{volt::PinDefId{1}, "2"},
        },
        volt::PropertyMap{
            {volt::PropertyKey{"tolerance"}, volt::PropertyValue{"1%"}},
            {volt::PropertyKey{"voltage_rating"}, volt::PropertyValue{"75 V"}},
        },
    };

    CHECK(physical_part.manufacturer_part().manufacturer() == "Yageo");
    CHECK(physical_part.manufacturer_part().part_number() == "RC0603FR-07330RL");
    CHECK(physical_part.package().value() == "0603");
    CHECK(physical_part.footprint().library() == "passives");
    CHECK(physical_part.footprint().name() == "R_0603_1608Metric");
    REQUIRE(physical_part.pin_pad_mappings().size() == 2);
    CHECK(physical_part.pin_pad_mappings()[0].pin() == volt::PinDefId{0});
    CHECK(physical_part.pin_pad_mappings()[0].pad() == "1");
    CHECK(physical_part.pin_pad_mappings()[1].pin() == volt::PinDefId{1});
    CHECK(physical_part.pin_pad_mappings()[1].pad() == "2");
    CHECK(physical_part.properties().get(volt::PropertyKey{"tolerance"}) ==
          volt::PropertyValue{"1%"});
    CHECK(physical_part.properties().get(volt::PropertyKey{"voltage_rating"}) ==
          volt::PropertyValue{"75 V"});
}

TEST_CASE("PhysicalPart defaults to empty properties") {
    const auto physical_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Lite-On", "LTST-C190KRKT"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"leds", "LED_0603_1608Metric"},
        std::vector{
            volt::PinPadMapping{volt::PinDefId{0}, "A"},
            volt::PinPadMapping{volt::PinDefId{1}, "K"},
        },
    };

    CHECK(physical_part.properties().empty());
    CHECK(physical_part.electrical_attributes().empty());
}

TEST_CASE("PhysicalPart rejects empty and ambiguous pin-pad mappings") {
    CHECK_THROWS_AS(volt::PhysicalPart(volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                       volt::PackageRef{"0603"},
                                       volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                       std::vector<volt::PinPadMapping>{}),
                    std::invalid_argument);

    CHECK_THROWS_AS(volt::PhysicalPart(volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                       volt::PackageRef{"0603"},
                                       volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                       std::vector{
                                           volt::PinPadMapping{volt::PinDefId{0}, "1"},
                                           volt::PinPadMapping{volt::PinDefId{0}, "2"},
                                       }),
                    std::invalid_argument);

    CHECK_THROWS_AS(volt::PhysicalPart(volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                       volt::PackageRef{"0603"},
                                       volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                       std::vector{
                                           volt::PinPadMapping{volt::PinDefId{0}, "1"},
                                           volt::PinPadMapping{volt::PinDefId{1}, "1"},
                                       }),
                    std::invalid_argument);
}
