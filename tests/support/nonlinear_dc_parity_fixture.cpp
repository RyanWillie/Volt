#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

#include <volt/electrical/dc_solve.hpp>
#include <volt/io/electrical/dc_request_io.hpp>
#include <volt/io/electrical/dc_solve_io.hpp>

#include "diode_fixture.hpp"

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
    const auto fixture = volt::test::diode::make_fixture();
    const auto owner = volt::test::diode::input(fixture);
    const auto request = volt::test::diode::voltage_request(fixture, owner, 5.0);
    const auto compiled = volt::compile_electrical(request);
    if (compiled.model() == nullptr) {
        throw std::runtime_error{"Nonlinear parity compilation failed"};
    }
    const auto report = volt::solve_dc(*compiled.model(), volt::NonlinearDcSolveOptions{});
    if (!report.success()) {
        throw std::runtime_error{"Nonlinear parity solve failed"};
    }
    auto builder = volt::io::ProjectBundleBuilder{
        volt::io::ProjectIdentity{"Diode numerical parity fixture", std::nullopt, std::nullopt},
        volt::io::ProjectRunSummary{true, volt::io::ProjectStatus::Clean, "default", {"design"}},
        volt::io::LogicalInputName{"project.py"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"project.py"}, "native fixture"}},
        volt::io::ProjectReport{
            R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
    builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.library);
    builder.build().write(destination / "project.volt");
    write_bytes(destination / "request.json", volt::io::write_dc_request(request));
    write_bytes(destination / "report.json", volt::io::write_dc_solve_report(report));
    write_bytes(destination / "solution.json", volt::io::write_dc_solution(*report.solution()));
    const auto failed_request = volt::test::diode::voltage_request(fixture, owner, 1e12);
    const auto failed_compiled = volt::compile_electrical(failed_request);
    if (failed_compiled.model() == nullptr) {
        throw std::runtime_error{"Nonlinear failed parity compilation failed"};
    }
    const auto failed = volt::solve_dc(*failed_compiled.model(), volt::NonlinearDcSolveOptions{});
    if (failed.outcome() != volt::DcSolveOutcome::DomainLimited || failed.solution() != nullptr) {
        throw std::runtime_error{"Nonlinear domain parity did not fail as expected"};
    }
    write_bytes(destination / "failed-request.json", volt::io::write_dc_request(failed_request));
    write_bytes(destination / "failed-report.json", volt::io::write_dc_solve_report(failed));
    const auto budget =
        volt::solve_dc(*compiled.model(), volt::NonlinearDcSolveOptions{volt::DcSolveOptions{}, 1});
    write_bytes(destination / "budget-report.json", volt::io::write_dc_solve_report(budget));
}
