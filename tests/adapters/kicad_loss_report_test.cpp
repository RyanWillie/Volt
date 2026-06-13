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
    CHECK(report.warnings().at(2).fabrication_impact ==
          volt::adapters::kicad::LossFabricationImpact::Informational);
    CHECK_FALSE(report.has_fab_critical_warnings());
}

TEST_CASE("KiCad LossReport is empty by default") {
    volt::adapters::kicad::LossReport report;

    CHECK_FALSE(report.has_warnings());
    CHECK_FALSE(report.has_fab_critical_warnings());
    CHECK(report.warnings().empty());
}

TEST_CASE("KiCad LossReport separates fab-critical and informational losses") {
    volt::adapters::kicad::LossReport report;

    report.add_warning(volt::adapters::kicad::LossKind::UnsupportedConstruct, "board.zone",
                       "Copper zones are not exported",
                       volt::adapters::kicad::LossSeverity::Warning,
                       volt::adapters::kicad::LossFabricationImpact::FabCritical);
    report.add_warning(volt::adapters::kicad::LossKind::UnsupportedConstruct, "board.text.layer",
                       "Annotation text is on an unmapped layer",
                       volt::adapters::kicad::LossSeverity::Info,
                       volt::adapters::kicad::LossFabricationImpact::Informational);

    REQUIRE(report.has_fab_critical_warnings());
    REQUIRE(report.fab_critical_warnings().size() == 1);
    CHECK(report.fab_critical_warnings().front().construct == "board.zone");
    CHECK(report.warnings().at(1).fabrication_impact ==
          volt::adapters::kicad::LossFabricationImpact::Informational);
}

TEST_CASE("KiCad fab-critical loss diagnostics use stable manufacturing codes") {
    volt::adapters::kicad::LossReport report;
    report.add_warning(volt::adapters::kicad::LossKind::UnsupportedConstruct, "board.zone",
                       "Copper zones are not exported",
                       volt::adapters::kicad::LossSeverity::Warning,
                       volt::adapters::kicad::LossFabricationImpact::FabCritical);
    report.add_warning(volt::adapters::kicad::LossKind::UnsupportedConstruct, "board.text.layer",
                       "Annotation text is on an unmapped layer",
                       volt::adapters::kicad::LossSeverity::Info,
                       volt::adapters::kicad::LossFabricationImpact::Informational);

    const auto diagnostics = volt::adapters::kicad::fabrication_diagnostics(report);

    REQUIRE(diagnostics.count() == 1);
    const auto &diagnostic = diagnostics.diagnostics().front();
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.category() ==
          volt::DiagnosticCategory{volt::diagnostic_categories::PcbFabrication});
    CHECK(diagnostic.code() == volt::DiagnosticCode{std::string{
                                   volt::pcb_fabrication_diagnostic_codes::KiCadFabExportLoss}});
    CHECK(diagnostic.message().find("board.zone") != std::string::npos);
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::board()});
    REQUIRE(diagnostic.rule().has_value());
    CHECK(diagnostic.rule().value() == "board.zone");
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
