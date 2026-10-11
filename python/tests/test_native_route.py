"""Direct native exception/report parity; no Python routing implementation."""

import gc
import json
import os
import subprocess
import sys
import weakref

import pytest
import volt
from project_framework_helpers import _native_fixture_parts


def source(value):
    return {
        "kind": int(value.kind), "field": int(value.field),
        "net_class": value.net_class, "room": value.room, "keepout": value.keepout,
        "clearance_kinds": list(value.clearance_kinds), "derivation": value.derivation,
    }


def rule(value):
    if value is None:
        return None
    return {
        "value_mm": value.value_mm, "default_mm": value.default_mm,
        "requested_mm": value.requested_mm,
        "sources": [source(s) for s in value.sources],
        "enforced_sources": [source(s) for s in value.enforced_sources],
    }


def snapshot(result):
    failure = result.failure
    out = {
        "committed": result.committed, "candidate_complete": result.candidate_complete,
        "net": result.net, "message": result.message,
        "candidates": [
            {"step_index": c.step_index, "kind": c.kind, "net": c.net,
             "points": c.points, "layers": c.layers, "width_mm": c.width_mm,
             "drill_mm": c.drill_mm, "diameter_mm": c.diameter_mm}
            for c in result.candidates
        ],
        "resolved_steps": [
            {"step_index": s.step_index, "start": s.start, "end": s.end,
             "incoming_layer": s.incoming_layer, "outgoing_layer": s.outgoing_layer,
             "candidates": s.candidates} for s in result.resolved_steps
        ],
        "resolved_rules": [
            {"candidate_ordinal": r.candidate_ordinal, "width": rule(r.width),
             "drill": rule(r.drill), "diameter": rule(r.diameter)}
            for r in result.resolved_rules
        ],
        "layer_rules": {
            "allowed": result.layer_rules.allowed, "excluded": result.layer_rules.excluded,
            "scope": result.layer_rules.scope, "requested_names": result.layer_rules.requested_names,
            "source": source(result.layer_rules.source),
        },
        "failure": None if failure is None else {
            "kind": int(failure.kind), "step_index": failure.step_index, "message": failure.message,
        },
        "blockers": [
            {"step_index": b.step_index, "candidate_ordinal": b.candidate_ordinal,
             "kind": int(b.kind), "layer": b.layer, "message": b.message,
             "witness": b.witness, "required_mm": b.required_mm, "actual_mm": b.actual_mm,
             "sources": [source(s) for s in b.sources], "entities": b.entities,
             "obstacle_label": b.obstacle_label, "earlier_candidate": b.earlier_candidate}
            for b in result.blockers
        ],
        "clearance_checks": [
            {"candidate_ordinal": c.candidate_ordinal, "layer": c.layer,
             "clearance": rule(c.clearance), "actual_mm": c.actual_mm,
             "obstacle_entities": c.obstacle_entities, "earlier_candidate": c.earlier_candidate}
            for c in result.clearance_checks
        ],
        "tracks": result.tracks, "vias": result.vias, "committed_order": result.committed_order,
    }
    return json.loads(json.dumps(out))


def fixture(mode="plain"):
    design = volt.Design("route")
    net = design.net("SIG")
    other = design.net("OTHER")
    resistor = None
    if mode == "unconnected":
        resistor = design.instantiate(_native_fixture_parts()["resistor"], ref="R1")
    board = design.add_board("Main")
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_design_rules(copper_clearance=0.2, min_track_width=0.2,
                           min_via_drill=0.3, min_via_annular=0.7,
                           board_outline_clearance=0.25)
    start = volt.RoutePoint(10, 10)
    steps = [volt.RouteTo(volt.RoutePoint(30, 10))]
    width = None
    if mode == "blocked":
        board.add_track(other, layer=front, points=((25, 5), (25, 15)), width=0.2)
    elif mode == "width":
        width = 0.1
    elif mode == "geometry":
        start = volt.RoutePoint(-sys.float_info.max, 0)
        steps = [volt.RouteTo(volt.RoutePoint(sys.float_info.max, 2))]
    elif mode == "legal":
        steps.insert(0, volt.RouteVia(board.route_layer(back)))
    elif mode == "unconnected":
        placement = board.place(resistor, at=(10, 10))
        start = board.route_pad(placement, 0)
    spec = board.route_spec(start=start, layer=board.route_layer(front), steps=steps,
                            net=board.route_net(net), width=width)
    return design, board, spec


