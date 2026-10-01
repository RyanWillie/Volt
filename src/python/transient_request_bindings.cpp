#include "transient_request_bindings.hpp"
#include "binding_diagnostic_conversions.hpp"
#include <pybind11/stl.h>
#include <volt/core/errors.hpp>
#include <volt/electrical/dc_solve.hpp>
#include <volt/electrical/transient_request.hpp>
#include <volt/io/electrical/transient_request_io.hpp>

namespace volt::python {
namespace {
template <typename Key> using KeyInput = std::variant<std::string, Key>;

template <typename Key> Key key_value(KeyInput<Key> input) {
    return std::visit([](auto value) { return Key{std::move(value)}; }, std::move(input));
}

template <typename Value> py::tuple copied_tuple(const std::vector<Value> &values) {
    py::tuple result{values.size()};
    for (std::size_t index = 0; index < values.size(); ++index)
        result[index] = py::cast(values[index], py::return_value_policy::copy);
    return result;
}

py::list diagnostic_list(const std::vector<Diagnostic> &values) {
    py::list result;
    for (const auto &value : values)
        result.append(diagnostic_to_dict(value));
    return result;
}

std::vector<TransientSource> request_sources(const py::iterable &values) {
    std::vector<TransientSource> result;
    for (const auto &value : values) {
        if (py::isinstance<TransientVoltageSource>(value))
            result.emplace_back(value.cast<TransientVoltageSource>());
        else if (py::isinstance<TransientCurrentSource>(value))
            result.emplace_back(value.cast<TransientCurrentSource>());
        else
            throw py::type_error{"Transient request sources contain an unsupported value"};
    }
    return result;
}

std::vector<DcProbe> request_probes(const py::iterable &values) {
    std::vector<DcProbe> result;
    for (const auto &value : values) {
        if (py::isinstance<DcVoltageProbe>(value))
            result.emplace_back(value.cast<DcVoltageProbe>());
        else if (py::isinstance<DcSourceCurrentProbe>(value))
            result.emplace_back(value.cast<DcSourceCurrentProbe>());
        else if (py::isinstance<DcModelElementCurrentProbe>(value))
            result.emplace_back(value.cast<DcModelElementCurrentProbe>());
        else
            throw py::type_error{"Transient request probes contain an unsupported value"};
    }
    return result;
}
} // namespace

void bind_transient_request(py::module_ &module) {
    py::class_<TransientTimeGrid>(module, "TransientTimeGrid")
        .def(py::init<std::vector<Quantity>>(), py::arg("times"))
        .def_static("uniform", &TransientTimeGrid::uniform, py::arg("horizon"), py::arg("count"))
        .def_property_readonly(
            "times", [](const TransientTimeGrid &grid) { return copied_tuple(grid.times()); })
        .def_property_readonly("horizon", &TransientTimeGrid::horizon,
                               py::return_value_policy::copy);
    py::class_<TransientWaveformKnot>(module, "TransientWaveformKnot")
        .def(py::init(
                 [](Quantity time, Quantity value) { return TransientWaveformKnot{time, value}; }),
             py::arg("time"), py::arg("value"))
        .def_property_readonly("time", [](const TransientWaveformKnot &knot) { return knot.time; })
        .def_property_readonly("value",
                               [](const TransientWaveformKnot &knot) { return knot.value; });
    py::class_<TransientWaveform>(module, "TransientWaveform")
        .def(py::init<Quantity>(), py::arg("constant"))
        .def(py::init<std::vector<TransientWaveformKnot>>(), py::arg("knots"))
        .def_property_readonly("constant", &TransientWaveform::constant)
        .def_property_readonly("dimension", &TransientWaveform::dimension)
        .def_property_readonly(
            "knots",
            [](const TransientWaveform &waveform) { return copied_tuple(waveform.knots()); })
        .def("value_at", &TransientWaveform::value_at, py::arg("time"));
    py::class_<TransientVoltageSource>(module, "_TransientVoltageSource")
        .def(py::init([](KeyInput<ElectricalSourceKey> key, ElectricalNetPair nets,
                         TransientWaveform waveform) {
                 return TransientVoltageSource{key_value(std::move(key)), std::move(nets),
                                               std::move(waveform)};
             }),
             py::arg("key"), py::arg("nets"), py::arg("waveform"))
        .def_property_readonly("key", &TransientVoltageSource::key, py::return_value_policy::copy)
        .def_property_readonly("nets", &TransientVoltageSource::nets, py::return_value_policy::copy)
        .def_property_readonly("waveform", &TransientVoltageSource::waveform,
                               py::return_value_policy::copy);
    py::class_<TransientCurrentSource>(module, "_TransientCurrentSource")
        .def(py::init([](KeyInput<ElectricalSourceKey> key, ElectricalNetPair nets,
                         TransientWaveform waveform) {
                 return TransientCurrentSource{key_value(std::move(key)), std::move(nets),
                                               std::move(waveform)};
             }),
             py::arg("key"), py::arg("nets"), py::arg("waveform"))
        .def_property_readonly("key", &TransientCurrentSource::key, py::return_value_policy::copy)
        .def_property_readonly("nets", &TransientCurrentSource::nets, py::return_value_policy::copy)
        .def_property_readonly("waveform", &TransientCurrentSource::waveform,
                               py::return_value_policy::copy);
    py::enum_<TransientStorageKind>(module, "TransientStorageKind")
        .value("CAPACITOR_VOLTAGE", TransientStorageKind::CapacitorVoltage)
        .value("INDUCTOR_CURRENT", TransientStorageKind::InductorCurrent);
    py::class_<TransientDcProvenance>(module, "TransientDcProvenance")
        .def_property_readonly("input",
                               [](const TransientDcProvenance &value) { return value.input; })
        .def_property_readonly(
            "analysis_identity",
            [](const TransientDcProvenance &value) { return value.analysis_identity; })
        .def_property_readonly(
            "participation_identity",
            [](const TransientDcProvenance &value) { return value.participation_identity; })
        .def_property_readonly("storage_count", [](const TransientDcProvenance &value) {
            return value.storage_count;
        });
    py::class_<TransientInitialState>(module, "TransientInitialState")
        .def(py::init([](const ElectricalInput &input, ElectricalOccurrenceRef occurrence,
                         std::string element, TransientStorageKind kind, Quantity value) {
                 if (occurrence.input() != input.identity())
                     throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                               "Initial state occurrence belongs to another input"};
                 const auto &selected =
                     input.circuit().get(occurrence.id()).selected_library_part_ref();
                 if (!selected)
                     throw KernelArgumentError{ErrorCode::InvalidArgument,
                                               "Initial state occurrence has no selected Part"};
                 return TransientInitialState{std::move(occurrence), *selected,
                                              ModelElementKey{std::move(element)}, kind, value};
             }),
             py::arg("input"), py::arg("occurrence"), py::arg("element"), py::arg("kind"),
             py::arg("value"))
        .def_property_readonly("occurrence", &TransientInitialState::occurrence,
                               py::return_value_policy::copy)
        .def_property_readonly("part",
                               [](const TransientInitialState &state) {
                                   const auto &part = state.part();
                                   py::dict value;
                                   value["library_namespace"] = part.library_namespace();
                                   value["library_version"] = part.library_version();
                                   value["part_key"] = part.part_key().value();
                                   value["library_bundle_digest"] = part.library_digest().value();
                                   value["part_digest"] = part.part_digest().value();
                                   return value;
                               })
        .def_property_readonly(
            "element", [](const TransientInitialState &state) { return state.element().value(); })
        .def_property_readonly("kind", &TransientInitialState::kind)
        .def_property_readonly("value", &TransientInitialState::value,
                               py::return_value_policy::copy);
    py::class_<TransientInitialConditions>(module, "TransientInitialConditions")
        .def(py::init<std::vector<TransientInitialState>>(), py::arg("entries") = py::tuple{})
        .def_property_readonly(
            "entries",
            [](const TransientInitialConditions &state) { return copied_tuple(state.entries()); })
        .def_property_readonly("dc_provenance", &TransientInitialConditions::dc_provenance,
                               py::return_value_policy::copy);
    py::class_<TransientRequest>(module, "TransientRequest")
        .def(py::init([](KeyInput<ElectricalRequestKey> key, const ElectricalInput &input,
                         TransientTimeGrid grid, std::optional<ElectricalNetRef> reference,
                         const py::iterable &sources, const py::iterable &probes,
                         std::vector<DcOccurrenceExclusion> exclusions,
                         const py::object &initial_state) {
                 return TransientRequest{
                     key_value(std::move(key)),
                     input,
                     std::move(reference),
                     std::move(grid),
                     request_sources(sources),
                     request_probes(probes),
                     std::move(exclusions),
                     py::isinstance<TransientInitialConditions>(initial_state)
                         ? initial_state.cast<TransientInitialConditions>()
                         : TransientInitialConditions{
                               initial_state.cast<std::vector<TransientInitialState>>()}};
             }),
             py::arg("key"), py::arg("input"), py::arg("grid"), py::arg("reference") = py::none(),
             py::arg("sources") = py::tuple{}, py::arg("probes") = py::tuple{},
             py::arg("exclusions") = py::tuple{}, py::arg("initial_state") = py::tuple{})
        .def_property_readonly("key", &TransientRequest::key, py::return_value_policy::copy)
        .def_property_readonly("input", &TransientRequest::input, py::return_value_policy::copy)
        .def_property_readonly("reference", &TransientRequest::reference,
                               py::return_value_policy::copy)
        .def_property_readonly("grid", &TransientRequest::grid, py::return_value_policy::copy)
        .def_property_readonly(
            "times", [](const TransientRequest &request) { return copied_tuple(request.times()); })
        .def_property_readonly(
            "sources",
            [](const TransientRequest &request) { return copied_tuple(request.sources()); })
        .def_property_readonly(
            "probes",
            [](const TransientRequest &request) { return copied_tuple(request.probes()); })
        .def_property_readonly(
            "exclusions",
            [](const TransientRequest &request) { return copied_tuple(request.exclusions()); })
        .def_property_readonly(
            "initial_state",
            [](const TransientRequest &request) { return copied_tuple(request.initial_state()); })
        .def_property_readonly("initial_conditions", &TransientRequest::initial_conditions,
                               py::return_value_policy::copy)
        .def("to_json", &io::write_transient_request)
        .def_static(
            "from_json",
            [](const ElectricalInput &input, const py::handle &data) {
                if (!py::isinstance<py::bytes>(data) && !py::isinstance<py::str>(data))
                    throw py::type_error{"Transient request JSON must be bytes or str"};
                return io::read_transient_request(data.cast<std::string>(), input);
            },
            py::arg("input"), py::arg("data"))
        .def("assess", &assess_transient_request);
    py::class_<TransientRequestAssessment>(module, "TransientRequestAssessment")
        .def_property_readonly("complete", &TransientRequestAssessment::complete)
        .def_property_readonly("input", &TransientRequestAssessment::input,
                               py::return_value_policy::copy)
        .def_property_readonly(
            "coverage",
            [](const TransientRequestAssessment &value) { return copied_tuple(value.coverage()); })
        .def_property_readonly("diagnostics", [](const TransientRequestAssessment &value) {
            return diagnostic_list(value.diagnostics());
        });
    module.def("assess_transient_request", &assess_transient_request, py::arg("request"));
    module.def(
        "initial_state_from",
        [](const DcSolution &solution) { return initial_state_from(solution); },
        py::arg("solution"));
    module.def(
        "initial_state_from",
        [](const DcSolveReport &report) { return initial_state_from(report); }, py::arg("report"));
}
} // namespace volt::python
