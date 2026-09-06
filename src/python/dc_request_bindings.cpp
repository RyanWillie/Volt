#include "dc_request_bindings.hpp"

#include "binding_diagnostic_conversions.hpp"
#include "py_circuit.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <pybind11/operators.h>
#include <pybind11/stl.h>

#include <volt/core/errors.hpp>
#include <volt/electrical/dc_request.hpp>
#include <volt/io/electrical/dc_request_io.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/parts/part_library_bundle.hpp>

namespace volt::python {
namespace {

template <typename Key> using KeyInput = std::variant<std::string, Key>;

template <typename Key> [[nodiscard]] Key key_value(KeyInput<Key> input) {
    return std::visit([](auto value) { return Key{std::move(value)}; }, std::move(input));
}

template <typename Key> void bind_key(py::class_<Key> binding) {
    binding.def(py::init<std::string>(), py::arg("value"))
        .def_property_readonly("value", &Key::value)
        .def("__str__", &Key::value)
        .def("__hash__", [](const Key &value) { return py::hash(py::str{value.value()}); })
        .def(py::self == py::self);
}

template <typename Value> [[nodiscard]] py::tuple copied_tuple(const std::vector<Value> &values) {
    auto result = py::tuple{values.size()};
    for (std::size_t index = 0; index < values.size(); ++index) {
        result[index] = py::cast(values[index], py::return_value_policy::copy);
    }
    return result;
}

[[nodiscard]] const PyCircuit &authoring_circuit(const py::handle &handle, const char *type_name) {
    const auto logical = py::module_::import("volt.logical");
    if (!py::isinstance(handle, logical.attr(type_name))) {
        throw py::type_error{std::string{"Expected a Volt "} + type_name + " handle"};
    }
    return handle.attr("_design").attr("_circuit").cast<const PyCircuit &>();
}

void require_logical_identity(const DcInput &input, const PyCircuit &circuit) {
    const auto actual = sha256_content_hash(io::write_logical_circuit(circuit.logical_circuit()));
    if (actual != input.identity().logical()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "DC input handle belongs to another logical circuit"};
    }
}

[[nodiscard]] DcNetRef input_net(const DcInput &input, const py::handle &handle) {
    const auto &circuit = authoring_circuit(handle, "Net");
    require_logical_identity(input, circuit);
    return input.net(NetId{handle.attr("index").cast<std::size_t>()});
}

[[nodiscard]] DcOccurrenceRef input_occurrence(const DcInput &input, const py::handle &handle) {
    const auto &circuit = authoring_circuit(handle, "Component");
    require_logical_identity(input, circuit);
    return input.occurrence(ComponentId{handle.attr("index").cast<std::size_t>()});
}

[[nodiscard]] py::object selected_part(const std::optional<LibraryPartRef> &reference) {
    if (!reference.has_value()) {
        return py::none{};
    }
    auto result = py::dict{};
    result["library_namespace"] = reference->library_namespace();
    result["library_version"] = reference->library_version();
    result["part_key"] = reference->part_key().value();
    result["library_bundle_digest"] = reference->library_digest().value();
    result["part_digest"] = reference->part_digest().value();
    return std::move(result);
}

[[nodiscard]] std::string request_bytes(const py::handle &value) {
    if (py::isinstance<py::bytes>(value) || py::isinstance<py::str>(value)) {
        return value.cast<std::string>();
    }
    throw py::type_error{"DC request JSON must be bytes or str"};
}

[[nodiscard]] std::vector<DcSource> request_sources(const py::iterable &values) {
    auto result = std::vector<DcSource>{};
    for (const auto &value : values) {
        if (py::isinstance<DcVoltageSource>(value)) {
            result.emplace_back(value.cast<DcVoltageSource>());
        } else if (py::isinstance<DcCurrentSource>(value)) {
            result.emplace_back(value.cast<DcCurrentSource>());
        } else {
            throw py::type_error{"DC request sources contain an unsupported value"};
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
            throw py::type_error{"DC request probes contain an unsupported value"};
        }
    }
    return result;
}

} // namespace

