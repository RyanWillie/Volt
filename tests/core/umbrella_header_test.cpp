#include <catch2/catch_test_macros.hpp>

#include <volt/volt.hpp>

TEST_CASE("Volt umbrella header exposes the public kernel surface") {
    CHECK_FALSE(volt::version_string().empty());
    CHECK(volt::io::capability_profile_format_version() == 1);
    CHECK(volt::io::logical_circuit_format_version() == 1);
    CHECK(volt::io::pcb_format_version() == 1);
}
