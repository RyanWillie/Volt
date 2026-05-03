#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include <volt/core/diagnostics.hpp>
#include <volt/core/ids.hpp>

TEST_CASE("DiagnosticCode rejects empty values") {
    CHECK_THROWS_AS(volt::DiagnosticCode{""}, std::invalid_argument);
}

TEST_CASE("DiagnosticCode stores comparable machine-readable values") {
    const auto code = volt::DiagnosticCode{"UNCONNECTED_PIN"};

    CHECK(code.value() == "UNCONNECTED_PIN");
    CHECK(code == volt::DiagnosticCode{"UNCONNECTED_PIN"});
    CHECK(code < volt::DiagnosticCode{"Z_LATER_CODE"});
}

TEST_CASE("EntityRef preserves logical entity kind and index") {
    const auto component_def = volt::EntityRef::component_def(volt::ComponentDefId{1});
    const auto component = volt::EntityRef::component(volt::ComponentId{2});
    const auto pin_def = volt::EntityRef::pin_def(volt::PinDefId{3});
    const auto pin = volt::EntityRef::pin(volt::PinId{4});
    const auto net = volt::EntityRef::net(volt::NetId{5});

    CHECK(component_def.kind() == volt::EntityKind::ComponentDef);
    CHECK(component_def.index() == 1);

    CHECK(component.kind() == volt::EntityKind::Component);
    CHECK(component.index() == 2);

    CHECK(pin_def.kind() == volt::EntityKind::PinDef);
    CHECK(pin_def.index() == 3);

    CHECK(pin.kind() == volt::EntityKind::Pin);
    CHECK(pin.index() == 4);

    CHECK(net.kind() == volt::EntityKind::Net);
    CHECK(net.index() == 5);
}

TEST_CASE("Diagnostic stores severity code message and related entities") {
    const auto diagnostic = volt::Diagnostic{
        volt::Severity::Warning,
        volt::DiagnosticCode{"SINGLE_PIN_NET"},
        "Net LED_A has only one connected pin",
        std::vector{volt::EntityRef::net(volt::NetId{7})},
    };

    CHECK(diagnostic.severity() == volt::Severity::Warning);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    CHECK(diagnostic.message() == "Net LED_A has only one connected pin");
    REQUIRE(diagnostic.entities().size() == 1);
    CHECK(diagnostic.entities().front() == volt::EntityRef::net(volt::NetId{7}));
}

TEST_CASE("DiagnosticReport starts empty") {
    const volt::DiagnosticReport report;

    CHECK(report.empty());
    CHECK(report.count() == 0);
    CHECK_FALSE(report.has_errors());
}

TEST_CASE("DiagnosticReport preserves insertion order") {
    volt::DiagnosticReport report;

    report.add(volt::Diagnostic{
        volt::Severity::Info,
        volt::DiagnosticCode{"FIRST"},
        "first diagnostic",
    });
    report.add(volt::Diagnostic{
        volt::Severity::Warning,
        volt::DiagnosticCode{"SECOND"},
        "second diagnostic",
    });

    REQUIRE(report.diagnostics().size() == 2);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"FIRST"});
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"SECOND"});
}

TEST_CASE("DiagnosticReport has_errors only reports error severity") {
    volt::DiagnosticReport report;

    report.add(volt::Diagnostic{
        volt::Severity::Info,
        volt::DiagnosticCode{"NOTE"},
        "informational note",
    });
    report.add(volt::Diagnostic{
        volt::Severity::Warning,
        volt::DiagnosticCode{"WARNING"},
        "warning note",
    });

    CHECK_FALSE(report.has_errors());

    report.add(volt::Diagnostic{
        volt::Severity::Error,
        volt::DiagnosticCode{"ERROR"},
        "error note",
    });

    CHECK(report.has_errors());
}

TEST_CASE("DiagnosticReport counts diagnostics by severity") {
    volt::DiagnosticReport report;

    report.add(volt::Diagnostic{volt::Severity::Info, volt::DiagnosticCode{"I1"}, "info"});
    report.add(volt::Diagnostic{volt::Severity::Warning, volt::DiagnosticCode{"W1"}, "warning"});
    report.add(volt::Diagnostic{volt::Severity::Warning, volt::DiagnosticCode{"W2"}, "warning"});
    report.add(volt::Diagnostic{volt::Severity::Error, volt::DiagnosticCode{"E1"}, "error"});

    CHECK(report.count() == 4);
    CHECK(report.count(volt::Severity::Info) == 1);
    CHECK(report.count(volt::Severity::Warning) == 2);
    CHECK(report.count(volt::Severity::Error) == 1);
}