void bind_dc_request(pybind11::module_ &module) {
    module.def("analysis_diagnostic_codes", []() {
        const auto &catalog = diagnostic_code_catalogs::Analysis;
        auto result = py::tuple{catalog.size()};
        for (std::size_t index = 0; index < catalog.size(); ++index) {
            result[index] = py::str{catalog[index]};
        }
        return result;
    });
    bind_key(py::class_<DcRequestKey>(module, "DcRequestKey"));
    bind_key(py::class_<DcSourceKey>(module, "DcSourceKey"));
    bind_key(py::class_<DcProbeKey>(module, "DcProbeKey"));

    py::class_<DcInputIdentity>(module, "DcInputIdentity")
        .def_property_readonly("logical", &DcInputIdentity::logical, py::return_value_policy::copy)
        .def_property_readonly("selected_parts", &DcInputIdentity::selected_parts,
                               py::return_value_policy::copy)
        .def(py::self == py::self);
    py::class_<DcNetRef>(module, "DcNetRef")
        .def_property_readonly("index", [](const DcNetRef &value) { return value.id().index(); })
        .def_property_readonly("input", &DcNetRef::input, py::return_value_policy::copy)
        .def(py::self == py::self);
    py::class_<DcOccurrenceRef>(module, "DcOccurrenceRef")
        .def_property_readonly("index",
                               [](const DcOccurrenceRef &value) { return value.id().index(); })
        .def_property_readonly("input", &DcOccurrenceRef::input, py::return_value_policy::copy)
        .def(py::self == py::self);
    py::class_<DcInput>(module, "DcInput")
        .def_property_readonly(
            "identity", [](const DcInput &input) { return input.identity(); },
            py::return_value_policy::copy)
        .def_property_readonly("nets",
                               [](const DcInput &input) {
                                   auto result = py::tuple{input.circuit().all<NetId>().size()};
                                   for (std::size_t index = 0; index < py::len(result); ++index) {
                                       result[index] = py::cast(input.net(NetId{index}),
                                                                py::return_value_policy::copy);
                                   }
                                   return result;
                               })
        .def_property_readonly("occurrences",
                               [](const DcInput &input) {
                                   auto result =
                                       py::tuple{input.circuit().all<ComponentId>().size()};
                                   for (std::size_t index = 0; index < py::len(result); ++index) {
                                       result[index] =
                                           py::cast(input.occurrence(ComponentId{index}),
                                                    py::return_value_policy::copy);
                                   }
                                   return result;
                               })
        .def("net", &input_net, py::arg("net"))
        .def("occurrence", &input_occurrence, py::arg("component"));
    module.def(
        "prepare_dc_input",
        [](const PyCircuit &circuit) {
            return io::prepare_dc_input(circuit.logical_circuit(), circuit.selected_part_bundle());
        },
        py::arg("circuit"));

    py::class_<DcNetPair>(module, "DcNetPair")
        .def(py::init<DcNetRef, DcNetRef>(), py::arg("from_"), py::arg("to"))
        .def_property_readonly("from_", &DcNetPair::from, py::return_value_policy::copy)
        .def_property_readonly("to", &DcNetPair::to, py::return_value_policy::copy)
        .def(py::self == py::self);
    py::class_<DcVoltageSource>(module, "_DcVoltageSource")
        .def(py::init([](KeyInput<DcSourceKey> key, DcNetPair nets, Quantity value) {
                 return DcVoltageSource{key_value(std::move(key)), std::move(nets), value};
             }),
             py::arg("key"), py::arg("nets"), py::arg("value"))
        .def_property_readonly("key", &DcVoltageSource::key, py::return_value_policy::copy)
        .def_property_readonly("nets", &DcVoltageSource::nets, py::return_value_policy::copy)
        .def_property_readonly("value", &DcVoltageSource::value, py::return_value_policy::copy);
    py::class_<DcCurrentSource>(module, "_DcCurrentSource")
        .def(py::init([](KeyInput<DcSourceKey> key, DcNetPair nets, Quantity value) {
                 return DcCurrentSource{key_value(std::move(key)), std::move(nets), value};
             }),
             py::arg("key"), py::arg("nets"), py::arg("value"))
        .def_property_readonly("key", &DcCurrentSource::key, py::return_value_policy::copy)
        .def_property_readonly("nets", &DcCurrentSource::nets, py::return_value_policy::copy)
        .def_property_readonly("value", &DcCurrentSource::value, py::return_value_policy::copy);
    py::class_<DcVoltageProbe>(module, "_DcVoltageProbe")
        .def(py::init([](KeyInput<DcProbeKey> key, DcNetPair nets) {
                 return DcVoltageProbe{key_value(std::move(key)), std::move(nets)};
             }),
             py::arg("key"), py::arg("nets"))
        .def_property_readonly("key", &DcVoltageProbe::key, py::return_value_policy::copy)
        .def_property_readonly("nets", &DcVoltageProbe::nets, py::return_value_policy::copy);
    py::class_<DcSourceCurrentProbe>(module, "DcSourceCurrentProbe")
        .def(py::init([](KeyInput<DcProbeKey> key, KeyInput<DcSourceKey> source) {
                 return DcSourceCurrentProbe{key_value(std::move(key)),
                                             key_value(std::move(source))};
             }),
             py::arg("key"), py::arg("source"))
        .def_property_readonly("key", &DcSourceCurrentProbe::key, py::return_value_policy::copy)
        .def_property_readonly("source", &DcSourceCurrentProbe::source,
                               py::return_value_policy::copy);
    py::class_<DcModelElementCurrentProbe>(module, "DcModelElementCurrentProbe")
        .def(py::init([](KeyInput<DcProbeKey> key, DcOccurrenceRef occurrence,
                         KeyInput<ModelElementKey> element) {
                 return DcModelElementCurrentProbe{key_value(std::move(key)), std::move(occurrence),
                                                   key_value(std::move(element))};
             }),
             py::arg("key"), py::arg("occurrence"), py::arg("element"))
        .def_property_readonly("key", &DcModelElementCurrentProbe::key,
                               py::return_value_policy::copy)
        .def_property_readonly("occurrence", &DcModelElementCurrentProbe::occurrence,
                               py::return_value_policy::copy)
        .def_property_readonly("element", &DcModelElementCurrentProbe::element,
                               py::return_value_policy::copy);

    py::class_<DcNonElectricalExclusion>(module, "DcNonElectricalExclusion")
        .def(py::init<>())
        .def(py::self == py::self);
    py::class_<DcOutsideAnalysisExclusion>(module, "DcOutsideAnalysisExclusion")
        .def(py::init<>())
        .def(py::self == py::self);
    py::class_<DcReplacedByStimulusExclusion>(module, "DcReplacedByStimulusExclusion")
        .def(py::init([](const std::vector<KeyInput<DcSourceKey>> &sources) {
                 auto keys = std::vector<DcSourceKey>{};
                 keys.reserve(sources.size());
                 for (const auto &source : sources) {
                     keys.push_back(key_value(source));
                 }
                 return DcReplacedByStimulusExclusion{std::move(keys)};
             }),
             py::arg("sources"))
        .def_property_readonly("sources", [](const DcReplacedByStimulusExclusion &value) {
            return copied_tuple(value.sources());
        });
    py::class_<DcOccurrenceExclusion>(module, "DcOccurrenceExclusion")
        .def(py::init<DcOccurrenceRef, DcExclusionReason>(), py::arg("occurrence"),
             py::arg("reason"))
        .def_property_readonly("occurrence", &DcOccurrenceExclusion::occurrence,
                               py::return_value_policy::copy)
        .def_property_readonly("reason", &DcOccurrenceExclusion::reason,
                               py::return_value_policy::copy);

    py::class_<DcRequest>(module, "DcRequest")
        .def(
            py::init([](KeyInput<DcRequestKey> key, const DcInput &input,
                        std::optional<DcNetRef> reference, const py::iterable &sources,
                        const py::iterable &probes, std::vector<DcOccurrenceExclusion> exclusions) {
                return DcRequest{key_value(std::move(key)), input,
                                 std::move(reference),      request_sources(sources),
                                 request_probes(probes),    std::move(exclusions)};
            }),
            py::arg("key"), py::arg("input"), py::arg("reference") = py::none(),
            py::arg("sources") = py::tuple{}, py::arg("probes") = py::tuple{},
            py::arg("exclusions") = py::tuple{})
        .def_property_readonly("key", &DcRequest::key, py::return_value_policy::copy)
        .def_property_readonly("input", &DcRequest::input, py::return_value_policy::copy)
        .def_property_readonly("reference", &DcRequest::reference, py::return_value_policy::copy)
        .def_property_readonly("sources",
                               [](const DcRequest &value) { return copied_tuple(value.sources()); })
        .def_property_readonly("probes",
                               [](const DcRequest &value) { return copied_tuple(value.probes()); })
        .def_property_readonly(
            "exclusions", [](const DcRequest &value) { return copied_tuple(value.exclusions()); })
        .def("to_json", &io::write_dc_request)
        .def_static(
            "from_json",
            [](const DcInput &input, const py::handle &value) {
                return io::read_dc_request(request_bytes(value), input);
            },
            py::arg("input"), py::arg("data"))
        .def("assess", &assess_dc_request);

    py::enum_<DcCoverageStatus>(module, "DcCoverageStatus")
        .value("SUPPORTED", DcCoverageStatus::Supported)
        .value("EXCLUDED", DcCoverageStatus::Excluded)
        .value("UNSELECTED", DcCoverageStatus::Unselected)
        .value("UNRESOLVED", DcCoverageStatus::Unresolved)
        .value("MODEL_ABSENT", DcCoverageStatus::ModelAbsent)
        .value("UNSUPPORTED", DcCoverageStatus::Unsupported);
    py::class_<DcOccurrenceCoverage>(module, "DcOccurrenceCoverage")
        .def_property_readonly("occurrence", &DcOccurrenceCoverage::occurrence,
                               py::return_value_policy::copy)
        .def_property_readonly("status", &DcOccurrenceCoverage::status)
        .def_property_readonly(
            "selected_part",
            [](const DcOccurrenceCoverage &value) { return selected_part(value.selected_part()); })
        .def_property_readonly("exclusion", &DcOccurrenceCoverage::exclusion,
                               py::return_value_policy::copy);
    py::class_<DcRequestAssessment>(module, "DcRequestAssessment")
        .def_property_readonly("complete", &DcRequestAssessment::complete)
        .def_property_readonly("input", &DcRequestAssessment::input, py::return_value_policy::copy)
        .def_property_readonly(
            "coverage",
            [](const DcRequestAssessment &value) { return copied_tuple(value.coverage()); })
        .def_property_readonly("diagnostics", [](const DcRequestAssessment &value) {
            auto result = py::list{};
            for (const auto &diagnostic : value.diagnostics()) {
                result.append(diagnostic_to_dict(diagnostic));
            }
            return result;
        });
    module.def("assess_dc_request", &assess_dc_request, py::arg("request"));
}

} // namespace volt::python
