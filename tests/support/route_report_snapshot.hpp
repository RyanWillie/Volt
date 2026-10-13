#pragma once

#include <nlohmann/json.hpp>
#include <volt/pcb/routing/route.hpp>

namespace volt::test {
using RouteJson = nlohmann::json;

template <typename T> RouteJson route_optional_id(const std::optional<T> &value) {
    return value ? RouteJson(value->index()) : RouteJson(nullptr);
}

inline RouteJson route_source(const RouteRuleSource &source) {
    auto derivation = RouteJson(nullptr);
    if (source.derivation) {
        const auto &d = *source.derivation;
        derivation = {{"calculator_id", d.calculator_id},
                      {"calculator_name", d.calculator_name},
                      {"standard", d.standard},
                      {"reference", d.reference},
                      {"inputs", RouteJson::array()}};
        for (const auto &input : d.inputs)
            derivation["inputs"].push_back({{"name", input.name},
                                            {"value", input.value},
                                            {"text_value", input.text_value},
                                            {"unit", input.unit}});
    }
    auto kinds = RouteJson::array();
    if (source.first_kind)
        kinds = {static_cast<int>(*source.first_kind), static_cast<int>(*source.second_kind)};
    return {{"kind", static_cast<int>(source.kind)},
            {"field", static_cast<int>(source.field)},
            {"net_class", route_optional_id(source.net_class)},
            {"room", route_optional_id(source.room)},
            {"keepout", route_optional_id(source.keepout)},
            {"clearance_kinds", kinds},
            {"derivation", derivation}};
}

inline RouteJson route_sources(const std::vector<RouteRuleSource> &sources) {
    auto out = RouteJson::array();
    for (const auto &source : sources)
        out.push_back(route_source(source));
    return out;
}

inline RouteJson route_rule(const std::optional<RouteRuleValue> &value) {
    if (!value)
        return nullptr;
    return {{"value_mm", value->value_mm},
            {"default_mm", value->default_mm},
            {"requested_mm",
             value->requested_mm ? RouteJson(*value->requested_mm) : RouteJson(nullptr)},
            {"sources", route_sources(value->sources)},
            {"enforced_sources", route_sources(value->enforced_sources)}};
}

inline RouteJson route_entities(const std::vector<EntityRef> &entities) {
    auto out = RouteJson::array();
    for (const auto &entity : entities)
        out.push_back({static_cast<int>(entity.kind()), entity.index()});
    return out;
}

inline RouteJson route_point(BoardPoint p) { return {p.x_mm(), p.y_mm()}; }

inline RouteJson route_snapshot(const RouteResult &result) {
    auto out = RouteJson{{"committed", result.committed},
                         {"candidate_complete", result.candidate_complete},
                         {"net", route_optional_id(result.net)},
                         {"message", result.message},
                         {"candidates", RouteJson::array()},
                         {"resolved_steps", RouteJson::array()},
                         {"resolved_rules", RouteJson::array()},
                         {"blockers", RouteJson::array()},
                         {"clearance_checks", RouteJson::array()},
                         {"tracks", RouteJson::array()},
                         {"vias", RouteJson::array()},
                         {"committed_order", RouteJson::array()},
                         {"failure", nullptr}};
    for (const auto &candidate : result.candidates) {
        auto c = RouteJson{{"step_index", candidate.step_index},
                           {"width_mm", nullptr},
                           {"drill_mm", nullptr},
                           {"diameter_mm", nullptr}};
        if (const auto *track = std::get_if<BoardTrack>(&candidate.copper)) {
            c["kind"] = "track";
            c["net"] = track->net().index();
            c["layers"] = {track->layer().index()};
            c["points"] = RouteJson::array();
            for (const auto p : track->points())
                c["points"].push_back(route_point(p));
            c["width_mm"] = track->width_mm();
        } else {
            const auto &via = std::get<BoardVia>(candidate.copper);
            c["kind"] = "via";
            c["net"] = via.net().index();
            c["layers"] = {via.start_layer().index(), via.end_layer().index()};
            c["points"] = RouteJson::array();
            c["points"].push_back(route_point(via.position()));
            c["drill_mm"] = via.drill_diameter_mm();
            c["diameter_mm"] = via.annular_diameter_mm();
        }
        out["candidates"].push_back(c);
    }
    for (const auto &step : result.resolved_steps)
        out["resolved_steps"].push_back({{"step_index", step.step_index},
                                         {"start", route_point(step.start)},
                                         {"end", route_point(step.end)},
                                         {"incoming_layer", step.incoming_layer.index()},
                                         {"outgoing_layer", step.outgoing_layer.index()},
                                         {"candidates", step.candidates}});
    for (const auto &rules : result.resolved_rules)
        out["resolved_rules"].push_back({{"candidate_ordinal", rules.candidate_ordinal},
                                         {"width", route_rule(rules.width)},
                                         {"drill", route_rule(rules.drill)},
                                         {"diameter", route_rule(rules.diameter)}});
    auto allowed = RouteJson::array(), excluded = RouteJson::array();
    for (const auto layer : result.layer_rules.allowed)
        allowed.push_back(layer.index());
    for (const auto layer : result.layer_rules.excluded)
        excluded.push_back(layer.index());
    out["layer_rules"] = {{"allowed", allowed},
                          {"excluded", excluded},
                          {"scope", static_cast<int>(result.layer_rules.scope)},
                          {"requested_names", result.layer_rules.requested_names},
                          {"source", route_source(result.layer_rules.source)}};
    if (result.failure)
        out["failure"] = {{"kind", static_cast<int>(result.failure->kind)},
                          {"step_index", result.failure->step_index
                                             ? RouteJson(*result.failure->step_index)
                                             : RouteJson(nullptr)},
                          {"message", result.failure->message}};
    for (const auto &block : result.blockers)
        out["blockers"].push_back(
            {{"step_index", block.step_index},
             {"candidate_ordinal", block.candidate_ordinal},
             {"kind", static_cast<int>(block.kind)},
             {"layer", route_optional_id(block.layer)},
             {"message", block.message},
             {"witness", block.witness ? route_point(*block.witness) : RouteJson(nullptr)},
             {"required_mm",
              block.required_mm ? RouteJson(*block.required_mm) : RouteJson(nullptr)},
             {"actual_mm", block.actual_mm ? RouteJson(*block.actual_mm) : RouteJson(nullptr)},
             {"sources", route_sources(block.sources)},
             {"entities", route_entities(block.entities)},
             {"obstacle_label", block.obstacle_label},
             {"earlier_candidate",
              block.earlier_candidate ? RouteJson(*block.earlier_candidate) : RouteJson(nullptr)}});
    for (const auto &check : result.clearance_checks)
        out["clearance_checks"].push_back(
            {{"candidate_ordinal", check.candidate_ordinal},
             {"layer", check.layer.index()},
             {"clearance", route_rule(check.clearance)},
             {"actual_mm", check.actual_mm},
             {"obstacle_entities", route_entities(check.obstacle_entities)},
             {"earlier_candidate",
              check.earlier_candidate ? RouteJson(*check.earlier_candidate) : RouteJson(nullptr)}});
    for (const auto track : result.tracks)
        out["tracks"].push_back(track.index());
    for (const auto via : result.vias)
        out["vias"].push_back(via.index());
    for (const auto &committed : result.committed_order)
        out["committed_order"].push_back(std::visit(
            [](const auto &id) -> RouteJson {
                return {std::same_as<std::decay_t<decltype(id)>, BoardTrackId> ? "track" : "via",
                        id.index()};
            },
            committed));
    out["target_resolutions"] = RouteJson::array();
    for (const auto &t : result.target_resolutions) {
        auto constraints = RouteJson::array();
        for (const auto &c : t.constraints)
            constraints.push_back({{"upper_side", c.upper_side},
                                   {"entities", route_entities(c.entities)},
                                   {"clearance", route_rule(c.clearance)},
                                   {"exempt", c.exempt}});
        out["target_resolutions"].push_back({{"step_index", t.step_index},
                                             {"position", route_point(t.position)},
                                             {"axis", static_cast<int>(t.axis)},
                                             {"raw_lower_mm", t.raw_lower_mm},
                                             {"raw_upper_mm", t.raw_upper_mm},
                                             {"lower_mm", t.lower_mm},
                                             {"upper_mm", t.upper_mm},
                                             {"width", route_rule(t.width)},
                                             {"constraints", constraints}});
    }
    return out;
}
} // namespace volt::test
