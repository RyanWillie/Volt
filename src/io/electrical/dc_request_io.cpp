#include <volt/io/electrical/dc_request_io.hpp>

#include <algorithm>
#include <initializer_list>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/detail/typed_id.hpp>

namespace volt::io {
namespace {

using Json = nlohmann::ordered_json;

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "DC request: " + message};
    }
}

void fields(const Json &value, std::initializer_list<std::string_view> keys) {
    require(value.is_object() && value.size() == keys.size(), "unexpected object fields");
    for (const auto key : keys) {
        require(value.contains(key), "missing field " + std::string{key});
    }
}

std::string text(const Json &value) {
    require(value.is_string(), "expected a string");
    return value.get<std::string>();
}

const Json &array(const Json &value) {
    require(value.is_array(), "expected an array");
    return value;
}

template <typename Id> Id local_id(const Json &value) {
    const auto spelling = text(value);
    const auto result = detail::decode_local_id<Id>(spelling);
    require(detail::encode_local_id(result) == spelling, "noncanonical local identity");
    return result;
}

Json nets_json(const DcNetPair &nets) {
    return Json{{"from", detail::encode_local_id(nets.from().id())},
                {"to", detail::encode_local_id(nets.to().id())}};
}

DcNetPair nets_from_json(const Json &value, const DcInput &input) {
    fields(value, {"from", "to"});
    return DcNetPair{input.net(local_id<NetId>(value.at("from"))),
                     input.net(local_id<NetId>(value.at("to")))};
}

Json source_json(const DcSource &source) {
    return std::visit(
        [](const auto &value) {
            constexpr bool voltage = std::same_as<std::decay_t<decltype(value)>, DcVoltageSource>;
            return Json{{"kind", voltage ? "voltage" : "current"},
                        {"key", value.key().value()},
                        {"nets", nets_json(value.nets())},
                        {"value", Json{{"dimension", voltage ? "voltage" : "current"},
                                       {"si", value.value().value()}}}};
        },
        source);
}

DcSource source_from_json(const Json &value, const DcInput &input) {
    fields(value, {"kind", "key", "nets", "value"});
    const auto kind = text(value.at("kind"));
    const auto &parameter = value.at("value");
    fields(parameter, {"dimension", "si"});
    const auto dimension = text(parameter.at("dimension"));
    require(dimension == "voltage" || dimension == "current", "unknown source dimension");
    require(parameter.at("si").is_number(), "source value must be numeric");
    const auto quantity =
        Quantity{dimension == "voltage" ? UnitDimension::Voltage : UnitDimension::Current,
                 parameter.at("si").get<double>()};
    const auto key = DcSourceKey{text(value.at("key"))};
    const auto nets = nets_from_json(value.at("nets"), input);
    if (kind == "voltage") {
        return DcVoltageSource{key, nets, quantity};
    }
    require(kind == "current", "unknown source kind");
    return DcCurrentSource{key, nets, quantity};
}

Json probe_json(const DcProbe &probe) {
    return std::visit(
        [](const auto &value) {
            using Probe = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<Probe, DcVoltageProbe>) {
                return Json{{"kind", "voltage"},
                            {"key", value.key().value()},
                            {"nets", nets_json(value.nets())}};
            } else if constexpr (std::same_as<Probe, DcSourceCurrentProbe>) {
                return Json{{"kind", "source_current"},
                            {"key", value.key().value()},
                            {"source", value.source().value()}};
            } else {
                static_assert(std::same_as<Probe, DcModelElementCurrentProbe>);
                return Json{{"kind", "model_element_current"},
                            {"key", value.key().value()},
                            {"occurrence", detail::encode_local_id(value.occurrence().id())},
                            {"element", value.element().value()}};
            }
        },
        probe);
}

DcProbe probe_from_json(const Json &value, const DcInput &input) {
    require(value.is_object() && value.contains("kind"), "probe kind is missing");
    const auto kind = text(value.at("kind"));
    if (kind == "voltage") {
        fields(value, {"kind", "key", "nets"});
        return DcVoltageProbe{DcProbeKey{text(value.at("key"))},
                              nets_from_json(value.at("nets"), input)};
    }
    if (kind == "source_current") {
        fields(value, {"kind", "key", "source"});
        return DcSourceCurrentProbe{DcProbeKey{text(value.at("key"))},
                                    DcSourceKey{text(value.at("source"))}};
    }
    require(kind == "model_element_current", "unknown probe kind");
    fields(value, {"kind", "key", "occurrence", "element"});
    return DcModelElementCurrentProbe{
        DcProbeKey{text(value.at("key"))},
        input.occurrence(local_id<ComponentId>(value.at("occurrence"))),
        ModelElementKey{text(value.at("element"))}};
}

