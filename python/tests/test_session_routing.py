import gc
import json
import weakref

import pytest
import volt

from test_native_route import snapshot


def fixture():
    design = volt.Design("session-synthetic")
    net = design.net("SIG")
    other = design.net("OTHER")
    board = design.add_board("Main")
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0, 0), size=(100, 100))
    return design, net, other, board, front, back


def test_fluent_native_complete_payload_and_owner_gc_retention():
    design, net, other, board, front, back = fixture()
    owner = weakref.ref(design)
    with board.layout() as layout:
        builder = layout.route(net, layer=front).at((10, 10)).right(3).via(back)
        del design
        gc.collect()
        assert owner() is not None
        result = builder.to((20, 10))
    assert result.committed and result.tracks == (0, 1) and result.vias == (0,)
    assert board._native._routing_resolution_builds == 1
    for y in (20, 30, 40):
        with board.layout() as layout:
            assert layout.connect((10, y), (20, y), layer=front, net=net).committed
    assert board._native._routing_resolution_builds == 1
    with pytest.raises(ValueError, match="materialized"):
        builder.finish()


def test_class_change_with_unchanged_copper_and_strict_report_cursor_atomicity():
    design, net, other, board, front, back = fixture()
    with board.layout(at=(10, 10)) as layout:
        assert layout.connect((10, 10), (20, 10), net=net, layer=front).committed
        klass = design.net_class("Wide", track_width=0.6)
        klass.assign(net)
        updated = layout.connect((10, 20), (20, 20), net=net, layer=front)
        assert [c.width_mm for c in updated.candidates] == [0.6]
        board.add_track(other, layer=front, points=((15, 25), (15, 35)), width=0.2)
        before = board.to_json()
        cursor = layout.here.point
        spec = board.route_spec(start=volt.RoutePoint(10, 30), layer=board.route_layer(front),
                                steps=[volt.RouteTo(volt.RoutePoint(20, 30))], net=board.route_net(net))
        native = board.route(spec, volt.RoutePolicy.REPORT)
        builder = layout.route(net, layer=front, policy=volt.RoutePolicy.REPORT).at((10, 30))
        assert snapshot(builder.to((20, 30))) == snapshot(native)
        assert layout.here.point == cursor and board.to_json() == before
        with pytest.raises(volt.RouteError) as error:
            layout.connect((10, 30), (20, 30), net=net, layer=front)
        assert str(error.value) == native.message
        assert snapshot(error.value.result) == snapshot(native)
        assert layout.here.point == cursor and board.to_json() == before
        assert builder.to((10, 40)).committed  # an uncommitted builder can be retried


def test_via_stitch_and_fanout_use_native_results_and_paired_overrides():
    design, net, other, board, front, back = fixture()
    with board.layout(at=(10, 10)) as layout:
        before = board.to_json()
        with pytest.raises(ValueError, match="both drill and annular"):
            layout.via(net, at=(10, 10), start_layer=front, end_layer=back, drill=0.3)
        assert board.to_json() == before
        route = layout.via(net, at=(10, 10), start_layer=front, end_layer=back)
        assert route.committed and route.tracks == () and route.vias == (0,)
        cursor = layout.here.point
        stitched = layout.stitch(net, at=((10, 20), (10, 30)), start_layer=front, end_layer=back)
        assert all(r.committed for r in stitched) and layout.here.point == cursor
        fanout = layout.fanout((volt.BoardAnchor((30, 30), board=board),), net=net,
                              layer=front, direction="right", distance=2, via_layers=(front, back))
        assert fanout[0].result.committed and fanout[0].end.point == (32, 30)
        assert len(fanout[0].result.vias) == 1 and layout.here.point == cursor


def test_fanout_reports_native_source_without_snapping_or_postcommit_conversion():
    design, net, other, board, front, back = fixture()
    with board.layout(grid=0.5) as layout:
        result = layout.fanout(((30.1, 30.1),), net=net, layer=front,
                               direction="right", distance=2)[0]
        assert result.source.point == result.result.resolved_steps[0].start == (30.1, 30.1)
        assert result.end.point == result.result.resolved_steps[-1].end == (32.1, 30.1)
        via = layout.via(net, at=(40.1, 40.1), start_layer=front, end_layer=back)
        result = layout.fanout((board.route_via(via.vias[0]),), net=net,
                               layer=back, direction="right", distance=2)[0]
        assert result.result.committed
        assert result.source.point == result.result.resolved_steps[0].start == (40.1, 40.1)
        before = board.to_json()
        with pytest.raises(TypeError):
            layout.fanout((object(),), net=net, layer=front, direction="right", distance=2)
        assert board.to_json() == before


def test_report_fanout_has_no_guessed_source_when_native_geometry_is_unresolvable():
    design, net, other, board, front, back = fixture()
    with board.layout() as layout:
        before = board.to_json()
        item = layout.fanout(((1.7976931348623157e308, 0),), net=net,
                             layer=front, direction="right", distance=1.7976931348623157e308,
                             policy=volt.RoutePolicy.REPORT)[0]
        assert not item.result.committed and not item.result.candidate_complete
        assert item.source is None and item.end is None
        assert board.to_json() == before
