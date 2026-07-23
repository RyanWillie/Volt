#include <catch2/catch_test_macros.hpp>

#include <volt/volt.hpp>

TEST_CASE("Volt umbrella header exposes the public kernel surface") {
    CHECK_FALSE(volt::version_string().empty());
    CHECK(volt::io::capability_profile_format_version() == 1);
    CHECK(volt::io::logical_circuit_format_version() == 1);
    CHECK(volt::io::pcb_format_version() == 3);
    CHECK(std::string{volt::io::compiled_board_format_name()} == "volt.compiled-board");
    static_cast<void>(&volt::io::compile_board);
    static_cast<void>(&volt::io::write_compiled_board);
    static_cast<void>(&volt::io::open_compiled_board);
}
