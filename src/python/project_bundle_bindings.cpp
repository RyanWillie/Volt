#include "project_bundle_bindings.hpp"

#include "py_board.hpp"
#include "py_circuit.hpp"
#include "py_schematic.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/stl/filesystem.h>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/io/project_bundle_writer.hpp>

#include "../io/project_bundle_v2_reports.hpp"

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

[[nodiscard]] const ProjectBundleBoardArtifacts *exact_board_artifacts(const py::handle &value) {
    if (!py::isinstance<py::capsule>(value)) {
        throw py::type_error{"ProjectBundle Board input must be typed native artifacts"};
    }
    auto capsule = py::reinterpret_borrow<py::capsule>(value);
    if (capsule.name() == nullptr ||
        std::string_view{capsule.name()} != project_bundle_board_capsule) {
        throw py::type_error{"ProjectBundle Board input has a foreign native artifact type"};
    }
    const auto *result = capsule.get_pointer<ProjectBundleBoardArtifacts>();
    if (result == nullptr) {
        throw py::type_error{"ProjectBundle Board input has no native artifact value"};
    }
    return result;
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

[[nodiscard]] py::dict exact_dict(const py::handle &value, const char *context) {
    if (!py::isinstance<py::dict>(value)) {
        throw py::type_error{std::string{context} + " must be an object"};
    }
    return py::reinterpret_borrow<py::dict>(value);
}

[[nodiscard]] std::string required_string(const py::dict &value, const char *field,
                                          const char *context) {
    if (!value.contains(field) || !py::isinstance<py::str>(value[field])) {
        throw py::type_error{std::string{context} + " must define string field " + field};
    }
    auto result = value[field].cast<std::string>();
    if (result.empty()) {
        throw py::value_error{std::string{context} + " field " + field + " must not be empty"};
    }
    return result;
}

[[nodiscard]] std::optional<std::string>
optional_string_field(const py::dict &value, const char *field, const char *context) {
    if (!value.contains(field) || value[field].is_none()) {
        return std::nullopt;
    }
    if (!py::isinstance<py::str>(value[field])) {
        throw py::type_error{std::string{context} + " field " + field + " must be a string"};
    }
    auto result = value[field].cast<std::string>();
    if (result.empty()) {
        throw py::value_error{std::string{context} + " field " + field + " must not be empty"};
    }
    return result;
}

[[nodiscard]] volt::io::ArtifactRef artifact_ref(const volt::io::ArtifactDescriptor &descriptor) {
    return volt::io::ArtifactRef{descriptor.id(), descriptor.content_digest()};
}

[[nodiscard]] const volt::io::ArtifactDescriptor &
named_model(const volt::io::ProjectBundlePublication &bundle, volt::io::ArtifactKind kind,
            const std::string &design, const std::string &name) {
    const auto match = std::ranges::find_if(bundle.artifacts(), [&](const auto &descriptor) {
        if (descriptor.kind() != kind) {
            return false;
        }
        if (kind == volt::io::ArtifactKind::LogicalModel) {
            const auto *owner =
                std::get_if<volt::io::LogicalArtifactIdentity>(&descriptor.id().owner());
            return owner != nullptr && owner->design.value() == design;
        }
        if (kind == volt::io::ArtifactKind::SchematicModel) {
            const auto *owner =
                std::get_if<volt::io::SchematicArtifactIdentity>(&descriptor.id().owner());
            return owner != nullptr && owner->design.value() == design &&
                   owner->schematic.value() == name;
        }
        const auto *owner = std::get_if<volt::io::BoardArtifactIdentity>(&descriptor.id().owner());
        return owner != nullptr && owner->design.value() == design && owner->board.value() == name;
    });
    if (match == bundle.artifacts().end()) {
        throw py::value_error{"selected export target does not identify a bundled model"};
    }
    return *match;
}

[[nodiscard]] const volt::io::ArtifactDescriptor &
compiled_board(const volt::io::ProjectBundlePublication &bundle, const std::string &design,
               const std::string &board) {
    const auto board_reference =
        artifact_ref(named_model(bundle, volt::io::ArtifactKind::BoardModel, design, board));
    const auto match = std::ranges::find_if(bundle.artifacts(), [&](const auto &descriptor) {
        return descriptor.kind() == volt::io::ArtifactKind::CompiledBoard &&
               std::ranges::find(descriptor.dependencies(), board_reference) !=
                   descriptor.dependencies().end();
    });
    if (match == bundle.artifacts().end()) {
        throw py::value_error{"selected Board has no exact CompiledBoard"};
    }
    return *match;
}

struct StepExportCandidate {
    std::string selector;
    volt::io::ArtifactRef selected_part;
    volt::io::LibraryAssetRef asset;
};

[[nodiscard]] std::vector<StepExportCandidate>
step_export_candidates(const volt::io::ProjectBundlePublication &baseline,
                       std::span<const PyCircuit *const> circuits) {
    auto result = std::vector<StepExportCandidate>{};
    for (const auto &selection : baseline.dependency_lock().selected_parts()) {
        const auto &reference = selection.selected_part;
        const auto match = std::ranges::find_if(circuits, [&](const auto *circuit) {
            const auto &bundle = circuit->selected_part_bundle();
            return bundle.identity().namespace_name() == reference.library_namespace() &&
                   bundle.identity().version() == reference.library_version() &&
                   bundle.library_digest() == reference.library_digest();
        });
        if (match == circuits.end()) {
            throw py::value_error{"selected STEP part has no exact retained library closure"};
        }
        const auto &part = (*match)->selected_part_bundle().resolve(reference);
        const auto &model = part.orderable_part().model_3d();
        if (!model.has_value() || model->format() != "step") {
            continue;
        }
        result.push_back(StepExportCandidate{
            reference.library_namespace() + "@" + reference.library_version() + ":" +
                reference.part_key().value(),
            selection.vendored_part,
            volt::io::LibraryAssetRef{reference.library_namespace(), reference.library_version(),
                                      volt::io::LibraryAssetKind::Step, reference.library_digest(),
                                      model->hash()}});
    }
    return result;
}

[[nodiscard]] volt::io::ExportRequest
step_export_request(const py::dict &row, const volt::io::ProjectBundlePublication &baseline,
                    std::span<const PyCircuit *const> circuits) {
    const auto candidates = step_export_candidates(baseline, circuits);
    const auto selector = optional_string_field(row, "part", "ProjectBundle selected STEP export");
    const auto match = selector.has_value()      ? std::ranges::find(candidates, *selector,
                                                                     &StepExportCandidate::selector)
                       : candidates.size() == 1U ? candidates.begin()
                                                 : candidates.end();
    if (match == candidates.end()) {
        auto message =
            std::string{selector.has_value()
                            ? "No exact selected STEP part " + *selector + ". Candidates: "
                            : "STEP export requires --part with an exact selector. Candidates: "};
        if (candidates.empty()) {
            message += "<none>";
        } else {
            for (auto index = std::size_t{0}; index < candidates.size(); ++index) {
                if (index != 0U) {
                    message += ", ";
                }
                message += candidates[index].selector;
            }
        }
        throw py::value_error{message};
    }
    return {volt::io::ExportKind::Step,
            volt::io::LibraryAssetExportTarget{match->selected_part, match->asset},
            volt::io::ExportRequestSchema{}, volt::io::StepParameters{}};
}

[[nodiscard]] volt::io::ExportRequest
export_request(const py::handle &value, const volt::io::ProjectBundlePublication &baseline,
               std::span<const PyCircuit *const> circuits) {
    const auto row = exact_dict(value, "ProjectBundle selected export");
    const auto kind = required_string(row, "kind", "ProjectBundle selected export");
    if (kind == "step") {
        return step_export_request(row, baseline, circuits);
    }
    const auto design = required_string(row, "design", "ProjectBundle selected export");
    if (kind == "bom") {
        const auto target =
            artifact_ref(named_model(baseline, volt::io::ArtifactKind::LogicalModel, design, ""));
        return {volt::io::ExportKind::Bom, volt::io::ModelExportTarget{target},
                volt::io::ExportRequestSchema{}, volt::io::BomParameters{}};
    }

    const auto name = required_string(row, "name", "ProjectBundle selected export");
    if (kind == "schematic_svg") {
        const auto target = artifact_ref(
            named_model(baseline, volt::io::ArtifactKind::SchematicModel, design, name));
        return {volt::io::ExportKind::SchematicSvg, volt::io::ModelExportTarget{target},
                volt::io::ExportRequestSchema{}, volt::io::SchematicSvgParameters{}};
    }

    const auto target = artifact_ref(compiled_board(baseline, design, name));
    if (kind == "board_svg") {
        return {volt::io::ExportKind::BoardSvg, volt::io::ModelExportTarget{target},
                volt::io::ExportRequestSchema{}, volt::io::BoardSvgParameters{}};
    }
    if (kind == "board_layer_image") {
        const auto layer = required_string(row, "layer", "ProjectBundle selected export");
        return {volt::io::ExportKind::BoardLayerImage,
                volt::io::BoardLayerExportTarget{target, volt::io::BoardLayerKey{layer}},
                volt::io::ExportRequestSchema{}, volt::io::BoardLayerImageParameters{}};
    }
    if (kind == "kicad_pcb") {
        return {volt::io::ExportKind::KicadPcb, volt::io::ModelExportTarget{target},
                volt::io::ExportRequestSchema{}, volt::io::KicadPcbParameters{}};
    }
    if (kind == "cpl") {
        return {volt::io::ExportKind::Cpl, volt::io::ModelExportTarget{target},
                volt::io::ExportRequestSchema{}, volt::io::CplParameters{}};
    }
    if (kind == "fabrication") {
        return {volt::io::ExportKind::Fabrication, volt::io::ModelExportTarget{target},
                volt::io::ExportRequestSchema{}, volt::io::FabricationParameters{}};
    }
    if (kind == "whole_board_glb") {
        return {volt::io::ExportKind::WholeBoardGlb, volt::io::ModelExportTarget{target},
                volt::io::ExportRequestSchema{}, volt::io::WholeBoardGlbParameters{}};
    }
    throw py::value_error{"ProjectBundle selected export kind is unsupported"};
}

[[nodiscard]] py::dict compiled_identity_dict(const volt::CompiledBoardIdentity &identity) {
    auto result = py::dict{};
    result["board"] = identity.board().value();
    result["provenance_digest"] = identity.provenance_digest().value();
    return result;
}

[[nodiscard]] py::dict library_part_ref_dict(const volt::LibraryPartRef &reference) {
    auto result = py::dict{};
    result["library_namespace"] = reference.library_namespace();
    result["library_version"] = reference.library_version();
    result["part_key"] = reference.part_key().value();
    result["library_bundle_digest"] = reference.library_digest().value();
    result["part_digest"] = reference.part_digest().value();
    return result;
}

[[nodiscard]] py::dict dependency_lock_dict(const volt::io::DependencyLock &lock) {
    auto result = py::dict{};
    auto libraries = py::list{};
    for (const auto &library : lock.libraries()) {
        auto row = py::dict{};
        row["library"] = library.library_namespace;
        row["version"] = library.library_version;
        row["library_bundle_digest"] = library.library_bundle_digest.value();
        libraries.append(std::move(row));
    }
    auto selected_parts = py::list{};
    for (const auto &selection : lock.selected_parts()) {
        auto row = py::dict{};
        row["selected_part"] = library_part_ref_dict(selection.selected_part);
        auto owner = py::dict{};
        owner["type"] = "library_part_ref";
        owner["value"] = library_part_ref_dict(
            std::get<volt::LibraryPartRef>(selection.vendored_part.artifact().owner()));
        auto artifact = py::dict{};
        artifact["kind"] =
            std::string{volt::io::artifact_kind_name(selection.vendored_part.artifact().kind())};
        artifact["owner"] = std::move(owner);
        auto vendored = py::dict{};
        vendored["artifact"] = std::move(artifact);
        vendored["content_digest"] = selection.vendored_part.content_digest().value();
        row["vendored_part"] = std::move(vendored);
        selected_parts.append(std::move(row));
    }
    result["libraries"] = std::move(libraries);
    result["selected_parts"] = std::move(selected_parts);
    return result;
}

} // namespace

