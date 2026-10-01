#pragma once
#include <nlohmann/json.hpp>
#include <set>
#include <volt/core/errors.hpp>
#include <volt/electrical/dc_request.hpp>
#include <volt/io/detail/typed_id.hpp>

namespace volt::io::request_detail {
using Json = nlohmann::ordered_json;

inline void require(bool condition, const std::string &message) {
    if (!condition) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Electrical request: " + message};
    }
}

inline void fields(const Json &value, std::initializer_list<std::string_view> keys) {
    require(value.is_object() && value.size() == keys.size(), "unexpected object fields");
    for (const auto key : keys) {
        require(value.contains(key), "missing field " + std::string{key});
    }
}

inline std::string text(const Json &value) {
    require(value.is_string(), "expected a string");
    return value.get<std::string>();
}

inline const Json &array(const Json &value) {
    require(value.is_array(), "expected an array");
    return value;
}

template <typename Id> Id local_id(const Json &value) {
    const auto spelling = text(value);
    const auto result = detail::decode_local_id<Id>(spelling);
    require(detail::encode_local_id(result) == spelling, "noncanonical local identity");
    return result;
}

inline Json nets_json(const ElectricalNetPair &nets) {
    return Json{{"from", detail::encode_local_id(nets.from().id())},
                {"to", detail::encode_local_id(nets.to().id())}};
}

inline ElectricalNetPair nets_from_json(const Json &value, const ElectricalInput &input) {
    fields(value, {"from", "to"});
    return ElectricalNetPair{input.net(local_id<NetId>(value.at("from"))),
                             input.net(local_id<NetId>(value.at("to")))};
}

inline Json probe_json(const DcProbe &probe) {
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

inline DcProbe probe_from_json(const Json &value, const ElectricalInput &input) {
    require(value.is_object() && value.contains("kind"), "probe kind is missing");
    const auto kind = text(value.at("kind"));
    if (kind == "voltage") {
        fields(value, {"kind", "key", "nets"});
        return DcVoltageProbe{ElectricalProbeKey{text(value.at("key"))},
                              nets_from_json(value.at("nets"), input)};
    }
    if (kind == "source_current") {
        fields(value, {"kind", "key", "source"});
        return DcSourceCurrentProbe{ElectricalProbeKey{text(value.at("key"))},
                                    ElectricalSourceKey{text(value.at("source"))}};
    }
    require(kind == "model_element_current", "unknown probe kind");
    fields(value, {"kind", "key", "occurrence", "element"});
    return DcModelElementCurrentProbe{
        ElectricalProbeKey{text(value.at("key"))},
        input.occurrence(local_id<ComponentId>(value.at("occurrence"))),
        ModelElementKey{text(value.at("element"))}};
}

inline Json exclusion_json(const DcOccurrenceExclusion &exclusion) {
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

inline DcOccurrenceExclusion exclusion_from_json(const Json &value, const ElectricalInput &input) {
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
    auto sources = std::vector<ElectricalSourceKey>{};
    for (const auto &source : array(reason.at("sources"))) {
        sources.emplace_back(text(source));
    }
    return DcOccurrenceExclusion{occurrence, DcReplacedByStimulusExclusion{std::move(sources)}};
}

} // namespace volt::io::request_detail
