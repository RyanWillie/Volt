#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

#include <volt/electrical/compiled_electrical_model.hpp>
#include <volt/io/electrical/compiled_electrical_model_io.hpp>
#include <volt/io/electrical/dc_request_io.hpp>

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
    const auto report = volt::compile_electrical(request);
    if (report.model() == nullptr) {
        throw std::runtime_error{"Native electrical compilation parity fixture is incomplete"};
    }

    volt::test::electrical_compilation::source_free_project(fixture).write(destination /
                                                                           "project.volt");
    write_bytes(destination / "request.json", volt::io::write_dc_request(request));
    write_bytes(destination / "compiled.json",
                volt::io::write_compiled_electrical_model(*report.model()));
    write_bytes(destination / "report.json", volt::io::write_electrical_compile_report(report));
}
