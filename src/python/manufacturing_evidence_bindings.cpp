#include "manufacturing_bindings.hpp"
#include <pybind11/stl.h>
#include <volt/io/pcb/compiled_board.hpp>
#include <volt/io/pcb/manufacturing_evidence.hpp>

namespace volt::python {
namespace py = pybind11;

void bind_manufacturing_evidence(py::module_ &module) {
    PYBIND11_CONSTINIT static py::gil_safe_call_once_and_store<py::object> read_error_type;
    read_error_type.call_once_and_store_result([&] {
        const auto name =
            module.attr("__name__").cast<std::string>() + ".ManufacturingEvidenceReadError";
        auto type = py::reinterpret_steal<py::object>(
            PyErr_NewException(name.c_str(), module.attr("VoltError").ptr(), nullptr));
        if (!type)
            throw py::error_already_set{};
        return type;
    });
    module.attr("ManufacturingEvidenceReadError") = read_error_type.get_stored();
    py::register_exception_translator([](std::exception_ptr failure) {
        if (!failure)
            return;
        try {
            std::rethrow_exception(failure);
        } catch (const io::ManufacturingEvidenceReadError &error) {
            const auto &type = read_error_type.get_stored();
            auto instance = type(error.what());
            instance.attr("code") =
                error.code() == io::ManufacturingEvidenceReadErrorCode::UnsupportedVersion
                    ? "UnsupportedVersion"
                    : "Malformed";
            PyErr_SetObject(type.ptr(), instance.ptr());
        }
    });
    py::enum_<ManufacturingCheck>(module, "ManufacturingCheck")
        .value("ArtifactIdentity", ManufacturingCheck::ArtifactIdentity)
        .value("ProcessSelections", ManufacturingCheck::ProcessSelections)
        .value("ProfileApplicability", ManufacturingCheck::ProfileApplicability)
        .value("LogicalContinuity", ManufacturingCheck::LogicalContinuity)
        .value("RequiredInventory", ManufacturingCheck::RequiredInventory)
        .value("RequiredContacts", ManufacturingCheck::RequiredContacts)
        .value("PhysicalConnectivity", ManufacturingCheck::PhysicalConnectivity)
        .value("UnintendedContacts", ManufacturingCheck::UnintendedContacts)
        .value("PhysicalGeometry", ManufacturingCheck::PhysicalGeometry)
        .value("ExportFidelity", ManufacturingCheck::ExportFidelity)
        .value("CopperWidth", ManufacturingCheck::CopperWidth)
        .value("TrackTrackSpacing", ManufacturingCheck::TrackTrackSpacing)
        .value("TrackPadSpacing", ManufacturingCheck::TrackPadSpacing)
        .value("TrackViaSpacing", ManufacturingCheck::TrackViaSpacing)
        .value("TrackZoneSpacing", ManufacturingCheck::TrackZoneSpacing)
        .value("PadPadSpacing", ManufacturingCheck::PadPadSpacing)
        .value("PadViaSpacing", ManufacturingCheck::PadViaSpacing)
        .value("PadZoneSpacing", ManufacturingCheck::PadZoneSpacing)
        .value("ViaViaSpacing", ManufacturingCheck::ViaViaSpacing)
        .value("ViaZoneSpacing", ManufacturingCheck::ViaZoneSpacing)
        .value("ZoneZoneSpacing", ManufacturingCheck::ZoneZoneSpacing)
        .value("ViaDrill", ManufacturingCheck::ViaDrill)
        .value("ViaCopperDiameter", ManufacturingCheck::ViaCopperDiameter)
        .value("ViaRadialAnnulus", ManufacturingCheck::ViaRadialAnnulus)
        .value("PadPthAnnulus", ManufacturingCheck::PadPthAnnulus)
        .value("PthDrillRange", ManufacturingCheck::PthDrillRange)
        .value("NpthDrillRange", ManufacturingCheck::NpthDrillRange)
        .value("DrillSeparation", ManufacturingCheck::DrillSeparation)
        .value("SlotWidth", ManufacturingCheck::SlotWidth)
        .value("SlotLength", ManufacturingCheck::SlotLength)
        .value("SlotTreatment", ManufacturingCheck::SlotTreatment)
        .value("OutlineClearance", ManufacturingCheck::OutlineClearance)
        .value("MechanicalClearance", ManufacturingCheck::MechanicalClearance)
        .value("DimensionX", ManufacturingCheck::DimensionX)
        .value("DimensionY", ManufacturingCheck::DimensionY)
        .value("CopperLayerCount", ManufacturingCheck::CopperLayerCount)
        .value("BoardThickness", ManufacturingCheck::BoardThickness)
        .value("TopCopperWeight", ManufacturingCheck::TopCopperWeight)
        .value("BottomCopperWeight", ManufacturingCheck::BottomCopperWeight)
        .value("MaterialRealization", ManufacturingCheck::MaterialRealization)
        .value("FinishRealization", ManufacturingCheck::FinishRealization)
        .value("MaskOpenings", ManufacturingCheck::MaskOpenings)
        .value("MaskCopperClearance", ManufacturingCheck::MaskCopperClearance)
        .value("MaskWeb", ManufacturingCheck::MaskWeb)
        .value("MaskSliver", ManufacturingCheck::MaskSliver)
        .value("ViaCovering", ManufacturingCheck::ViaCovering)
        .value("LegendHeight", ManufacturingCheck::LegendHeight)
        .value("LegendStroke", ManufacturingCheck::LegendStroke)
        .value("LegendPadClearance", ManufacturingCheck::LegendPadClearance)
        .value("LegendMaskClearance", ManufacturingCheck::LegendMaskClearance)
        .value("LegendClipping", ManufacturingCheck::LegendClipping)
        .value("RequiredPinOne", ManufacturingCheck::RequiredPinOne)
        .value("RequiredPolarity", ManufacturingCheck::RequiredPolarity)
        .value("PartPackageReview", ManufacturingCheck::PartPackageReview)
        .value("LandPatternReview", ManufacturingCheck::LandPatternReview)
        .value("PinMapReview", ManufacturingCheck::PinMapReview)
        .value("AssemblyOrientation", ManufacturingCheck::AssemblyOrientation)
        .value("PopulatedQuantities", ManufacturingCheck::PopulatedQuantities)
        .value("DnpQuantities", ManufacturingCheck::DnpQuantities)
        .value("SelectedAlternates", ManufacturingCheck::SelectedAlternates)
        .value("CplIdentity", ManufacturingCheck::CplIdentity)
        .value("CplSide", ManufacturingCheck::CplSide)
        .value("CplRotation", ManufacturingCheck::CplRotation)
        .value("SelectedPartRatings", ManufacturingCheck::SelectedPartRatings)
        .value("AssemblyProcess", ManufacturingCheck::AssemblyProcess);
    py::enum_<ManufacturingReviewMethod>(module, "ManufacturingReviewMethod")
        .value("EmittedFabricationInspection",
               ManufacturingReviewMethod::EmittedFabricationInspection)
        .value("IndependentAssemblyInspection",
               ManufacturingReviewMethod::IndependentAssemblyInspection);
    py::enum_<ManufacturingReviewOutcome>(module, "ManufacturingReviewOutcome")
        .value("Pass", ManufacturingReviewOutcome::Pass)
        .value("Fail", ManufacturingReviewOutcome::Fail);
    py::class_<io::ManufacturingReviewAssertion>(module, "ManufacturingReviewAssertion")
        .def_property_readonly(
            "check", [](const io::ManufacturingReviewAssertion &a) { return a.requirement.check; })
        .def_readonly("outcome", &io::ManufacturingReviewAssertion::outcome)
        .def_readonly("reviewer", &io::ManufacturingReviewAssertion::reviewer);
    py::class_<io::ManufacturingEvidenceAttachment>(module, "ManufacturingEvidenceAttachment")
        .def(py::init([](std::string key, const py::bytes &bytes) {
                 return io::ManufacturingEvidenceAttachment{std::move(key),
                                                            bytes.cast<std::string>()};
             }),
             py::arg("key"), py::arg("bytes"))
        .def_readwrite("key", &io::ManufacturingEvidenceAttachment::key)
        .def_property(
            "bytes",
            [](const io::ManufacturingEvidenceAttachment &a) { return py::bytes{a.bytes}; },
            [](io::ManufacturingEvidenceAttachment &a, const py::bytes &b) {
                a.bytes = b.cast<std::string>();
            });
    py::class_<io::PcbFabricationFile>(module, "ManufacturingFabricationFile")
        .def(py::init([](std::string filename, std::string function, const py::bytes &bytes) {
                 return io::PcbFabricationFile{std::move(filename), std::move(function),
                                               bytes.cast<std::string>()};
             }),
             py::arg("filename"), py::arg("function"), py::arg("bytes"))
        .def_readwrite("filename", &io::PcbFabricationFile::filename)
        .def_readwrite("function", &io::PcbFabricationFile::function)
        .def_property(
            "bytes", [](const io::PcbFabricationFile &f) { return py::bytes{f.text}; },
            [](io::PcbFabricationFile &f, const py::bytes &b) { f.text = b.cast<std::string>(); });
    py::class_<io::ManufacturingEvidenceInput>(module, "ManufacturingEvidenceInput")
        .def(py::init<>())
        .def_property(
            "basename", [](const io::ManufacturingEvidenceInput &i) { return i.options.basename; },
            [](io::ManufacturingEvidenceInput &i, std::optional<std::string> b) {
                i.options.basename = std::move(b);
            })
        .def_readwrite("reviewed_files", &io::ManufacturingEvidenceInput::reviewed_files)
        .def_readwrite("assertions", &io::ManufacturingEvidenceInput::assertions)
        .def_readwrite("attachments", &io::ManufacturingEvidenceInput::attachments);
    py::class_<io::ManufacturingEvaluation>(module, "ManufacturingEvaluation")
        .def_property_readonly(
            "status", [](const io::ManufacturingEvaluation &r) { return r.coverage().status(); })
        .def_property_readonly(
            "ready", [](const io::ManufacturingEvaluation &r) { return r.coverage().ready(); })
        .def_property_readonly(
            "binding_digest",
            [](const io::ManufacturingEvaluation &r) { return r.binding().digest().value(); })
        .def_property_readonly(
            "files", [](const io::ManufacturingEvaluation &r) { return r.output().files; })
        .def("to_json", &io::write_manufacturing_evaluation);
    module.def("read_manufacturing_evidence", &io::read_manufacturing_evidence, py::arg("text"));
    module.def(
        "write_manufacturing_evidence",
        [](const std::vector<io::ManufacturingReviewAssertion> &a) {
            return io::write_manufacturing_evidence(a);
        },
        py::arg("assertions"));
    module.def(
        "evaluate_manufacturing_export",
        [](const py::bytes &artifact, const ManufacturingTarget &target,
           const io::ManufacturingEvidenceInput &input) {
            return io::evaluate_manufacturing_export(
                io::open_compiled_board(artifact.cast<std::string>()), target, input);
        },
        py::arg("artifact"), py::arg("target"),
        py::arg("input") = io::ManufacturingEvidenceInput{});
    module.def(
        "make_manufacturing_review",
        [](const py::bytes &artifact, const io::ManufacturingEvaluation &evaluation,
           ManufacturingCheck check, ManufacturingReviewOutcome outcome, std::string reviewer,
           std::string source, std::string source_key, std::string criterion,
           std::optional<double> minimum_mm, std::string observation, std::string rationale,
           std::string evidence_key, std::string evidence_digest) {
            const auto compiled = io::open_compiled_board(artifact.cast<std::string>());
            const auto requirement = manufacturing_review_requirement(compiled, check);
            if (!requirement || compiled.identity() != evaluation.binding().source ||
                compiled.content_digest() != evaluation.binding().content_digest)
                throw KernelArgumentError{
                    ErrorCode::InvalidArgument,
                    "Review requires an eligible leaf and exact evaluation artifact"};
            auto result = io::ManufacturingReviewAssertion{evaluation.binding(),
                                                           *requirement,
                                                           outcome,
                                                           std::move(reviewer),
                                                           std::move(source),
                                                           std::move(source_key),
                                                           std::move(criterion),
                                                           minimum_mm,
                                                           std::move(observation),
                                                           std::move(rationale),
                                                           std::move(evidence_key),
                                                           ContentHash{std::move(evidence_digest)}};
            (void)io::write_manufacturing_evidence(std::span{&result, std::size_t{1}});
            return result;
        },
        py::arg("artifact"), py::arg("evaluation"), py::arg("check"), py::arg("outcome"),
        py::arg("reviewer"), py::arg("source"), py::arg("source_key"), py::arg("criterion"),
        py::arg("minimum_mm"), py::arg("observation"), py::arg("rationale"),
        py::arg("evidence_key"), py::arg("evidence_digest"));
}
} // namespace volt::python
