#include <catch2/catch_test_macros.hpp>

#include <string>

#include <volt/core/version.hpp>

TEST_CASE("Volt exposes the configured project version") {
    CHECK(std::string{volt::version_string()} == "0.1.0");
}
