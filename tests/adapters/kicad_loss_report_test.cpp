#include <catch2/catch_test_macros.hpp>

#include <volt/adapters/kicad/loss_report.hpp>

TEST_CASE("KiCad adapter reports structured unsupported incomplete and lossy warnings") {
    volt::adapters::kicad::LossReport report;

    report.add_warning(volt::adapters::kicad::LossKind::UnsupportedConstruct, "no_connect",
                       "No-connect markers need an explicit Volt design-intent primitive");
    report.add_warning(volt::adapters::kicad::LossKind::IncompleteConstruct, "footprint",
                       "Footprint-only assignments are preserved as provenance");
    report.add_warning(volt::adapters::kicad::LossKind::LossyConstruct, "stroke",
                       "KiCad stroke style was approximated by Volt schematic geometry",
                       volt::adapters::kicad::LossSeverity::Warning);

    REQUIRE(report.has_warnings());
    REQUIRE(report.warnings().size() == 3);

    CHECK(report.warnings().at(0).kind == volt::adapters::kicad::LossKind::UnsupportedConstruct);
    CHECK(report.warnings().at(0).construct == "no_connect");
    CHECK(report.warnings().at(0).message ==
          "No-connect markers need an explicit Volt design-intent primitive");

    CHECK(report.warnings().at(1).kind == volt::adapters::kicad::LossKind::IncompleteConstruct);
    CHECK(report.warnings().at(1).construct == "footprint");

    CHECK(report.warnings().at(2).kind == volt::adapters::kicad::LossKind::LossyConstruct);
    CHECK(report.warnings().at(2).severity == volt::adapters::kicad::LossSeverity::Warning);
}

TEST_CASE("KiCad LossReport is empty by default") {
    volt::adapters::kicad::LossReport report;

    CHECK_FALSE(report.has_warnings());
    CHECK(report.warnings().empty());
}

TEST_CASE("KiCad LossReport rejects empty construct or message") {
    volt::adapters::kicad::LossReport report;

    CHECK_THROWS_AS(report.add_warning(volt::adapters::kicad::LossKind::UnsupportedConstruct, "",
                                       "some message"),
                    std::invalid_argument);

    CHECK_THROWS_AS(
        report.add_warning(volt::adapters::kicad::LossKind::UnsupportedConstruct, "construct", ""),
        std::invalid_argument);

    CHECK_FALSE(report.has_warnings());
}
