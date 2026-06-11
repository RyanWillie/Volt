#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include <volt/io/board_capability_profile.hpp>

namespace {

[[nodiscard]] std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    REQUIRE(input.is_open());
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return buffer.str();
}

} // namespace

TEST_CASE("Capability profile document round-trips byte-stable") {
    const auto text = read_fixture("example_fab_2layer.voltcap.json");

    const auto profile = volt::io::read_capability_profile_text(text);
    auto input = std::istringstream{text};
    const auto stream_profile = volt::io::read_capability_profile(input);

    CHECK(profile.name() == "Example Fab 2-layer capability snapshot");
    CHECK(profile.provenance().source ==
          "Example fixture derived from a public fabrication capability table for tests only");
    CHECK(profile.minimum_clearance_mm(volt::BoardClearanceKind::Track,
                                       volt::BoardClearanceKind::Pad) == 0.2);
    CHECK(profile.copper_weight_refinements().size() == 2);
    CHECK(volt::io::write_capability_profile(profile) == text);
    CHECK(volt::io::write_capability_profile(stream_profile) == text);
}

TEST_CASE("Capability profile reader rejects malformed documents") {
    const auto text = read_fixture("example_fab_2layer.voltcap.json");

    auto wrong_format = nlohmann::json::parse(text);
    wrong_format["format"] = "volt.pcb";
    CHECK_THROWS_AS(volt::io::read_capability_profile_text(wrong_format.dump()), std::logic_error);

    auto wrong_version = nlohmann::json::parse(text);
    wrong_version["version"] = 2;
    CHECK_THROWS_AS(volt::io::read_capability_profile_text(wrong_version.dump()), std::logic_error);

    auto missing_provenance = nlohmann::json::parse(text);
    missing_provenance["profile"].erase("provenance");
    CHECK_THROWS_AS(volt::io::read_capability_profile_text(missing_provenance.dump()),
                    std::logic_error);

    auto unknown_clearance_kind = nlohmann::json::parse(text);
    unknown_clearance_kind["profile"]["minimum_clearances"][0]["first"] = "solder";
    CHECK_THROWS_AS(volt::io::read_capability_profile_text(unknown_clearance_kind.dump()),
                    std::logic_error);

    auto negative_value = nlohmann::json::parse(text);
    negative_value["profile"]["minimum_track_width_mm"] = -0.2;
    CHECK_THROWS_AS(volt::io::read_capability_profile_text(negative_value.dump()),
                    std::invalid_argument);

    auto duplicate_pair = nlohmann::json::parse(text);
    duplicate_pair["profile"]["minimum_clearances"].push_back(
        {{"first", "pad"}, {"second", "track"}, {"clearance_mm", 0.25}});
    CHECK_THROWS(volt::io::read_capability_profile_text(duplicate_pair.dump()));
}
