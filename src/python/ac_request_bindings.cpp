#include "ac_request_bindings.hpp"
#include "binding_diagnostic_conversions.hpp"
#include <pybind11/complex.h>
#include <pybind11/stl.h>
#include <volt/electrical/ac_request.hpp>
#include <volt/io/electrical/ac_request_io.hpp>

namespace volt::python {
namespace {
template <typename Key> using KeyInput = std::variant<std::string, Key>;

template <typename Key> [[nodiscard]] Key key_value(KeyInput<Key> input) {
    return std::visit([](auto value) { return Key{std::move(value)}; }, std::move(input));
}

template <typename Value> [[nodiscard]] py::tuple copied_tuple(const std::vector<Value> &values) {
    auto result = py::tuple{values.size()};
    for (std::size_t index = 0; index < values.size(); ++index) {
        result[index] = py::cast(values[index], py::return_value_policy::copy);
    }
    return result;
}

[[nodiscard]] std::string request_bytes(const py::handle &value) {
    if (py::isinstance<py::bytes>(value) || py::isinstance<py::str>(value)) {
        return value.cast<std::string>();
    }
    throw py::type_error{"AC request JSON must be bytes or str"};
}

[[nodiscard]] std::vector<AcSource> request_sources(const py::iterable &values) {
    auto result = std::vector<AcSource>{};
    for (const auto &value : values) {
        if (py::isinstance<AcVoltageSource>(value)) {
            result.emplace_back(value.cast<AcVoltageSource>());
        } else if (py::isinstance<AcCurrentSource>(value)) {
            result.emplace_back(value.cast<AcCurrentSource>());
        } else {
            throw py::type_error{"AC request sources contain an unsupported value"};
        }
    }
    return result;
}

[[nodiscard]] std::vector<DcProbe> request_probes(const py::iterable &values) {
    auto result = std::vector<DcProbe>{};
    for (const auto &value : values) {
        if (py::isinstance<DcVoltageProbe>(value)) {
            result.emplace_back(value.cast<DcVoltageProbe>());
        } else if (py::isinstance<DcSourceCurrentProbe>(value)) {
            result.emplace_back(value.cast<DcSourceCurrentProbe>());
        } else if (py::isinstance<DcModelElementCurrentProbe>(value)) {
            result.emplace_back(value.cast<DcModelElementCurrentProbe>());
        } else {
            throw py::type_error{"AC request probes contain an unsupported value"};
        }
    }
    return result;
}

} // namespace

void bind_ac_request(pybind11::module_ &module) {
    py::class_<AcFrequencySweep>(module, "AcFrequencySweep")
        .def(py::init<std::vector<Quantity>>(), py::arg("frequencies"))
        .def_static("linear", &AcFrequencySweep::linear, py::arg("start"), py::arg("stop"),
                    py::arg("count"))
        .def_static("logarithmic", &AcFrequencySweep::logarithmic, py::arg("start"),
                    py::arg("stop"), py::arg("count"))
        .def_property_readonly(
            "frequencies", [](const AcFrequencySweep &s) { return copied_tuple(s.frequencies()); });
    py::class_<AcVoltageSource>(module, "_AcVoltageSource")
        .def(py::init(
                 [](KeyInput<DcSourceKey> key, DcNetPair nets, Quantity amplitude, double phase) {
                     return AcVoltageSource{key_value(std::move(key)), std::move(nets), amplitude,
                                            phase};
                 }),
             py::arg("key"), py::arg("nets"), py::arg("amplitude"), py::arg("phase") = 0.0)
        .def_property_readonly("key", &AcVoltageSource::key, py::return_value_policy::copy)
        .def_property_readonly("nets", &AcVoltageSource::nets, py::return_value_policy::copy)
        .def_property_readonly("amplitude", &AcVoltageSource::amplitude,
                               py::return_value_policy::copy)
        .def_property_readonly("phase", &AcVoltageSource::phase)
        .def_property_readonly("phasor", &AcVoltageSource::phasor);
    py::class_<AcCurrentSource>(module, "_AcCurrentSource")
        .def(py::init(
                 [](KeyInput<DcSourceKey> key, DcNetPair nets, Quantity amplitude, double phase) {
                     return AcCurrentSource{key_value(std::move(key)), std::move(nets), amplitude,
                                            phase};
                 }),
             py::arg("key"), py::arg("nets"), py::arg("amplitude"), py::arg("phase") = 0.0)
        .def_property_readonly("key", &AcCurrentSource::key, py::return_value_policy::copy)
        .def_property_readonly("nets", &AcCurrentSource::nets, py::return_value_policy::copy)
        .def_property_readonly("amplitude", &AcCurrentSource::amplitude,
                               py::return_value_policy::copy)
        .def_property_readonly("phase", &AcCurrentSource::phase)
        .def_property_readonly("phasor", &AcCurrentSource::phasor);
    py::class_<AcGainProbe>(module, "AcGainProbe")
        .def(py::init([](KeyInput<DcProbeKey> key, KeyInput<DcProbeKey> numerator,
                         KeyInput<DcProbeKey> denominator) {
                 return AcGainProbe{key_value(std::move(key)), key_value(std::move(numerator)),
                                    key_value(std::move(denominator))};
             }),
             py::arg("key"), py::arg("numerator"), py::arg("denominator"))
        .def_property_readonly("key", &AcGainProbe::key, py::return_value_policy::copy)
        .def_property_readonly("numerator", &AcGainProbe::numerator, py::return_value_policy::copy)
        .def_property_readonly("denominator", &AcGainProbe::denominator,
                               py::return_value_policy::copy);
    py::class_<AcImpedanceProbe>(module, "_AcImpedanceProbe")
        .def(py::init([](KeyInput<DcProbeKey> key, DcNetPair nets, KeyInput<DcSourceKey> source) {
                 return AcImpedanceProbe{key_value(std::move(key)), std::move(nets),
                                         key_value(std::move(source))};
             }),
             py::arg("key"), py::arg("nets"), py::arg("source"))
        .def_property_readonly("key", &AcImpedanceProbe::key, py::return_value_policy::copy)
        .def_property_readonly("nets", &AcImpedanceProbe::nets, py::return_value_policy::copy)
        .def_property_readonly("source", &AcImpedanceProbe::source, py::return_value_policy::copy);
    py::class_<AcRequest>(module, "AcRequest")
        .def(py::init([](KeyInput<DcRequestKey> key, const DcInput &input, AcFrequencySweep sweep,
                         std::optional<DcNetRef> reference, const py::iterable &sources,
                         const py::iterable &probes, std::vector<DcOccurrenceExclusion> exclusions,
                         std::vector<AcGainProbe> gains, std::vector<AcImpedanceProbe> impedances) {
                 return AcRequest{key_value(std::move(key)), input,
                                  std::move(reference),      std::move(sweep),
                                  request_sources(sources),  request_probes(probes),
                                  std::move(exclusions),     std::move(gains),
                                  std::move(impedances)};
             }),
             py::arg("key"), py::arg("input"), py::arg("sweep"), py::arg("reference") = py::none(),
             py::arg("sources") = py::tuple{}, py::arg("probes") = py::tuple{},
             py::arg("exclusions") = py::tuple{}, py::arg("gains") = py::tuple{},
             py::arg("impedances") = py::tuple{})
        .def_property_readonly("key", &AcRequest::key, py::return_value_policy::copy)
        .def_property_readonly("input", &AcRequest::input, py::return_value_policy::copy)
        .def_property_readonly("reference", &AcRequest::reference, py::return_value_policy::copy)
        .def_property_readonly("sweep", &AcRequest::sweep, py::return_value_policy::copy)
        .def_property_readonly("frequencies",
                               [](const AcRequest &r) { return copied_tuple(r.frequencies()); })
        .def_property_readonly("sources",
                               [](const AcRequest &r) { return copied_tuple(r.sources()); })
        .def_property_readonly("probes",
                               [](const AcRequest &r) { return copied_tuple(r.probes()); })
        .def_property_readonly("exclusions",
                               [](const AcRequest &r) { return copied_tuple(r.exclusions()); })
        .def_property_readonly("gains", [](const AcRequest &r) { return copied_tuple(r.gains()); })
        .def_property_readonly("impedances",
                               [](const AcRequest &r) { return copied_tuple(r.impedances()); })
        .def("to_json", &io::write_ac_request)
        .def_static(
            "from_json",
            [](const DcInput &input, const py::handle &data) {
                return io::read_ac_request(request_bytes(data), input);
            },
            py::arg("input"), py::arg("data"))
        .def("assess", &assess_ac_request);
    py::class_<AcRequestAssessment>(module, "AcRequestAssessment")
        .def_property_readonly("complete", &AcRequestAssessment::complete)
        .def_property_readonly("input", &AcRequestAssessment::input, py::return_value_policy::copy)
        .def_property_readonly(
            "coverage", [](const AcRequestAssessment &a) { return copied_tuple(a.coverage()); })
        .def_property_readonly("diagnostics", [](const AcRequestAssessment &a) {
            py::list result;
            for (const auto &d : a.diagnostics())
                result.append(diagnostic_to_dict(d));
            return result;
        });
    module.def("assess_ac_request", &assess_ac_request, py::arg("request"));
}
} // namespace volt::python
