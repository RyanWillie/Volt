#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

#include <volt/io/electrical/dc_request_io.hpp>
#include <volt/io/electrical/dc_solve_io.hpp>

#include "electrical_compilation_fixture.hpp"

namespace {
void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
    auto output = std::ofstream{};
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    const auto destination = std::filesystem::path{argv[1]};
    std::filesystem::create_directory(destination);
    const auto fixture = volt::test::electrical_compilation::make_fixture();
    const auto input = volt::test::electrical_compilation::input(fixture);
    const auto request = volt::test::electrical_compilation::divider_request(fixture, input);
    const auto compiled = volt::compile_electrical(request);
    if (compiled.model() == nullptr) {
        throw std::runtime_error{"DC parity compilation failed"};
    }
    const auto report = volt::solve_dc(*compiled.model());
    if (!report.success()) {
        throw std::runtime_error{"DC parity solve failed"};
    }
    volt::test::electrical_compilation::source_free_project(fixture).write(destination /
                                                                           "project.volt");
    write_bytes(destination / "request.json", volt::io::write_dc_request(request));
    write_bytes(destination / "report.json", volt::io::write_dc_solve_report(report));
    write_bytes(destination / "solution.json", volt::io::write_dc_solution(*report.solution()));
}
