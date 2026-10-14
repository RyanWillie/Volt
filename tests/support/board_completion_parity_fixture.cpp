#include "board_completion_fixture.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

int main(int argc, char **argv) {
    auto options = volt::test::CompletionFixtureOptions{};
    const auto mode = argc > 1 ? std::string{argv[1]} : "complete";
    if (mode == "missing")
        options.placements = 0;
    else if (mode == "netless-short") {
        options.different_nets = true;
        options.netless_zone = true;
    } else if (mode == "pad-short") {
        options.different_nets = true;
        options.zone = false;
        options.mechanical_bridge = true;
    } else if (mode == "range") {
        options.components = 1;
        options.zone = false;
        options.first_x = 10010;
        options.outline_x = 10000;
    } else if (mode == "modules")
        options.modules = true;
    else if (mode == "multi")
        options.multi_pad = true;
    else if (mode == "rounding")
        options.second_x = 20.0000004;
    else if (mode == "drill") {
        options.via = true;
        options.drill_off_grid = true;
    } else if (mode == "nc") {
        options.components = 1;
        options.connected = false;
        options.explicit_nc = true;
        options.zone = false;
        options.nc_contact = true;
    } else if (mode == "dnp") {
        options.second_dnp = true;
        options.placements = 1;
    } else if (mode == "lossy-short") {
        options.different_nets = true;
        options.shape = volt::FootprintPadShape::RoundedRectangle;
    }
    const auto artifact = volt::test::completion_fixture(options);
    auto hex = std::string{};
    constexpr auto digits = std::string_view{"0123456789abcdef"};
    for (const char value : artifact.bytes()) {
        const auto byte = static_cast<unsigned char>(value);
        hex.push_back(digits[byte >> 4U]);
        hex.push_back(digits[byte & 0xfU]);
    }
    std::cout << nlohmann::json{{"artifact_hex", hex},
                                {"report",
                                 nlohmann::json::parse(volt::io::write_board_completion(
                                     volt::io::evaluate_fabrication_completion(artifact)))}}
                     .dump()
              << '\n';
}
