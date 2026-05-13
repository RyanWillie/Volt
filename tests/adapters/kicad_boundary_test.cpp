#include <catch2/catch_test_macros.hpp>

#include <volt/volt.hpp>

// loss_report.hpp uses a named #ifndef guard (not #pragma once) so that this test can check the
// macro to verify volt/volt.hpp has not transitively included the KiCad adapter header.
TEST_CASE("Volt umbrella header does not include KiCad adapter types") {
#ifdef VOLT_KICAD_ADAPTER_LOSS_REPORT_HPP
    FAIL("volt/volt.hpp must not include KiCad adapter headers");
#else
    SUCCEED("KiCad adapter headers stay outside the Volt core umbrella");
#endif
}
