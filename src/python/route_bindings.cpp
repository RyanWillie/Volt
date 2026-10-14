#include "route_bindings.hpp"
#include "py_circuit.hpp"

#include <exception>
#include <pybind11/stl.h>

namespace volt::python {
namespace {

template <typename Id> py::object id(const std::optional<Id> &value) {
    return value.has_value() ? py::cast(value->index()) : py::none();
}

template <typename Id> py::tuple ids(const std::vector<Id> &values) {
    auto out = py::tuple{values.size()};
    for (std::size_t i = 0; i < values.size(); ++i)
        out[i] = values[i].index();
    return out;
}

py::tuple point(BoardPoint value) { return py::make_tuple(value.x_mm(), value.y_mm()); }

py::tuple entities(const std::vector<EntityRef> &values) {
    auto out = py::tuple{values.size()};
    for (std::size_t i = 0; i < values.size(); ++i) {
        out[i] = py::make_tuple(static_cast<int>(values[i].kind()), values[i].index());
    }
    return out;
}

PyObject *route_error_type = nullptr;

void translate_route_error(std::exception_ptr pointer) {
    try {
        if (pointer)
            std::rethrow_exception(pointer);
    } catch (const RouteRejected &error) {
        auto type = py::reinterpret_borrow<py::object>(route_error_type);
        auto exception = type(error.what());
        exception.attr("result") = py::cast(error.result());
        PyErr_SetObject(type.ptr(), exception.ptr());
    }
}

} // namespace

void bind_route_values(py::module_ &module) {
    auto *error = PyErr_NewException("volt.RouteError", PyExc_RuntimeError, nullptr);
    if (error == nullptr)
        throw py::error_already_set{};
    module.attr("RouteError") = py::reinterpret_steal<py::object>(error);
    route_error_type = error;
    py::register_exception_translator(&translate_route_error);

    py::enum_<CornerStyle>(module, "CornerStyle")
        .value("CHAMFER45", CornerStyle::Chamfer45)
        .value("ORTHOGONAL", CornerStyle::Orthogonal)
        .value("DIRECT", CornerStyle::Direct);
    py::enum_<RoutePolicy>(module, "RoutePolicy")
        .value("STRICT", RoutePolicy::Strict)
        .value("REPORT", RoutePolicy::Report);
    py::enum_<RouteTrackEnd>(module, "RouteTrackEnd")
        .value("FIRST", RouteTrackEnd::First)
        .value("LAST", RouteTrackEnd::Last);
    py::enum_<RouteFailureKind>(module, "RouteFailureKind")
        .value("BLOCKED", RouteFailureKind::Blocked)
        .value("ENDPOINT_UNREACHABLE", RouteFailureKind::EndpointUnreachable)
        .value("LAYER_NOT_ALLOWED", RouteFailureKind::LayerNotAllowed)
        .value("RULE_VIOLATION", RouteFailureKind::RuleViolation)
        .value("RULE_UNRESOLVABLE", RouteFailureKind::RuleUnresolvable)
        .value("GEOMETRY_UNRESOLVABLE", RouteFailureKind::GeometryUnresolvable)
        .value("NO_CANDIDATE", RouteFailureKind::NoCandidate);
    py::enum_<RouteRuleSourceKind>(module, "RouteRuleSourceKind")
        .value("BOARD_RULE", RouteRuleSourceKind::BoardRule)
        .value("NET_CLASS_RULE", RouteRuleSourceKind::NetClassRule)
        .value("ROOM_RULE", RouteRuleSourceKind::RoomRule)
        .value("EXPLICIT_OVERRIDE", RouteRuleSourceKind::ExplicitOverride)
        .value("KEEPOUT", RouteRuleSourceKind::Keepout);
    py::enum_<RouteRuleField>(module, "RouteRuleField")
        .value("WIDTH", RouteRuleField::Width)
        .value("VIA_DRILL", RouteRuleField::ViaDrill)
        .value("VIA_DIAMETER", RouteRuleField::ViaDiameter)
        .value("CLEARANCE", RouteRuleField::Clearance)
        .value("ALLOWED_LAYERS", RouteRuleField::AllowedLayers);

    py::class_<RoutePoint>(module, "RoutePoint")
        .def(py::init([](double x, double y) { return RoutePoint{BoardPoint{x, y}}; }))
        .def_property_readonly("position", [](const RoutePoint &v) { return point(v.position); });
    py::class_<RouteNetRef>(module, "RouteNetRef")
        .def(py::init([](const PyCircuit &c, std::size_t net) {
                 return RouteNetRef{&c.logical_circuit(), NetId{net}};
             }),
             py::keep_alive<1, 2>())
        .def(py::init([](const PyBoard &b, std::size_t net) {
                 return RouteNetRef{&b.authoring_board().circuit(), NetId{net}};
             }),
             py::keep_alive<1, 2>())
        .def_property_readonly("net", [](const RouteNetRef &v) { return v.net.index(); });
    py::class_<RouteLayerRef>(module, "RouteLayerRef")
        .def(py::init([](const PyBoard &b, std::size_t layer) {
                 return RouteLayerRef{&b.authoring_board(), BoardLayerId{layer}};
             }),
             py::keep_alive<1, 2>())
        .def_property_readonly("layer", [](const RouteLayerRef &v) { return v.layer.index(); });
    py::class_<RoutePad>(module, "RoutePad")
        .def(py::init([](const PyBoard &b, std::size_t placement, const RoutePad &identity,
                         std::size_t pad) {
                 return RoutePad{&b.authoring_board(), ComponentPlacementId{placement},
                                 identity.footprint, FootprintPadId{pad}};
             }),
             py::keep_alive<1, 2>())
        .def_property_readonly("placement", [](const RoutePad &v) { return v.placement.index(); })
        .def_property_readonly("pad", [](const RoutePad &v) { return v.pad.index(); })
        .def_property_readonly("footprint", [](const RoutePad &v) {
            return py::make_tuple(v.footprint.ref().library(), v.footprint.ref().name());
        });
    py::class_<RouteViaRef>(module, "RouteViaRef")
        .def(py::init([](const PyBoard &b, std::size_t via) {
                 return RouteViaRef{&b.authoring_board(), BoardViaId{via}};
             }),
             py::keep_alive<1, 2>())
        .def_property_readonly("via", [](const RouteViaRef &v) { return v.via.index(); });
    py::class_<RouteTrackRef>(module, "RouteTrackRef")
        .def(py::init([](const PyBoard &b, std::size_t track, RouteTrackEnd end) {
                 return RouteTrackRef{&b.authoring_board(), BoardTrackId{track}, end};
             }),
             py::keep_alive<1, 2>(), py::arg("board"), py::arg("track"),
             py::arg("end") = RouteTrackEnd::Last)
        .def_property_readonly("track", [](const RouteTrackRef &v) { return v.track.index(); })
        .def_readonly("end", &RouteTrackRef::end);
    py::enum_<RouteAxis>(module, "RouteAxis").value("X", RouteAxis::X).value("Y", RouteAxis::Y);
    py::enum_<RouteDirection>(module, "RouteDirection")
        .value("POSITIVE_X", RouteDirection::PositiveX)
        .value("NEGATIVE_X", RouteDirection::NegativeX)
        .value("POSITIVE_Y", RouteDirection::PositiveY)
        .value("NEGATIVE_Y", RouteDirection::NegativeY);
    py::class_<RouteTrackCopper>(module, "RouteTrackCopper")
        .def(py::init([](const PyBoard &b, std::size_t track) {
                 return RouteTrackCopper{&b.authoring_board(), BoardTrackId{track}};
             }),
             py::keep_alive<1, 2>())
        .def_property_readonly("track", [](const RouteTrackCopper &v) { return v.track.index(); });
    py::class_<RoutePlacedCopper>(module, "RoutePlacedCopper")
        .def(py::init([](const RoutePad &identity) {
                 return RoutePlacedCopper{identity.owner, identity.placement, identity.footprint};
             }),
             py::keep_alive<1, 2>())
        .def_property_readonly("placement",
                               [](const RoutePlacedCopper &v) { return v.placement.index(); });
    py::class_<RouteKeepout>(module, "RouteKeepout")
        .def(py::init([](const PyBoard &b, std::size_t keepout) {
                 return RouteKeepout{&b.authoring_board(), BoardKeepoutId{keepout}};
             }),
             py::keep_alive<1, 2>())
        .def_property_readonly("keepout", [](const RouteKeepout &v) { return v.keepout.index(); });
    py::class_<RouteBoardEdge>(module, "RouteBoardEdge")
        .def(py::init([](const PyBoard &b, std::size_t index) {
                 return RouteBoardEdge{&b.authoring_board(), index};
             }),
             py::keep_alive<1, 2>())
        .def_readonly("edge_index", &RouteBoardEdge::edge_index);
    py::class_<RouteExit>(module, "RouteExit")
        .def(py::init<RouteOperand, RouteDirection, double>(), py::arg("operand"),
             py::arg("direction"), py::arg("distance_mm") = 0.0, py::keep_alive<1, 2>())
        .def_readonly("operand", &RouteExit::operand)
        .def_readonly("direction", &RouteExit::direction)
        .def_readonly("distance_mm", &RouteExit::distance_mm);
    py::class_<RouteBeside>(module, "RouteBeside")
        .def(py::init<RouteOperand, RouteDirection, double>(), py::arg("operand"),
             py::arg("direction"), py::arg("distance_mm") = 0.0, py::keep_alive<1, 2>())
        .def_readonly("operand", &RouteBeside::operand)
        .def_readonly("direction", &RouteBeside::direction)
        .def_readonly("distance_mm", &RouteBeside::distance_mm);
    py::class_<RouteGap>(module, "RouteGap")
        .def(py::init<RouteOperand, RouteOperand, RouteAxis>(), py::arg("lower"), py::arg("upper"),
             py::arg("axis"), py::keep_alive<1, 2>(), py::keep_alive<1, 3>())
        .def_readonly("lower", &RouteGap::lower)
        .def_readonly("upper", &RouteGap::upper)
        .def_readonly("axis", &RouteGap::axis);
    py::class_<RouteAlign>(module, "RouteAlign")
        .def(py::init<RouteEndpoint, RouteAxis>(), py::keep_alive<1, 2>())
        .def_readonly("endpoint", &RouteAlign::endpoint)
        .def_readonly("axis", &RouteAlign::axis);
    py::class_<RouteOffset>(module, "RouteOffset")
        .def(py::init<RouteAxis, double>())
        .def_readonly("axis", &RouteOffset::axis)
        .def_readonly("distance_mm", &RouteOffset::distance_mm);
    py::class_<RouteFinish>(module, "RouteFinish").def(py::init<>());
    py::class_<RouteThrough>(module, "RouteThrough")
        .def(py::init<RouteEndpoint, CornerStyle>(), py::arg("endpoint"),
             py::arg("corner") = CornerStyle::Chamfer45, py::keep_alive<1, 2>())
        .def_readonly("endpoint", &RouteThrough::endpoint)
        .def_readonly("corner", &RouteThrough::corner);
    py::class_<RouteTo>(module, "RouteTo")
        .def(py::init<RouteEndpoint, CornerStyle>(), py::arg("endpoint"),
             py::arg("corner") = CornerStyle::Chamfer45, py::keep_alive<1, 2>())
        .def_readonly("endpoint", &RouteTo::endpoint)
        .def_readonly("corner", &RouteTo::corner);
    py::class_<RouteToX>(module, "RouteToX")
        .def(py::init<double>())
        .def_readonly("coordinate_mm", &RouteToX::coordinate_mm);
    py::class_<RouteToY>(module, "RouteToY")
        .def(py::init<double>())
        .def_readonly("coordinate_mm", &RouteToY::coordinate_mm);
    py::class_<RouteVia>(module, "RouteVia")
        .def(py::init<RouteLayerRef>(), py::keep_alive<1, 2>())
        .def_readonly("destination", &RouteVia::destination);
    py::class_<RouteViaOverride>(module, "RouteViaOverride")
        .def(py::init<double, double>())
        .def_readonly("drill_mm", &RouteViaOverride::drill_mm)
        .def_readonly("diameter_mm", &RouteViaOverride::diameter_mm);
    py::class_<RouteSpec>(module, "RouteSpec")
        .def(py::init([](const PyBoard &b, RouteEndpoint start, RouteLayerRef layer,
                         std::vector<RouteStep> steps, std::optional<RouteNetRef> net,
                         std::optional<double> width, std::optional<RouteViaOverride> via) {
                 return RouteSpec{&b.authoring_board(),
                                  net,
                                  std::move(start),
                                  layer,
                                  std::move(steps),
                                  width,
                                  via};
             }),
             py::arg("board"), py::arg("start"), py::arg("layer"), py::arg("steps"),
             py::arg("net") = std::nullopt, py::arg("width_mm") = std::nullopt,
             py::arg("via_size") = std::nullopt, py::keep_alive<1, 2>(), py::keep_alive<1, 3>(),
             py::keep_alive<1, 4>(), py::keep_alive<1, 5>(), py::keep_alive<1, 6>())
        .def_readonly("start", &RouteSpec::start)
        .def_readonly("layer", &RouteSpec::layer)
        .def_readonly("steps", &RouteSpec::steps)
        .def_readonly("net", &RouteSpec::net)
        .def_readonly("width_mm", &RouteSpec::width_mm)
        .def_readonly("via_size", &RouteSpec::via_size);

    py::class_<RouteRuleSource>(module, "RouteRuleSource")
        .def_readonly("kind", &RouteRuleSource::kind)
        .def_readonly("field", &RouteRuleSource::field)
        .def_property_readonly("net_class",
                               [](const RouteRuleSource &v) { return id(v.net_class); })
        .def_property_readonly("room", [](const RouteRuleSource &v) { return id(v.room); })
        .def_property_readonly("keepout", [](const RouteRuleSource &v) { return id(v.keepout); })
        .def_property_readonly("clearance_kinds",
                               [](const RouteRuleSource &v) -> py::tuple {
                                   if (!v.first_kind)
                                       return py::tuple{};
                                   return py::make_tuple(static_cast<int>(*v.first_kind),
                                                         static_cast<int>(*v.second_kind));
                               })
        .def_property_readonly("derivation", [](const RouteRuleSource &v) -> py::object {
            if (!v.derivation)
                return py::none();
            auto out = py::dict{};
            out["calculator_id"] = v.derivation->calculator_id;
            out["calculator_name"] = v.derivation->calculator_name;
            out["standard"] = v.derivation->standard;
            out["reference"] = v.derivation->reference;
            auto inputs = py::list{};
            for (const auto &input : v.derivation->inputs) {
                auto item = py::dict{};
                item["name"] = input.name;
                item["value"] = input.value;
                item["text_value"] = input.text_value;
                item["unit"] = input.unit;
                inputs.append(item);
            }
            out["inputs"] = inputs;
            return out;
        });
    py::class_<RouteRuleValue>(module, "RouteRuleValue")
        .def_readonly("value_mm", &RouteRuleValue::value_mm)
        .def_readonly("default_mm", &RouteRuleValue::default_mm)
        .def_readonly("requested_mm", &RouteRuleValue::requested_mm)
        .def_readonly("sources", &RouteRuleValue::sources)
        .def_readonly("enforced_sources", &RouteRuleValue::enforced_sources);
    py::class_<RoutePrimitiveRules>(module, "RoutePrimitiveRules")
        .def_readonly("candidate_ordinal", &RoutePrimitiveRules::candidate_ordinal)
        .def_readonly("width", &RoutePrimitiveRules::width)
        .def_readonly("drill", &RoutePrimitiveRules::drill)
        .def_readonly("diameter", &RoutePrimitiveRules::diameter);
    py::class_<RouteLayerRules>(module, "RouteLayerRules")
        .def_property_readonly("allowed", [](const RouteLayerRules &v) { return ids(v.allowed); })
        .def_property_readonly("excluded", [](const RouteLayerRules &v) { return ids(v.excluded); })
        .def_readonly("source", &RouteLayerRules::source)
        .def_readonly("requested_names", &RouteLayerRules::requested_names)
        .def_property_readonly("scope",
                               [](const RouteLayerRules &v) { return static_cast<int>(v.scope); });
    py::class_<RouteCandidate>(module, "RouteCandidate")
        .def_readonly("step_index", &RouteCandidate::step_index)
        .def_property_readonly("kind",
                               [](const RouteCandidate &v) {
                                   return std::holds_alternative<BoardTrack>(v.copper) ? "track"
                                                                                       : "via";
                               })
        .def_property_readonly("net",
                               [](const RouteCandidate &v) {
                                   return std::visit([](const auto &c) { return c.net().index(); },
                                                     v.copper);
                               })
        .def_property_readonly("points",
                               [](const RouteCandidate &v) -> py::tuple {
                                   if (const auto *track = std::get_if<BoardTrack>(&v.copper)) {
                                       auto out = py::tuple{track->points().size()};
                                       for (std::size_t i = 0; i < track->points().size(); ++i)
                                           out[i] = point(track->points()[i]);
                                       return out;
                                   }
                                   return py::make_tuple(
                                       point(std::get<BoardVia>(v.copper).position()));
                               })
        .def_property_readonly("layers",
                               [](const RouteCandidate &v) -> py::tuple {
                                   if (const auto *track = std::get_if<BoardTrack>(&v.copper))
                                       return py::make_tuple(track->layer().index());
                                   const auto &via = std::get<BoardVia>(v.copper);
                                   return py::make_tuple(via.start_layer().index(),
                                                         via.end_layer().index());
                               })
        .def_property_readonly("width_mm",
                               [](const RouteCandidate &v) -> py::object {
                                   if (const auto *track = std::get_if<BoardTrack>(&v.copper))
                                       return py::cast(track->width_mm());
                                   return py::none();
                               })
        .def_property_readonly("drill_mm",
                               [](const RouteCandidate &v) -> py::object {
                                   if (const auto *via = std::get_if<BoardVia>(&v.copper))
                                       return py::cast(via->drill_diameter_mm());
                                   return py::none();
                               })
        .def_property_readonly("diameter_mm", [](const RouteCandidate &v) -> py::object {
            if (const auto *via = std::get_if<BoardVia>(&v.copper))
                return py::cast(via->annular_diameter_mm());
            return py::none();
        });
    py::class_<RouteTargetConstraint>(module, "RouteTargetConstraint")
        .def_readonly("upper_side", &RouteTargetConstraint::upper_side)
        .def_property_readonly("entities",
                               [](const RouteTargetConstraint &v) { return entities(v.entities); })
        .def_readonly("clearance", &RouteTargetConstraint::clearance)
        .def_readonly("exempt", &RouteTargetConstraint::exempt);
    py::class_<RouteTargetResolution>(module, "RouteTargetResolution")
        .def_readonly("step_index", &RouteTargetResolution::step_index)
        .def_property_readonly("position",
                               [](const RouteTargetResolution &v) { return point(v.position); })
        .def_readonly("axis", &RouteTargetResolution::axis)
        .def_readonly("raw_lower_mm", &RouteTargetResolution::raw_lower_mm)
        .def_readonly("raw_upper_mm", &RouteTargetResolution::raw_upper_mm)
        .def_readonly("lower_mm", &RouteTargetResolution::lower_mm)
        .def_readonly("upper_mm", &RouteTargetResolution::upper_mm)
        .def_readonly("width", &RouteTargetResolution::width)
        .def_readonly("constraints", &RouteTargetResolution::constraints);
    py::class_<RouteResolvedStep>(module, "RouteResolvedStep")
        .def_readonly("step_index", &RouteResolvedStep::step_index)
        .def_property_readonly("start", [](const RouteResolvedStep &v) { return point(v.start); })
        .def_property_readonly("end", [](const RouteResolvedStep &v) { return point(v.end); })
        .def_property_readonly("incoming_layer",
                               [](const RouteResolvedStep &v) { return v.incoming_layer.index(); })
        .def_property_readonly("outgoing_layer",
                               [](const RouteResolvedStep &v) { return v.outgoing_layer.index(); })
        .def_readonly("candidates", &RouteResolvedStep::candidates);
    py::class_<RouteFailure>(module, "RouteFailure")
        .def_readonly("kind", &RouteFailure::kind)
        .def_readonly("step_index", &RouteFailure::step_index)
        .def_readonly("message", &RouteFailure::message);
    py::class_<RouteBlocker>(module, "RouteBlocker")
        .def_readonly("step_index", &RouteBlocker::step_index)
        .def_readonly("candidate_ordinal", &RouteBlocker::candidate_ordinal)
        .def_readonly("kind", &RouteBlocker::kind)
        .def_property_readonly("layer", [](const RouteBlocker &v) { return id(v.layer); })
        .def_readonly("message", &RouteBlocker::message)
        .def_readonly("required_mm", &RouteBlocker::required_mm)
        .def_readonly("actual_mm", &RouteBlocker::actual_mm)
        .def_readonly("sources", &RouteBlocker::sources)
        .def_readonly("obstacle_label", &RouteBlocker::obstacle_label)
        .def_readonly("earlier_candidate", &RouteBlocker::earlier_candidate)
        .def_property_readonly("entities",
                               [](const RouteBlocker &v) { return entities(v.entities); })
        .def_property_readonly("witness", [](const RouteBlocker &v) -> py::object {
            if (v.witness)
                return point(*v.witness);
            return py::none();
        });
    py::class_<RouteClearanceCheck>(module, "RouteClearanceCheck")
        .def_readonly("candidate_ordinal", &RouteClearanceCheck::candidate_ordinal)
        .def_property_readonly("layer",
                               [](const RouteClearanceCheck &v) { return v.layer.index(); })
        .def_readonly("clearance", &RouteClearanceCheck::clearance)
        .def_readonly("actual_mm", &RouteClearanceCheck::actual_mm)
        .def_readonly("earlier_candidate", &RouteClearanceCheck::earlier_candidate)
        .def_property_readonly("obstacle_entities", [](const RouteClearanceCheck &v) {
            return entities(v.obstacle_entities);
        });
    py::class_<RouteResult>(module, "RouteResult")
        .def_readonly("committed", &RouteResult::committed)
        .def_readonly("candidate_complete", &RouteResult::candidate_complete)
        .def_property_readonly("net", [](const RouteResult &v) { return id(v.net); })
        .def_readonly("candidates", &RouteResult::candidates)
        .def_readonly("resolved_steps", &RouteResult::resolved_steps)
        .def_readonly("target_resolutions", &RouteResult::target_resolutions)
        .def_readonly("resolved_rules", &RouteResult::resolved_rules)
        .def_readonly("layer_rules", &RouteResult::layer_rules)
        .def_readonly("clearance_checks", &RouteResult::clearance_checks)
        .def_readonly("failure", &RouteResult::failure)
        .def_readonly("blockers", &RouteResult::blockers)
        .def_readonly("message", &RouteResult::message)
        .def_property_readonly("tracks", [](const RouteResult &v) { return ids(v.tracks); })
        .def_property_readonly("vias", [](const RouteResult &v) { return ids(v.vias); })
        .def_property_readonly("committed_order", [](const RouteResult &v) {
            auto out = py::tuple{v.committed_order.size()};
            for (std::size_t i = 0; i < v.committed_order.size(); ++i) {
                out[i] = std::visit(
                    [](const auto &c) {
                        return py::make_tuple(
                            std::same_as<std::decay_t<decltype(c)>, BoardTrackId> ? "track" : "via",
                            c.index());
                    },
                    v.committed_order[i]);
            }
            return out;
        });
}

void bind_route_board(py::class_<PyBoard> &board) {
    board.def_property_readonly("_routing_resolution_builds", &PyBoard::routing_resolution_builds)
        .def("query_gap", &PyBoard::query_gap, py::arg("prefix"))
        .def("route", &PyBoard::route, py::arg("spec"), py::arg("policy") = RoutePolicy::Strict)
        .def("route_pad", &PyBoard::route_pad, py::keep_alive<0, 1>(), py::arg("placement"),
             py::arg("pad"));
}

} // namespace volt::python