Json exclusion_json(const DcOccurrenceExclusion &exclusion) {
    auto reason = std::visit(
        [](const auto &value) {
            using Reason = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<Reason, DcNonElectricalExclusion>) {
                return Json{{"kind", "non_electrical"}};
            } else if constexpr (std::same_as<Reason, DcOutsideAnalysisExclusion>) {
                return Json{{"kind", "outside_analysis"}};
            } else {
                static_assert(std::same_as<Reason, DcReplacedByStimulusExclusion>);
                auto sources = Json::array();
                for (const auto &source : value.sources()) {
                    sources.push_back(source.value());
                }
                return Json{{"kind", "replaced_by_stimulus"}, {"sources", std::move(sources)}};
            }
        },
        exclusion.reason());
    return Json{{"occurrence", detail::encode_local_id(exclusion.occurrence().id())},
                {"reason", std::move(reason)}};
}

DcOccurrenceExclusion exclusion_from_json(const Json &value, const DcInput &input) {
    fields(value, {"occurrence", "reason"});
    const auto occurrence = input.occurrence(local_id<ComponentId>(value.at("occurrence")));
    const auto &reason = value.at("reason");
    require(reason.is_object() && reason.contains("kind"), "exclusion reason is missing");
    const auto kind = text(reason.at("kind"));
    if (kind == "non_electrical") {
        fields(reason, {"kind"});
        return DcOccurrenceExclusion{occurrence, DcNonElectricalExclusion{}};
    }
    if (kind == "outside_analysis") {
        fields(reason, {"kind"});
        return DcOccurrenceExclusion{occurrence, DcOutsideAnalysisExclusion{}};
    }
    require(kind == "replaced_by_stimulus", "unknown exclusion reason");
    fields(reason, {"kind", "sources"});
    auto sources = std::vector<DcSourceKey>{};
    for (const auto &source : array(reason.at("sources"))) {
        sources.emplace_back(text(source));
    }
    return DcOccurrenceExclusion{occurrence, DcReplacedByStimulusExclusion{std::move(sources)}};
}

} // namespace

std::string write_dc_request(const DcRequest &request) {
    auto sources = Json::array();
    for (const auto &source : request.sources()) {
        sources.push_back(source_json(source));
    }
    auto probes = Json::array();
    for (const auto &probe : request.probes()) {
        probes.push_back(probe_json(probe));
    }
    auto exclusions = Json::array();
    for (const auto &exclusion : request.exclusions()) {
        exclusions.push_back(exclusion_json(exclusion));
    }
    const auto &identity = request.input().identity();
    return Json{{"format", dc_request_format_name()},
                {"version", dc_request_format_version()},
                {"key", request.key().value()},
                {"input", Json{{"logical", identity.logical().value()},
                               {"selected_parts", identity.selected_parts().value()}}},
                {"reference", request.reference()
                                  ? Json(detail::encode_local_id(request.reference()->id()))
                                  : Json(nullptr)},
                {"sources", std::move(sources)},
                {"probes", std::move(probes)},
                {"exclusions", std::move(exclusions)}}
               .dump(2) +
           "\n";
}

DcRequest read_dc_request(std::string_view bytes, const DcInput &input) {
    try {
        auto object_keys = std::vector<std::set<std::string>>{};
        const auto document = Json::parse(bytes, [&](int, Json::parse_event_t event, Json &value) {
            if (event == Json::parse_event_t::object_start) {
                object_keys.emplace_back();
            } else if (event == Json::parse_event_t::key) {
                require(object_keys.back().insert(value.get<std::string>()).second,
                        "duplicate object key");
            } else if (event == Json::parse_event_t::object_end) {
                object_keys.pop_back();
            }
            return true;
        });
        fields(document, {"format", "version", "key", "input", "reference", "sources", "probes",
                          "exclusions"});
        require(text(document.at("format")) == dc_request_format_name(), "unsupported format");
        require(document.at("version").is_number_integer() &&
                    document.at("version") == dc_request_format_version(),
                "unsupported version");
        const auto &identity = document.at("input");
        fields(identity, {"logical", "selected_parts"});
        if (DcInputIdentity{ContentHash{text(identity.at("logical"))},
                            ContentHash{text(identity.at("selected_parts"))}} != input.identity()) {
            throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                      "DC request belongs to a different exact input"};
        }
        auto reference = std::optional<DcNetRef>{};
        if (!document.at("reference").is_null()) {
            reference = input.net(local_id<NetId>(document.at("reference")));
        }
        auto sources = std::vector<DcSource>{};
        for (const auto &source : array(document.at("sources"))) {
            sources.push_back(source_from_json(source, input));
        }
        auto probes = std::vector<DcProbe>{};
        for (const auto &probe : array(document.at("probes"))) {
            probes.push_back(probe_from_json(probe, input));
        }
        auto exclusions = std::vector<DcOccurrenceExclusion>{};
        for (const auto &exclusion : array(document.at("exclusions"))) {
            exclusions.push_back(exclusion_from_json(exclusion, input));
        }
        return DcRequest{DcRequestKey{text(document.at("key"))},
                         input,
                         reference,
                         std::move(sources),
                         std::move(probes),
                         std::move(exclusions)};
    } catch (const nlohmann::json::exception &error) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "DC request JSON is invalid: " + std::string{error.what()}};
    }
}

} // namespace volt::io