@pytest.mark.parametrize("mode", ["blocked", "width", "geometry", "unconnected"])
def test_direct_cpp_strict_exception_matches_python_and_report(mode):
    executable = os.environ["VOLT_ROUTE_PARITY_FIXTURE"]
    native = json.loads(subprocess.check_output([executable, mode], text=True))
    design, board, spec = fixture(mode)
    before = board.to_json()
    with pytest.raises(volt.RouteError) as caught:
        board.route(spec)
    assert isinstance(caught.value, RuntimeError)
    assert not isinstance(caught.value, volt.VoltError)
    assert str(caught.value) == native["message"] == caught.value.result.message
    assert snapshot(caught.value.result) == native
    assert snapshot(board.route(spec, volt.RoutePolicy.REPORT)) == native
    assert board.to_json() == before
    assert caught.value.result.tracks == caught.value.result.vias == ()
    assert caught.value.result.committed_order == ()
    result = caught.value.result
    del design, board, spec, caught
    gc.collect()
    assert snapshot(result) == native  # owned, not a dangling spatial-index report
    with pytest.raises(AttributeError):
        result.committed = True
    if result.candidates:
        with pytest.raises(AttributeError):
            result.candidates[0].width_mm = 10


def test_report_legal_native_parity_commits_not_dry_run():
    native = json.loads(subprocess.check_output([os.environ["VOLT_ROUTE_PARITY_FIXTURE"], "legal"], text=True))
    _, board, spec = fixture("legal")
    result = board.route(spec, volt.RoutePolicy.REPORT)
    assert result.committed
    assert snapshot(result) == native
    assert result.committed_order == (("via", 0), ("track", 0))
    document = json.loads(board.to_json())
    assert document["board"]["tracks"][0]["points"] == [[10, 10], [30, 10]]
    assert document["board"]["vias"][0]["position"] == [10, 10]


def test_owner_bearing_values_and_structural_error_translation():
    _, board, spec = fixture()
    other_design, other, _ = fixture()
    before = board.to_json()
    wrong = board.route_spec(start=spec.start, layer=other.route_layer(0), steps=spec.steps,
                             net=spec.net)
    with pytest.raises(volt.CrossReferenceError) as caught:
        board.route(wrong, volt.RoutePolicy.REPORT)
    assert caught.value.code == "CrossReferenceViolation"
    wrong_net = board.route_spec(start=spec.start, layer=spec.layer, steps=spec.steps,
                                 net=board.route_net(other_design.nets()[0]))
    with pytest.raises(volt.CrossReferenceError):
        board.route(wrong_net)
    malformed = board.route_spec(start=spec.start, layer=spec.layer, steps=[], net=spec.net)
    with pytest.raises(volt.InvalidArgumentError):
        board.route(malformed, volt.RoutePolicy.REPORT)
    assert board.to_json() == before


def test_literal_axes_do_not_inherit_chamfer_and_request_is_copied():
    _, board, spec = fixture()
    steps = [volt.RouteToX(15), volt.RouteToY(18), volt.RouteTo(volt.RoutePoint(15, 20))]
    request = board.route_spec(start=spec.start, layer=spec.layer, steps=steps, net=spec.net)
    steps.clear()
    with pytest.raises(AttributeError):
        request.width_mm = 3
    result = board.route(request)
    assert [c.points for c in result.candidates] == [((10, 10), (15, 10)), ((15, 10), (15, 18)), ((15, 18), (15, 20))]


def test_unrelated_native_argument_error_is_not_route_error():
    with pytest.raises(ValueError) as caught:
        volt.RoutePoint(float("nan"), 0)
    assert not isinstance(caught.value, volt.RouteError)


def test_request_and_extracted_views_retain_native_owners_after_gc():
    design, board, original = fixture()
    board_ref = weakref.ref(board._native)
    circuit_ref = weakref.ref(design._circuit)
    # Extracted native views must retain the request and its owner-bearing values.
    start, layer, terminal, net = original.start, original.layer, original.steps[0], original.net
    del original, design, board
    gc.collect()
    assert board_ref() is not None and circuit_ref() is not None
    request = volt.RouteSpec(board_ref(), start, layer, [terminal], net)
    del start, layer, terminal, net
    gc.collect()
    assert board_ref().route(request).committed
