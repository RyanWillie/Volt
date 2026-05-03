#include <catch2/catch_test_macros.hpp>

#include <volt/core/version.hpp>

TEST_CASE("Volt exposes the configured project version") {
    CHECK(volt::version_string() == "0.1.0");
}
