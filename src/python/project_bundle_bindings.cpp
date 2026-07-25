#include "project_bundle_bindings.hpp"

#include "py_board.hpp"
#include "py_circuit.hpp"
#include "py_schematic.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/io/project_bundle_v2_writer.hpp>

namespace volt::python {
namespace {

class ProjectBundleBoardArtifacts final {
  public:
    ProjectBundleBoardArtifacts(const PyBoard &board, bool models3d)
        : board_{&board}, compiled_{board.compile_for_delivery(models3d)},
          scene_{volt::prepare_board_scene(compiled_)} {}

    [[nodiscard]] const PyBoard &board() const noexcept { return *board_; }

    [[nodiscard]] const volt::CompiledBoard &compiled() const noexcept { return compiled_; }

    [[nodiscard]] const volt::BoardScene &scene() const noexcept { return scene_; }

  private:
    const PyBoard *board_;
    volt::CompiledBoard compiled_;
    volt::BoardScene scene_;
};

constexpr auto project_bundle_board_capsule = "volt.ProjectBundleBoardArtifacts.v1";

[[nodiscard]] const ProjectBundleBoardArtifacts &exact_board_artifacts(const py::handle &value) {
    if (!py::isinstance<py::capsule>(value)) {
        throw py::type_error{"ProjectBundle Board input must be typed native artifacts"};
    }
    auto capsule = py::reinterpret_borrow<py::capsule>(value);
    if (capsule.name() == nullptr ||
        std::string_view{capsule.name()} != project_bundle_board_capsule) {
        throw py::type_error{"ProjectBundle Board input has a foreign native artifact type"};
    }
    return *capsule.get_pointer<ProjectBundleBoardArtifacts>();
}

[[nodiscard]] volt::io::ProjectStatus project_status(const std::string &value) {
    if (value == "clean") {
        return volt::io::ProjectStatus::Clean;
    }
    if (value == "expected-diagnostics") {
        return volt::io::ProjectStatus::ExpectedDiagnostics;
    }
    if (value == "failed") {
        return volt::io::ProjectStatus::Failed;
    }
    throw std::invalid_argument{"ProjectBundle status is unsupported"};
}

[[nodiscard]] std::optional<std::string> optional_string(const py::object &value) {
    return value.is_none() ? std::nullopt : std::optional<std::string>{value.cast<std::string>()};
}

[[nodiscard]] py::tuple exact_tuple(const py::handle &value, std::size_t size,
                                    const char *context) {
    if (!py::isinstance<py::tuple>(value)) {
        throw py::type_error{std::string{context} + " must be a tuple"};
    }
    auto result = py::reinterpret_borrow<py::tuple>(value);
    if (py::len(result) != size) {
        throw py::value_error{std::string{context} + " has the wrong field count"};
    }
    return result;
}

} // namespace

void bind_project_bundle(py::module_ &module) {
    module.def(
        "_prepare_project_bundle_board",
        [](const PyBoard &board, bool models3d) {
            return py::capsule{
                new ProjectBundleBoardArtifacts{board, models3d}, project_bundle_board_capsule,
                [](void *pointer) { delete static_cast<ProjectBundleBoardArtifacts *>(pointer); }};
        },
        py::arg("board"), py::arg("models3d"));

    module.def(
        "_write_project_bundle_v2",
        [](const std::string &destination, const std::string &project_name,
           const py::object &project_version, const py::object &project_description, bool ok,
           const std::string &status, const std::string &profile,
           const std::vector<std::string> &stages, const std::string &entrypoint,
           const py::list &authoring_inputs, const py::list &logicals, const py::list &schematics,
           const py::list &boards, const std::string &diagnostics_bytes,
           const std::string &tests_bytes) {
            auto inputs = std::vector<volt::io::AuthoringInput>{};
            inputs.reserve(static_cast<std::size_t>(py::len(authoring_inputs)));
            for (const auto item : authoring_inputs) {
                const auto row = exact_tuple(item, 3, "ProjectBundle authoring input");
                const auto kind = row[0].cast<std::string>();
                const auto input_kind =
                    kind == "project_source" ? volt::io::AuthoringInputKind::ProjectSource
                    : kind == "declared_input"
                        ? volt::io::AuthoringInputKind::DeclaredInput
                        : throw std::invalid_argument{
                              "ProjectBundle authoring input kind is unsupported"};
                inputs.emplace_back(input_kind,
                                    volt::io::LogicalInputName{row[1].cast<std::string>()},
                                    row[2].cast<py::bytes>().cast<std::string>());
            }

            auto builder = volt::io::ProjectBundleV2Builder{
                volt::io::ProjectIdentity{project_name, optional_string(project_version),
                                          optional_string(project_description)},
                volt::io::ProjectRunSummary{ok, project_status(status), profile, stages},
                volt::io::LogicalInputName{entrypoint},
                std::move(inputs),
                volt::io::ProjectReport{diagnostics_bytes},
                volt::io::ProjectReport{tests_bytes}};

            for (const auto item : logicals) {
                const auto row = exact_tuple(item, 2, "ProjectBundle logical input");
                const auto &circuit = row[1].cast<const PyCircuit &>();
                builder.add_logical(volt::io::DesignKey{row[0].cast<std::string>()},
                                    circuit.logical_circuit(), circuit.selected_part_bundle());
            }
            for (const auto item : schematics) {
                const auto row = exact_tuple(item, 3, "ProjectBundle schematic input");
                const auto &schematic = row[2].cast<const PySchematicDocument &>();
                builder.add_schematic(volt::io::DesignKey{row[0].cast<std::string>()},
                                      volt::io::SchematicKey{row[1].cast<std::string>()},
                                      schematic.native_schematic());
            }

            for (const auto item : boards) {
                const auto row = exact_tuple(item, 2, "ProjectBundle Board input");
                const auto &artifacts = exact_board_artifacts(row[1]);
                const auto &board = artifacts.board();
                builder.add_board(volt::io::DesignKey{row[0].cast<std::string>()},
                                  board.authoring_board(), artifacts.compiled(), artifacts.scene(),
                                  board.selected_part_bundle());
            }

            auto bundle = builder.build();
            bundle.write(destination);
            auto result = py::dict{};
            result["build_id"] = bundle.build_id().content_hash().value();
            result["bundle_digest"] = bundle.bundle_digest().value();
            result["artifact_count"] = bundle.artifacts().size();
            return result;
        },
        py::arg("destination"), py::arg("project_name"), py::arg("project_version"),
        py::arg("project_description"), py::arg("ok"), py::arg("status"), py::arg("profile"),
        py::arg("stages"), py::arg("entrypoint"), py::arg("authoring_inputs"), py::arg("logicals"),
        py::arg("schematics"), py::arg("boards"), py::arg("diagnostics_bytes"),
        py::arg("tests_bytes"));
}

} // namespace volt::python
