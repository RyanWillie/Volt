#include <volt/io/electrical/ac_request_io.hpp>

#include "request_io_detail.hpp"

#include <nlohmann/json.hpp>
#include <set>
#include <volt/core/errors.hpp>
#include <volt/io/detail/typed_id.hpp>

namespace volt::io {
namespace {
using Json = nlohmann::ordered_json;

using namespace request_detail;
} // namespace

std::string write_ac_request(const AcRequest &request) {
    auto probes = Json::array();
    for (const auto &probe : request.probes())
        probes.push_back(probe_json(probe));
    auto exclusions = Json::array();
    for (const auto &exclusion : request.exclusions())
        exclusions.push_back(exclusion_json(exclusion));
    auto document = Json{
        {"format", ac_request_format_name()},
        {"version", ac_request_format_version()},
        {"key", request.key().value()},
        {"input", Json{{"logical", request.input().identity().logical().value()},
                       {"selected_parts", request.input().identity().selected_parts().value()}}},
        {"reference", request.reference() ? Json(detail::encode_local_id(request.reference()->id()))
                                          : Json(nullptr)},
        {"sources", Json::array()},
        {"probes", std::move(probes)},
        {"exclusions", std::move(exclusions)}};
    document["frequencies"] = Json::array();
    for (const auto frequency : request.frequencies())
        document["frequencies"].push_back(
            Json{{"dimension", "frequency"}, {"si", frequency.value()}});
    for (const auto &source : request.sources()) {
        std::visit(
            [&](const auto &value) {
                constexpr bool voltage =
                    std::same_as<std::decay_t<decltype(value)>, AcVoltageSource>;
                document["sources"].push_back(
                    Json{{"kind", voltage ? "voltage" : "current"},
                         {"key", value.key().value()},
                         {"nets", nets_json(value.nets())},
                         {"amplitude", Json{{"dimension", voltage ? "voltage" : "current"},
                                            {"si", value.amplitude().value()}}},
                         {"phase_radians", value.phase()}});
            },
            source);
    }
    document["gains"] = Json::array();
    for (const auto &gain : request.gains())
        document["gains"].push_back(Json{{"key", gain.key().value()},
                                         {"numerator", gain.numerator().value()},
                                         {"denominator", gain.denominator().value()}});
    document["impedances"] = Json::array();
    for (const auto &impedance : request.impedances())
        document["impedances"].push_back(Json{{"key", impedance.key().value()},
                                              {"nets", nets_json(impedance.nets())},
                                              {"source", impedance.source().value()}});
    return document.dump(2) + "\n";
}

AcRequest read_ac_request(std::string_view bytes, const ElectricalInput &input) {
    try {
        std::vector<std::set<std::string>> keys;
        auto document = Json::parse(bytes, [&](int, Json::parse_event_t event, Json &value) {
            if (event == Json::parse_event_t::object_start)
                keys.emplace_back();
            else if (event == Json::parse_event_t::key)
                require(keys.back().insert(value.get<std::string>()).second,
                        "AC request has a duplicate object key");
            else if (event == Json::parse_event_t::object_end)
                keys.pop_back();
            return true;
        });
        fields(document, {"format", "version", "key", "input", "reference", "sources", "probes",
                          "exclusions", "frequencies", "gains", "impedances"});
        require(document.at("format") == ac_request_format_name() &&
                    document.at("version").is_number_integer() &&
                    document.at("version") == ac_request_format_version(),
                "Unsupported AC request format or version");
        require(document.at("frequencies").is_array(), "AC frequencies must be an array");
        std::vector<Quantity> frequencies;
        for (const auto &frequency : document.at("frequencies")) {
            fields(frequency, {"dimension", "si"});
            require(frequency.at("dimension") == "frequency" && frequency.at("si").is_number(),
                    "AC frequency must have numeric SI frequency dimension");
            frequencies.emplace_back(UnitDimension::Frequency, frequency.at("si").get<double>());
        }
        require(document.at("sources").is_array(), "AC sources must be an array");
        std::vector<AcSource> sources;
        for (auto &source : document.at("sources")) {
            fields(source, {"kind", "key", "nets", "amplitude", "phase_radians"});
            fields(source.at("amplitude"), {"dimension", "si"});
            const bool voltage = source.at("kind") == "voltage";
            require(voltage || source.at("kind") == "current", "Unknown AC source kind");
            require(source.at("amplitude").at("dimension") == (voltage ? "voltage" : "current") &&
                        source.at("amplitude").at("si").is_number() &&
                        source.at("phase_radians").is_number(),
                    "AC source amplitude dimension or phase is invalid");
            const auto key = ElectricalSourceKey{source.at("key").get<std::string>()};
            const auto nets = nets_from_json(source.at("nets"), input);
            const auto amplitude =
                Quantity{voltage ? UnitDimension::Voltage : UnitDimension::Current,
                         source.at("amplitude").at("si").get<double>()};
            const auto phase = source.at("phase_radians").get<double>();
            if (voltage)
                sources.emplace_back(AcVoltageSource{key, nets, amplitude, phase});
            else
                sources.emplace_back(AcCurrentSource{key, nets, amplitude, phase});
        }
        require(document.at("gains").is_array() && document.at("impedances").is_array(),
                "AC derived probes must be arrays");
        std::vector<AcGainProbe> gains;
        for (const auto &gain : document.at("gains")) {
            fields(gain, {"key", "numerator", "denominator"});
            gains.emplace_back(ElectricalProbeKey{gain.at("key").get<std::string>()},
                               ElectricalProbeKey{gain.at("numerator").get<std::string>()},
                               ElectricalProbeKey{gain.at("denominator").get<std::string>()});
        }
        std::vector<AcImpedanceProbe> impedances;
        for (const auto &impedance : document.at("impedances")) {
            fields(impedance, {"key", "nets", "source"});
            impedances.emplace_back(ElectricalProbeKey{impedance.at("key").get<std::string>()},
                                    nets_from_json(impedance.at("nets"), input),
                                    ElectricalSourceKey{impedance.at("source").get<std::string>()});
        }
        const auto &identity = document.at("input");
        fields(identity, {"logical", "selected_parts"});
        require(ElectricalInputIdentity{ContentHash{text(identity.at("logical"))},
                                        ContentHash{text(identity.at("selected_parts"))}} ==
                    input.identity(),
                "AC request belongs to another exact input");
        std::optional<ElectricalNetRef> reference;
        if (!document.at("reference").is_null())
            reference = input.net(local_id<NetId>(document.at("reference")));
        std::vector<DcProbe> probes;
        for (const auto &probe : array(document.at("probes")))
            probes.push_back(probe_from_json(probe, input));
        std::vector<DcOccurrenceExclusion> exclusions;
        for (const auto &exclusion : array(document.at("exclusions")))
            exclusions.push_back(exclusion_from_json(exclusion, input));
        return AcRequest{ElectricalRequestKey{text(document.at("key"))},
                         input,
                         reference,
                         AcFrequencySweep{std::move(frequencies)},
                         std::move(sources),
                         std::move(probes),
                         std::move(exclusions),
                         std::move(gains),
                         std::move(impedances)};
    } catch (const Json::exception &error) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Invalid AC request JSON: " + std::string{error.what()}};
    }
}
} // namespace volt::io
