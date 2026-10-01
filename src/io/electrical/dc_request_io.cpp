#include "request_io_detail.hpp"
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

using namespace request_detail;

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

DcSource source_from_json(const Json &value, const ElectricalInput &input) {
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
    const auto key = ElectricalSourceKey{text(value.at("key"))};
    const auto nets = nets_from_json(value.at("nets"), input);
    if (kind == "voltage") {
        return DcVoltageSource{key, nets, quantity};
    }
    require(kind == "current", "unknown source kind");
    return DcCurrentSource{key, nets, quantity};
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

DcRequest read_dc_request(std::string_view bytes, const ElectricalInput &input) {
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
        if (ElectricalInputIdentity{ContentHash{text(identity.at("logical"))},
                                    ContentHash{text(identity.at("selected_parts"))}} !=
            input.identity()) {
            throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                      "DC request belongs to a different exact input"};
        }
        auto reference = std::optional<ElectricalNetRef>{};
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
        return DcRequest{ElectricalRequestKey{text(document.at("key"))},
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
