#include "manufacturing_fixture.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

int main(int argc, char **argv) {
    const auto mode = argc > 1 ? std::string{argv[1]} : "a";
    const auto strict = mode == "b";
    auto options = volt::test::manufacturing_options(strict);
    if (mode == "missing")
        options.placements = 0;
    if (mode == "short") {
        options.different_nets = true;
        options.netless_zone = true;
    }
    const auto compiled = volt::test::completion_fixture(options);
    const auto target = volt::test::manufacturing_target(compiled, strict);
    const auto result = volt::evaluate_manufacturing_coverage(compiled, target);
    auto hex = std::string{};
    constexpr auto digits = std::string_view{"0123456789abcdef"};
    for (const char value : compiled.bytes()) {
        const auto byte = static_cast<unsigned char>(value);
        hex.push_back(digits[byte >> 4U]);
        hex.push_back(digits[byte & 15U]);
    }
    std::cout << nlohmann::json{{"artifact_hex", hex},
                                {"target", volt::io::write_manufacturing_target(target)},
                                {"report", volt::io::write_manufacturing_coverage(result)}}
                     .dump()
              << '\n';
}