void bind_project_bundle(py::module_ &module) {
    py::enum_<volt::io::ProjectBundleSchemaVersion>(module, "ProjectBundleSchemaVersion")
        .value("V2", volt::io::ProjectBundleSchemaVersion::V2);
    py::enum_<volt::io::ProjectBundleStorageKind>(module, "ProjectBundleStorageKind")
        .value("DIRECTORY", volt::io::ProjectBundleStorageKind::Directory)
        .value("ZIP_ARCHIVE", volt::io::ProjectBundleStorageKind::ZipArchive);
    py::enum_<volt::io::BundleIntegrityStatus>(module, "BundleIntegrityStatus")
        .value("VERIFIED", volt::io::BundleIntegrityStatus::Verified);

    py::class_<volt::io::ArtifactId>(module, "ArtifactId")
        .def_property_readonly("kind",
                               [](const volt::io::ArtifactId &id) {
                                   return std::string{volt::io::artifact_kind_name(id.kind())};
                               })
        .def("__eq__", [](const volt::io::ArtifactId &left, const volt::io::ArtifactId &right) {
            return left == right;
        });

    py::class_<volt::io::ProjectBundleArtifactView>(module, "ProjectBundleArtifact")
        .def_property_readonly(
            "id",
            [](const volt::io::ProjectBundleArtifactView &view) { return view.descriptor().id(); })
        .def_property_readonly("id_json", &volt::io::ProjectBundleArtifactView::id_json)
        .def_property_readonly("kind",
                               [](const volt::io::ProjectBundleArtifactView &view) {
                                   return std::string{
                                       volt::io::artifact_kind_name(view.descriptor().kind())};
                               })
        .def_property_readonly("role",
                               [](const volt::io::ProjectBundleArtifactView &view) {
                                   return std::string{
                                       volt::io::artifact_role_name(view.descriptor().role())};
                               })
        .def_property_readonly("path",
                               [](const volt::io::ProjectBundleArtifactView &view) {
                                   return view.descriptor().path().value();
                               })
        .def_property_readonly("content_digest",
                               [](const volt::io::ProjectBundleArtifactView &view) {
                                   return view.descriptor().content_digest().value();
                               })
        .def_property_readonly("manifest_record_json",
                               &volt::io::ProjectBundleArtifactView::manifest_record_json)
        .def_property_readonly("bytes", [](const volt::io::ProjectBundleArtifactView &view) {
            return py::bytes{view.bytes()};
        });

    py::class_<volt::io::LoadedLogicalModelView>(module, "LoadedCircuit")
        .def_property_readonly(
            "design",
            [](const volt::io::LoadedLogicalModelView &view) { return view.design().value(); })
        .def_property_readonly("artifact", &volt::io::LoadedLogicalModelView::artifact)
        .def_property_readonly("component_count",
                               [](const volt::io::LoadedLogicalModelView &view) {
                                   return view.model().template all<volt::ComponentId>().size();
                               })
        .def_property_readonly("net_count", [](const volt::io::LoadedLogicalModelView &view) {
            return view.model().template all<volt::NetId>().size();
        });

    py::class_<volt::io::LoadedSchematicView>(module, "LoadedSchematic")
        .def_property_readonly(
            "design",
            [](const volt::io::LoadedSchematicView &view) { return view.design().value(); })
        .def_property_readonly(
            "name",
            [](const volt::io::LoadedSchematicView &view) { return view.schematic().value(); })
        .def_property_readonly("artifact", &volt::io::LoadedSchematicView::artifact)
        .def_property_readonly("circuit", &volt::io::LoadedSchematicView::circuit);

    py::class_<volt::io::LoadedBoardView>(module, "LoadedBoard")
        .def_property_readonly(
            "design", [](const volt::io::LoadedBoardView &view) { return view.design().value(); })
        .def_property_readonly(
            "name", [](const volt::io::LoadedBoardView &view) { return view.board().value(); })
        .def_property_readonly("artifact", &volt::io::LoadedBoardView::artifact)
        .def_property_readonly("circuit", &volt::io::LoadedBoardView::circuit);

    py::class_<volt::io::LoadedCompiledBoardView>(module, "LoadedCompiledBoard")
        .def_property_readonly("identity",
                               [](const volt::io::LoadedCompiledBoardView &view) {
                                   return compiled_identity_dict(view.identity());
                               })
        .def_property_readonly("artifact", &volt::io::LoadedCompiledBoardView::artifact);

    py::class_<volt::io::LoadedBoardSceneView>(module, "LoadedBoardScene")
        .def_property_readonly("artifact", &volt::io::LoadedBoardSceneView::artifact)
        .def_property_readonly("compiled_board", &volt::io::LoadedBoardSceneView::compiled_board)
        .def_property_readonly("model_digests", [](const volt::io::LoadedBoardSceneView &view) {
            auto result = py::list{};
            for (const auto &model : view.model().models()) {
                result.append(model.reference().digest().value());
            }
            return result;
        });

    py::class_<volt::io::LoadedProject>(module, "LoadedProject")
        .def_property_readonly("circuits", &volt::io::LoadedProject::circuits)
        .def_property_readonly("schematics", &volt::io::LoadedProject::schematics)
        .def_property_readonly("boards", &volt::io::LoadedProject::boards)
        .def_property_readonly("compiled_boards", &volt::io::LoadedProject::compiled_boards)
        .def_property_readonly("board_scenes", &volt::io::LoadedProject::board_scenes)
        .def_property_readonly("diagnostics", &volt::io::LoadedProject::diagnostics)
        .def_property_readonly("tests", &volt::io::LoadedProject::tests)
        .def_property_readonly("selected_exports", &volt::io::LoadedProject::selected_exports);

    py::class_<volt::io::ProjectBundleGraphView>(module, "ProjectBundleGraphView")
        .def_property_readonly("project_name", &volt::io::ProjectBundleGraphView::project_name)
        .def_property_readonly("project_version",
                               &volt::io::ProjectBundleGraphView::project_version)
        .def_property_readonly("project_description",
                               &volt::io::ProjectBundleGraphView::project_description)
        .def_property_readonly("build_id",
                               [](const volt::io::ProjectBundleGraphView &view) {
                                   return view.build_id().content_hash().value();
                               })
        .def_property_readonly("bundle_digest",
                               [](const volt::io::ProjectBundleGraphView &view) {
                                   return view.bundle_digest().value();
                               })
        .def_property_readonly("authoring_inputs_digest",
                               [](const volt::io::ProjectBundleGraphView &view) {
                                   return view.authoring_inputs_digest().value();
                               })
        .def_property_readonly("dependency_lock",
                               [](const volt::io::ProjectBundleGraphView &view) {
                                   return dependency_lock_dict(view.dependency_lock());
                               })
        .def_property_readonly("artifacts", &volt::io::ProjectBundleGraphView::artifacts)
        .def("artifact", &volt::io::ProjectBundleGraphView::artifact, py::arg("id"))
        .def_property_readonly("manifest_bytes",
                               [](const volt::io::ProjectBundleGraphView &view) {
                                   return py::bytes{view.manifest_bytes()};
                               })
        .def_property_readonly("loaded_project", &volt::io::ProjectBundleGraphView::loaded_project);

    py::class_<volt::io::ProjectBundle>(module, "ProjectBundle")
        .def_static("open", &volt::io::ProjectBundle::open, py::arg("path"))
        .def_property_readonly("schema_version", &volt::io::ProjectBundle::schema_version)
        .def_property_readonly("storage_kind", &volt::io::ProjectBundle::storage_kind)
        .def_property_readonly("integrity_status", &volt::io::ProjectBundle::integrity_status)
        .def_property_readonly("graph", &volt::io::ProjectBundle::graph);

    module.def(
        "_prepare_project_bundle_board",
        [](const PyBoard &board, bool models3d) {
            return py::capsule{
                new ProjectBundleBoardArtifacts{board, models3d}, project_bundle_board_capsule,
                [](void *pointer) { delete static_cast<ProjectBundleBoardArtifacts *>(pointer); }};
        },
        py::arg("board"), py::arg("models3d"));

    module.def(
        "_project_bundle_library_report_subject",
        [](const std::string &library_namespace, const std::string &library_version,
           const std::string &library_digest) {
            return volt::io::detail::project_library_report_subject(
                library_namespace, library_version, volt::ContentHash{library_digest});
        },
        py::arg("library_namespace"), py::arg("library_version"), py::arg("library_digest"));

    module.def(
        "_project_bundle_subject_mask",
        [](const py::list &logicals, const py::list &subjects) {
            auto selected = std::set<std::pair<std::string, std::string>>{};
            for (const auto item : logicals) {
                const auto row = exact_tuple(item, 2, "ProjectBundle logical input");
                const auto *circuit = row[1].cast<const PyCircuit *>();
                if (circuit == nullptr) {
                    throw py::type_error{"ProjectBundle logical input has no native Circuit"};
                }
                const auto &logical = circuit->logical_circuit();
                for (auto index = std::size_t{0}; index < logical.all<volt::ComponentId>().size();
                     ++index) {
                    const auto &reference =
                        volt::queries::selected_library_part_ref(logical, volt::ComponentId{index});
                    if (reference.has_value()) {
                        selected.emplace(
                            volt::io::detail::project_library_report_subject(*reference),
                            reference->part_key().value());
                    }
                }
            }
            auto result = py::list{};
            for (const auto item : subjects) {
                const auto row = exact_tuple(item, 2, "ProjectBundle report subject");
                if (row[0].is_none() || row[1].is_none()) {
                    result.append(true);
                    continue;
                }
                const auto report = row[0].cast<std::string>();
                const auto source = row[1].cast<std::string>();
                constexpr auto library_prefix = std::string_view{"library:"};
                constexpr auto part_prefix = std::string_view{"part:"};
                if (!report.starts_with(library_prefix) || !source.starts_with(part_prefix)) {
                    result.append(true);
                    continue;
                }
                result.append(selected.contains({report, source.substr(part_prefix.size())}));
            }
            return result;
        },
        py::arg("logicals"), py::arg("subjects"));

    module.def(
        "_write_project_bundle_v2",
        [](const std::string &destination, const std::string &project_name,
           const py::object &project_version, const py::object &project_description, bool ok,
           const std::string &status, const std::string &profile,
           const std::vector<std::string> &stages, const std::string &entrypoint,
           const py::list &authoring_inputs, const py::list &logicals, const py::list &schematics,
           const py::list &boards, const std::string &diagnostics_bytes,
           const std::string &tests_bytes, const py::list &selected_exports) {
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

            auto builder = volt::io::ProjectBundleBuilder{
                volt::io::ProjectIdentity{project_name, optional_string(project_version),
                                          optional_string(project_description)},
                volt::io::ProjectRunSummary{ok, project_status(status), profile, stages},
                volt::io::LogicalInputName{entrypoint},
                std::move(inputs),
                volt::io::ProjectReport{diagnostics_bytes},
                volt::io::ProjectReport{tests_bytes}};

            auto circuits = std::vector<const PyCircuit *>{};
            circuits.reserve(static_cast<std::size_t>(py::len(logicals)));
            for (const auto item : logicals) {
                const auto row = exact_tuple(item, 2, "ProjectBundle logical input");
                const auto *circuit = row[1].cast<const PyCircuit *>();
                if (circuit == nullptr) {
                    throw py::type_error{"ProjectBundle logical input has no native Circuit"};
                }
                circuits.push_back(circuit);
                builder.add_logical(volt::io::DesignKey{row[0].cast<std::string>()},
                                    circuit->logical_circuit(), circuit->selected_part_bundle());
            }
            for (const auto item : schematics) {
                const auto row = exact_tuple(item, 3, "ProjectBundle schematic input");
                const auto *schematic = row[2].cast<const PySchematicDocument *>();
                if (schematic == nullptr) {
                    throw py::type_error{"ProjectBundle schematic input has no native Schematic"};
                }
                builder.add_schematic(volt::io::DesignKey{row[0].cast<std::string>()},
                                      volt::io::SchematicKey{row[1].cast<std::string>()},
                                      schematic->native_schematic());
            }

            for (const auto item : boards) {
                const auto row = exact_tuple(item, 2, "ProjectBundle Board input");
                const auto *artifacts = exact_board_artifacts(row[1]);
                const auto &board = artifacts->board();
                builder.add_board(volt::io::DesignKey{row[0].cast<std::string>()},
                                  board.authoring_board(), artifacts->compiled(),
                                  artifacts->scene(), board.selected_part_bundle());
            }

            auto bundle = builder.build();
            if (py::len(selected_exports) != 0) {
                auto requests = volt::io::ExportSelection{};
                requests.reserve(static_cast<std::size_t>(py::len(selected_exports)));
                for (const auto item : selected_exports) {
                    requests.push_back(export_request(item, bundle, circuits));
                }
                builder.select_exports(std::move(requests));
                bundle = builder.build();
            }
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
        py::arg("tests_bytes"), py::arg("selected_exports"));
}

} // namespace volt::python
