#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include <volt/io/electrical/dc_request_io.hpp>
#include <volt/io/project_bundle.hpp>

#include "support/project_bundle_v2_board_test_support.hpp"

TEST_CASE("verified ProjectBundle logical view lazily prepares an owning DC input") {
    using namespace volt::test::project_bundle_v2;

    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "dc-input.volt";
    auto expected_part = std::optional<volt::LibraryPartRef>{};
    auto request_bytes = std::string{};
    {
        const auto fixture = board_fixture();
        expected_part = fixture.circuit->get(volt::ComponentId{0}).selected_library_part_ref();
        REQUIRE(expected_part.has_value());
        const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.bundle);
        request_bytes = volt::io::write_dc_request(
            volt::DcRequest{volt::DcRequestKey{"source-free"}, input, std::nullopt});
        auto builder = project_builder();
        builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
        builder.build().write(path);
    }

    auto input = std::optional<volt::DcInput>{};
    {
        const auto bundle = volt::io::ProjectBundle::open(path);
        const auto circuits = bundle.graph().loaded_project().circuits();
        REQUIRE(circuits.size() == 1U);
        input.emplace(circuits.front().dc_input());
    }

    REQUIRE(input.has_value());
    REQUIRE(input->circuit().all<volt::ComponentId>().size() == 1U);
    const auto *part = input->part(volt::ComponentId{0});
    REQUIRE(part != nullptr);
    CHECK(part->content_identity() == expected_part->part_digest());
    CHECK(volt::io::write_dc_request(volt::io::read_dc_request(request_bytes, *input)) ==
          request_bytes);
}
