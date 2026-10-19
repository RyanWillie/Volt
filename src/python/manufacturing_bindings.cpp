#include "manufacturing_bindings.hpp"
#include <pybind11/stl.h>
#include <volt/io/capabilities/board_capability_profile.hpp>
#include <volt/io/pcb/compiled_board.hpp>
#include <volt/io/pcb/manufacturing_target.hpp>

namespace volt::python {
namespace py = pybind11;

namespace {
PyObject *read_error_type = nullptr;
}

void bind_manufacturing(py::module_ &module) {
    const auto error_class_name =
        module.attr("__name__").cast<std::string>() + ".ManufacturingTargetReadError";
    read_error_type =
        PyErr_NewException(error_class_name.c_str(), module.attr("VoltError").ptr(), nullptr);
    module.attr("ManufacturingTargetReadError") =
        py::reinterpret_steal<py::object>(read_error_type);
    py::register_exception_translator([](std::exception_ptr failure) {
        if (!failure)
            return;
        try {
            std::rethrow_exception(failure);
        } catch (const io::ManufacturingTargetReadError &error) {
            auto type = py::reinterpret_borrow<py::object>(read_error_type);
            auto instance = type(error.what());
            instance.attr("code") =
                error.code() == io::ManufacturingTargetReadErrorCode::UnsupportedVersion
                    ? "UnsupportedVersion"
                    : "Malformed";
            PyErr_SetObject(read_error_type, instance.ptr());
        }
    });
    py::enum_<ManufacturingGoal>(module, "ManufacturingGoal")
        .value("Fabrication", ManufacturingGoal::Fabrication)
        .value("FabricationAndAssembly", ManufacturingGoal::FabricationAndAssembly);
    py::enum_<ManufacturingFamily>(module, "ManufacturingFamily")
        .value("RigidFr4", ManufacturingFamily::RigidFr4)
        .value("Flex", ManufacturingFamily::Flex);
    py::enum_<ManufacturingMaterial>(module, "ManufacturingMaterial")
        .value("Fr4Tg135", ManufacturingMaterial::Fr4Tg135)
        .value("OtherFr4", ManufacturingMaterial::OtherFr4);
    py::enum_<ManufacturingFinish>(module, "ManufacturingFinish")
        .value("LeadFreeHasl", ManufacturingFinish::LeadFreeHasl)
        .value("Enig", ManufacturingFinish::Enig);
    py::enum_<ManufacturingMaskSides>(module, "ManufacturingMaskSides")
        .value("Both", ManufacturingMaskSides::Both)
        .value("SingleSide", ManufacturingMaskSides::SingleSide)
        .value("NoMask", ManufacturingMaskSides::NoMask);
    py::enum_<ManufacturingMaskColour>(module, "ManufacturingMaskColour")
        .value("Green", ManufacturingMaskColour::Green)
        .value("OtherColour", ManufacturingMaskColour::OtherColour);
    py::enum_<ManufacturingViaTreatment>(module, "ManufacturingViaTreatment")
        .value("Exposed", ManufacturingViaTreatment::Exposed)
        .value("Tented", ManufacturingViaTreatment::Tented)
        .value("Filled", ManufacturingViaTreatment::Filled);
    py::enum_<ManufacturingDrilling>(module, "ManufacturingDrilling")
        .value("MechanicalThrough", ManufacturingDrilling::MechanicalThrough)
        .value("Laser", ManufacturingDrilling::Laser);
    py::enum_<ManufacturingHoleTreatment>(module, "ManufacturingHoleTreatment")
        .value("RoundOnly", ManufacturingHoleTreatment::RoundOnly)
        .value("BlindBuried", ManufacturingHoleTreatment::BlindBuried);
    py::enum_<ManufacturingSlotTreatment>(module, "ManufacturingSlotTreatment")
        .value("NoSlots", ManufacturingSlotTreatment::NoSlots)
        .value("Routed", ManufacturingSlotTreatment::Routed)
        .value("Plated", ManufacturingSlotTreatment::Plated);
    py::enum_<ManufacturingAssembly>(module, "ManufacturingAssembly")
        .value("NoAssembly", ManufacturingAssembly::NoAssembly)
        .value("SmtAndThroughHole", ManufacturingAssembly::SmtAndThroughHole)
        .value("OtherAssembly", ManufacturingAssembly::OtherAssembly);
    py::enum_<ManufacturingState>(module, "ManufacturingState")
        .value("NativePass", ManufacturingState::NativePass)
        .value("Failed", ManufacturingState::Failed)
        .value("NotEvaluated", ManufacturingState::NotEvaluated)
        .value("NotApplicable", ManufacturingState::NotApplicable);
    py::enum_<ManufacturingStatus>(module, "ManufacturingStatus")
        .value("Blocked", ManufacturingStatus::Blocked)
        .value("UnsupportedTarget", ManufacturingStatus::UnsupportedTarget);
    py::class_<BoardCapabilityRange>(module, "ManufacturingRange")
        .def(py::init<double, double>(), py::arg("minimum_mm"), py::arg("maximum_mm"))
        .def_readwrite("minimum_mm", &BoardCapabilityRange::minimum_mm)
        .def_readwrite("maximum_mm", &BoardCapabilityRange::maximum_mm);
    py::class_<ManufacturingProcess>(module, "ManufacturingProcess")
        .def(py::init<>())
        .def_readwrite("family", &ManufacturingProcess::family)
        .def_readwrite("material", &ManufacturingProcess::material)
        .def_readwrite("copper_layers", &ManufacturingProcess::copper_layers)
        .def_readwrite("top_copper_weight_oz", &ManufacturingProcess::top_copper_weight_oz)
        .def_readwrite("bottom_copper_weight_oz", &ManufacturingProcess::bottom_copper_weight_oz)
        .def_readwrite("thickness_mm", &ManufacturingProcess::thickness_mm)
        .def_readwrite("finish", &ManufacturingProcess::finish)
        .def_readwrite("mask_sides", &ManufacturingProcess::mask_sides)
        .def_readwrite("mask_colour", &ManufacturingProcess::mask_colour)
        .def_readwrite("via_treatment", &ManufacturingProcess::via_treatment)
        .def_readwrite("drilling", &ManufacturingProcess::drilling)
        .def_readwrite("holes", &ManufacturingProcess::holes)
        .def_readwrite("slots", &ManufacturingProcess::slots)
        .def_readwrite("assembly", &ManufacturingProcess::assembly)
        .def_readwrite("dimension_x_mm", &ManufacturingProcess::dimension_x_mm)
        .def_readwrite("dimension_y_mm", &ManufacturingProcess::dimension_y_mm);
    py::class_<CplRotationOffset>(module, "ManufacturingCplOffset")
        .def(py::init([](std::string library, std::string name, double rotation_deg) {
                 return CplRotationOffset{FootprintRef{std::move(library), std::move(name)},
                                          rotation_deg};
             }),
             py::arg("library"), py::arg("name"), py::arg("rotation_deg"))
        .def_property_readonly(
            "library", [](const CplRotationOffset &value) { return value.footprint().library(); })
        .def_property_readonly(
            "name", [](const CplRotationOffset &value) { return value.footprint().name(); })
        .def_property_readonly("rotation_deg", &CplRotationOffset::rotation_deg);
    py::class_<CplProjectionOptions>(module, "ManufacturingCplOptions")
        .def(py::init<>())
        .def_readwrite("rotation_offsets", &CplProjectionOptions::rotation_offsets);
    py::class_<ManufacturingProfile>(module, "ManufacturingProfile")
        .def(py::init([](const std::string &capability_json, const std::string &source_content,
                         ManufacturingProcess applicability, std::optional<BoardCapabilityRange> x,
                         std::optional<BoardCapabilityRange> y, std::optional<double> radial) {
                 return ManufacturingProfile{io::read_capability_profile_text(capability_json),
                                             ContentHash{source_content},
                                             applicability,
                                             x,
                                             y,
                                             radial};
             }),
             py::arg("capability_json"), py::arg("source_content"), py::arg("applicability"),
             py::arg("dimension_x_mm") = py::none(), py::arg("dimension_y_mm") = py::none(),
             py::arg("minimum_via_radial_annulus_mm") = py::none())
        .def_property_readonly("digest",
                               [](const ManufacturingProfile &p) { return p.digest().value(); })
        .def_property_readonly(
            "source_content",
            [](const ManufacturingProfile &p) { return p.source_content().value(); })
        .def_property_readonly("capability_json",
                               [](const ManufacturingProfile &p) {
                                   return io::write_capability_profile(p.capability());
                               })
        .def_property_readonly("applicability",
                               [](const ManufacturingProfile &p) { return p.applicability(); });
    py::class_<ManufacturingTarget>(module, "ManufacturingTarget")
        .def(py::init([](const std::string &board, const std::string &provenance_digest,
                         const std::string &content_digest, ManufacturingGoal goal,
                         ManufacturingProcess process, ManufacturingProfile profile,
                         std::optional<CplProjectionOptions> cpl) {
                 return ManufacturingTarget{
                     CompiledBoardIdentity{BoardName{board}, ContentHash{provenance_digest}},
                     ContentHash{content_digest},
                     goal,
                     process,
                     std::move(profile),
                     std::move(cpl)};
             }),
             py::arg("board"), py::arg("provenance_digest"), py::arg("content_digest"),
             py::arg("goal"), py::arg("process"), py::arg("profile"),
             py::arg("cpl_options") = py::none())
        .def_property_readonly("digest",
                               [](const ManufacturingTarget &t) { return t.digest().value(); })
        .def_property_readonly("goal", &ManufacturingTarget::goal)
        .def_property_readonly("process", [](const ManufacturingTarget &t) { return t.process(); })
        .def_property_readonly("profile", [](const ManufacturingTarget &t) { return t.profile(); })
        .def_property_readonly("cpl_options",
                               [](const ManufacturingTarget &t) { return t.cpl_options(); });
    py::class_<ManufacturingCoverage>(module, "ManufacturingCoverage")
        .def_property_readonly("status", &ManufacturingCoverage::status)
        .def_property_readonly("ready", &ManufacturingCoverage::ready)
        .def_property_readonly(
            "target_digest",
            [](const ManufacturingCoverage &r) { return r.target_digest().value(); })
        .def_property_readonly(
            "profile_digest",
            [](const ManufacturingCoverage &r) { return r.profile_digest().value(); })
        .def("to_json", &io::write_manufacturing_coverage);
    module.def("read_manufacturing_target", &io::read_manufacturing_target, py::arg("text"));
    module.def("write_manufacturing_target", &io::write_manufacturing_target, py::arg("target"));
    module.def(
        "evaluate_manufacturing_coverage",
        [](const py::bytes &artifact, const ManufacturingTarget &target) {
            const auto compiled = io::open_compiled_board(artifact.cast<std::string>());
            return evaluate_manufacturing_coverage(compiled, target);
        },
        py::arg("artifact"), py::arg("target"));
    module.def(
        "make_manufacturing_target",
        [](const py::bytes &artifact, ManufacturingGoal goal, ManufacturingProcess process,
           ManufacturingProfile profile, std::optional<CplProjectionOptions> cpl) {
            const auto compiled = io::open_compiled_board(artifact.cast<std::string>());
            return ManufacturingTarget{
                compiled.identity(), compiled.content_digest(), goal, process, std::move(profile),
                std::move(cpl)};
        },
        py::arg("artifact"), py::arg("goal"), py::arg("process"), py::arg("profile"),
        py::arg("cpl_options") = py::none());
}
} // namespace volt::python
