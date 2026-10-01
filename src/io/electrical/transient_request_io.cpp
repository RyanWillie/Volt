#include "request_io_detail.hpp"
#include <volt/io/electrical/transient_request_io.hpp>

namespace volt::io {
namespace {
using namespace request_detail;

const char *dimension_name(UnitDimension dimension) {
    if (dimension == UnitDimension::Time)
        return "time";
    if (dimension == UnitDimension::Voltage)
        return "voltage";
    if (dimension == UnitDimension::Current)
        return "current";
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Unsupported transient quantity dimension"};
}

Json quantity_json(Quantity value) {
    return Json{{"dimension", dimension_name(value.dimension())}, {"si", value.value()}};
}

Quantity quantity_from_json(const Json &value, UnitDimension expected) {
    fields(value, {"dimension", "si"});
    require(text(value.at("dimension")) == dimension_name(expected) && value.at("si").is_number(),
            "Transient quantity has invalid dimension or SI value");
    return Quantity{expected, value.at("si").get<double>()};
}

Json waveform_json(const TransientWaveform &waveform) {
    if (waveform.constant())
        return Json{{"kind", "constant"}, {"value", quantity_json(waveform.knots().front().value)}};
    auto knots = Json::array();
    for (const auto &knot : waveform.knots())
        knots.push_back(
            Json{{"time", quantity_json(knot.time)}, {"value", quantity_json(knot.value)}});
    return Json{{"kind", "continuous_pwl"}, {"knots", std::move(knots)}};
}

TransientWaveform waveform_from_json(const Json &value, UnitDimension dimension) {
    require(value.is_object() && value.contains("kind"), "Transient waveform kind is missing");
    if (text(value.at("kind")) == "constant") {
        fields(value, {"kind", "value"});
        return TransientWaveform{quantity_from_json(value.at("value"), dimension)};
    }
    require(text(value.at("kind")) == "continuous_pwl", "Unknown transient waveform kind");
    fields(value, {"kind", "knots"});
    std::vector<TransientWaveformKnot> knots;
    for (const auto &knot : array(value.at("knots"))) {
        fields(knot, {"time", "value"});
        knots.push_back({quantity_from_json(knot.at("time"), UnitDimension::Time),
                         quantity_from_json(knot.at("value"), dimension)});
    }
    return TransientWaveform{std::move(knots)};
}

Json part_json(const LibraryPartRef &part) {
    return Json{{"library_namespace", part.library_namespace()},
                {"library_version", part.library_version()},
                {"part_key", part.part_key().value()},
                {"library_digest", part.library_digest().value()},
                {"part_digest", part.part_digest().value()}};
}

LibraryPartRef part_from_json(const Json &value) {
    fields(value,
           {"library_namespace", "library_version", "part_key", "library_digest", "part_digest"});
    return LibraryPartRef{text(value.at("library_namespace")), text(value.at("library_version")),
                          PartKey{text(value.at("part_key"))},
                          ContentHash{text(value.at("library_digest"))},
                          ContentHash{text(value.at("part_digest"))}};
}
} // namespace

std::string write_transient_request(const TransientRequest &request) {
    auto times = Json::array();
    for (const auto time : request.times())
        times.push_back(quantity_json(time));
    auto sources = Json::array();
    for (const auto &source : request.sources())
        std::visit(
            [&](const auto &value) {
                constexpr bool voltage =
                    std::same_as<std::decay_t<decltype(value)>, TransientVoltageSource>;
                sources.push_back(Json{{"kind", voltage ? "voltage" : "current"},
                                       {"key", value.key().value()},
                                       {"nets", nets_json(value.nets())},
                                       {"waveform", waveform_json(value.waveform())}});
            },
            source);
    auto probes = Json::array();
    for (const auto &probe : request.probes())
        probes.push_back(probe_json(probe));
    auto exclusions = Json::array();
    for (const auto &exclusion : request.exclusions())
        exclusions.push_back(exclusion_json(exclusion));
    auto initial_state = Json::array();
    for (const auto &state : request.initial_state())
        initial_state.push_back(Json{
            {"occurrence", detail::encode_local_id(state.occurrence().id())},
            {"part", part_json(state.part())},
            {"element", state.element().value()},
            {"kind", state.kind() == TransientStorageKind::CapacitorVoltage ? "capacitor_voltage"
                                                                            : "inductor_current"},
            {"value", quantity_json(state.value())}});
    auto provenance = Json(nullptr);
    if (request.initial_conditions().dc_provenance()) {
        const auto &value = *request.initial_conditions().dc_provenance();
        provenance = Json{{"input", Json{{"logical", value.input.logical().value()},
                                         {"selected_parts", value.input.selected_parts().value()}}},
                          {"analysis_identity", value.analysis_identity.value()},
                          {"participation_identity", value.participation_identity.value()},
                          {"storage_count", value.storage_count}};
    }
    return Json{{"format", transient_request_format_name()},
                {"version", transient_request_format_version()},
                {"key", request.key().value()},
                {"input",
                 Json{{"logical", request.input().identity().logical().value()},
                      {"selected_parts", request.input().identity().selected_parts().value()}}},
                {"reference", request.reference()
                                  ? Json(detail::encode_local_id(request.reference()->id()))
                                  : Json(nullptr)},
                {"sources", std::move(sources)},
                {"probes", std::move(probes)},
                {"exclusions", std::move(exclusions)},
                {"times", std::move(times)},
                {"initial_state", std::move(initial_state)},
                {"dc_provenance", std::move(provenance)}}
               .dump(2) +
           "\n";
}

TransientRequest read_transient_request(std::string_view bytes, const ElectricalInput &input) {
    try {
        std::vector<std::set<std::string>> keys;
        const auto document = Json::parse(bytes, [&](int, Json::parse_event_t event, Json &value) {
            if (event == Json::parse_event_t::object_start)
                keys.emplace_back();
            else if (event == Json::parse_event_t::key)
                require(keys.back().insert(value.get<std::string>()).second,
                        "Transient request has duplicate object key");
            else if (event == Json::parse_event_t::object_end)
                keys.pop_back();
            return true;
        });
        fields(document, {"format", "version", "key", "input", "reference", "sources", "probes",
                          "exclusions", "times", "initial_state", "dc_provenance"});
        require(document.at("format") == transient_request_format_name() &&
                    document.at("version").is_number_integer() &&
                    document.at("version") == transient_request_format_version(),
                "Unsupported transient request format or version");
        const auto &identity = document.at("input");
        fields(identity, {"logical", "selected_parts"});
        require(ElectricalInputIdentity{ContentHash{text(identity.at("logical"))},
                                        ContentHash{text(identity.at("selected_parts"))}} ==
                    input.identity(),
                "Transient request belongs to another exact input");
        std::optional<ElectricalNetRef> reference;
        if (!document.at("reference").is_null())
            reference = input.net(local_id<NetId>(document.at("reference")));
        std::vector<Quantity> times;
        for (const auto &time : array(document.at("times")))
            times.push_back(quantity_from_json(time, UnitDimension::Time));
        std::vector<TransientSource> sources;
        for (const auto &source : array(document.at("sources"))) {
            fields(source, {"kind", "key", "nets", "waveform"});
            const auto kind = text(source.at("kind"));
            require(kind == "voltage" || kind == "current", "Unknown transient source kind");
            const auto key = ElectricalSourceKey{text(source.at("key"))};
            const auto nets = nets_from_json(source.at("nets"), input);
            const auto waveform = waveform_from_json(source.at("waveform"),
                                                     kind == "voltage" ? UnitDimension::Voltage
                                                                       : UnitDimension::Current);
            if (kind == "voltage")
                sources.emplace_back(TransientVoltageSource{key, nets, waveform});
            else
                sources.emplace_back(TransientCurrentSource{key, nets, waveform});
        }
        std::vector<DcProbe> probes;
        for (const auto &probe : array(document.at("probes")))
            probes.push_back(probe_from_json(probe, input));
        std::vector<DcOccurrenceExclusion> exclusions;
        for (const auto &exclusion : array(document.at("exclusions")))
            exclusions.push_back(exclusion_from_json(exclusion, input));
        std::vector<TransientInitialState> initial_state;
        for (const auto &state : array(document.at("initial_state"))) {
            fields(state, {"occurrence", "part", "element", "kind", "value"});
            const auto kind = text(state.at("kind"));
            require(kind == "capacitor_voltage" || kind == "inductor_current",
                    "Unknown transient storage kind");
            initial_state.emplace_back(
                input.occurrence(local_id<ComponentId>(state.at("occurrence"))),
                part_from_json(state.at("part")), ModelElementKey{text(state.at("element"))},
                kind == "capacitor_voltage" ? TransientStorageKind::CapacitorVoltage
                                            : TransientStorageKind::InductorCurrent,
                quantity_from_json(state.at("value"), kind == "capacitor_voltage"
                                                          ? UnitDimension::Voltage
                                                          : UnitDimension::Current));
        }
        std::optional<TransientDcProvenance> provenance;
        const auto &value = document.at("dc_provenance");
        if (!value.is_null()) {
            fields(value,
                   {"input", "analysis_identity", "participation_identity", "storage_count"});
            require(value.at("storage_count").is_number_unsigned() ||
                        (value.at("storage_count").is_number_integer() &&
                         value.at("storage_count").get<std::int64_t>() >= 0),
                    "DC storage count must be a nonnegative integer");
            const auto &provenance_input = value.at("input");
            fields(provenance_input, {"logical", "selected_parts"});
            provenance = TransientDcProvenance{
                ElectricalInputIdentity{ContentHash{text(provenance_input.at("logical"))},
                                        ContentHash{text(provenance_input.at("selected_parts"))}},
                ContentHash{text(value.at("analysis_identity"))},
                ContentHash{text(value.at("participation_identity"))},
                value.at("storage_count").get<std::size_t>()};
        }
        return TransientRequest{
            ElectricalRequestKey{text(document.at("key"))},
            input,
            reference,
            TransientTimeGrid{std::move(times)},
            std::move(sources),
            std::move(probes),
            std::move(exclusions),
            TransientInitialConditions{std::move(initial_state), std::move(provenance)}};
    } catch (const Json::exception &error) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Invalid transient request JSON: " + std::string{error.what()}};
    }
}
} // namespace volt::io
