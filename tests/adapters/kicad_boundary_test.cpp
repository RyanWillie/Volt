#include <catch2/catch_test_macros.hpp>

#include <volt/volt.hpp>

TEST_CASE("Volt umbrella header does not include KiCad adapter types") {
#ifdef VOLT_KICAD_ADAPTER_LOSS_REPORT_HPP
    FAIL("volt/volt.hpp must not include KiCad adapter headers");
#else
    SUCCEED("KiCad adapter headers stay outside the Volt core umbrella");
#endif
}
