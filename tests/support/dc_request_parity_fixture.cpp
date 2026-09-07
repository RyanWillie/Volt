#include <filesystem>
#include <fstream>
#include <string_view>

#include <volt/io/electrical/dc_request_io.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/project_bundle_writer.hpp>

#include "dc_request_fixture.hpp"

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
    const auto fixture = volt::test::dc_request::make_fixture();
    const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
    const auto request = volt::test::dc_request::complete_request(input);

    auto project = volt::io::ProjectBundleBuilder{
        volt::io::ProjectIdentity{"native DC request parity", std::nullopt, std::nullopt},
        volt::io::ProjectRunSummary{true, volt::io::ProjectStatus::Clean, "default", {"design"}},
        volt::io::LogicalInputName{"project.py"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"project.py"}, "native fixture"}},
        volt::io::ProjectReport{
            R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
    project.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.library);
    project.build().write(destination / "project.volt");
    write_bytes(destination / "request.json", volt::io::write_dc_request(request));
    write_bytes(destination / "logical.json", volt::io::write_logical_circuit(*fixture.circuit));
}
