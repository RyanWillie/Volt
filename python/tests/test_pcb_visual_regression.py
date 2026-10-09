"""Shareable routed fixture; private consumer sources stay outside Volt."""
import importlib.util
import json
from pathlib import Path

import pytest
import volt
from project_framework_helpers import _delivery_profile, _native_fixture_parts

_SCRIPT = Path(__file__).resolve().parents[2] / "scripts/pcb_visual_regression.py"
_spec = importlib.util.spec_from_file_location("pcb_visual_regression", _SCRIPT)
regression = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(regression)


def build_project(variant="clean", expected=()):
    project = volt.Project("routed-visual-regression")
    authored_board = None

    @project.design
    def design():
        d = volt.Design(project.name)
        part = _native_fixture_parts()["resistor"]
        first = d.instantiate(part, ref="R1").dnp(False)
        second = d.instantiate(part, ref="R2").dnp(False)
        a = d.net("A")
        a += first[1], second[1]
        b = d.net("B")
        b += first[2], second[2]
        return d

    @project.board
    def board(context):
        nonlocal authored_board
        d = context.design()
        pcb = d.add_board("Regression")
        front = pcb.add_layer("F.Cu", role="copper", side="top")
        back = pcb.add_layer("B.Cu", role="copper", side="bottom")
        silk = pcb.add_layer("F.SilkS", role="silkscreen", side="top")
        pcb.set_layer_stack((front, back), thickness=1.6)
        pcb.set_capability_profile(_delivery_profile())
        pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        pcb.place(d.component("R1"), at=(6, 5))
        pcb.place(d.component("R2"), at=(6, 5) if variant == "placement" else (14, 5))
        nets = {n.name: n for n in d.nets()}
        pcb.add_track(nets["A"], layer=front, points=((5.25, 5), (5.25, 2), (13.25, 2), (13.25, 5)), width=0.25)
        pcb.add_track(nets["B"], layer=front, points=((6.75, 5), (6.75, 8), (14.75, 8), (14.75, 5)), width=0.25)
        pcb.add_text("REV A", at=(8, 1.5), layer=silk, size=0.6)
        if variant in ("advisory", "extra"):
            pcb.add_text("REVIEW", at=(21, 1), layer=silk, size=0.6)
        if variant == "extra":
            pcb.add_text("EXTRA", at=(21, 9), layer=silk, size=0.6)
        if variant == "text":
            pcb.add_text("CONFLICT", at=(8, 1.5), layer=silk, size=0.6)
        if variant == "hole":
            pcb.add(volt.Hole(center=(10, 5), diameter=2, role="mounting"))
            pcb.add_text("X", at=(10, 5), layer=silk, size=0.6)
        if variant == "feature":
            pcb.add(volt.Hole(center=(20, 9), diameter=2, role="mounting"))
        authored_board = pcb
        return pcb

    @project.board.test
    def exact_visual_policy(check):
        regression.check_diagnostics(authored_board.validate(), list(expected))

    return project


def test_routed_visual_benchmark_is_clean_and_deterministic(tmp_path):
    first = build_project().run()
    second = build_project().run()
    assert first.ok and second.ok
    artifacts = regression.review_artifacts(first, "Regression")
    assert artifacts == regression.review_artifacts(second, "Regression")
    fixture = Path(__file__).resolve().parents[2] / "tests/fixtures/pcb_visual_regression"
    for name in ("pcb.json", "pcb.svg"):
        assert artifacts[name] == (fixture / name).read_text(encoding="utf-8")
    first.write(tmp_path / "first.volt")
    second.write(tmp_path / "second.volt")
    regression.check_bundle_diagnostics(tmp_path / "first.volt", first)
    regression.check_bundle_diagnostics(tmp_path / "second.volt", second)
    def contents(root):
        return {p.relative_to(root): p.read_bytes() for p in root.rglob("*") if p.is_file()}
    assert contents(tmp_path / "first.volt") == contents(tmp_path / "second.volt")


@pytest.mark.parametrize("variant,code,category", [
    ("advisory", "PCB_VISUAL_LABEL_OUTSIDE_BOARD", "pcb.visual"),
    ("text", "PCB_VISUAL_LABEL_OVERLAP", "pcb.visual"),
    ("placement", "PCB_VISUAL_PLACEMENT_OVERLAP", "pcb.visual"),
    ("feature", "PCB_BOARD_FEATURE_OUTSIDE_OUTLINE", "pcb.board"),
    ("hole", "PCB_VISUAL_LABEL_OBSTRUCTION", "pcb.visual"),
])
def test_native_visual_broken_variants_fail_project_gate(variant, code, category):
    result = build_project(variant).run()
    assert not result.ok
    assert [f.name for f in result.test_failures()] == ["exact_visual_policy"]
    native = result.board().validate()
    matches = [d for d in native if d.code == code]
    assert len(matches) == 1
    diagnostic = matches[0]
    assert diagnostic.category == category
    assert diagnostic.severity == ("error" if variant == "feature" else "warning")
    entities = {
        "advisory": (("board_text", 1),),
        "text": (("board_text", 0), ("board_text", 1)),
        "placement": (("component", 0), ("component_placement", 0),
                      ("component", 1), ("component_placement", 1)),
        "feature": (("board_feature", 0),),
        "hole": (("board_text", 1), ("board_feature", 0)),
    }[variant]
    assert diagnostic.entities == tuple(volt.DiagnosticEntity(*e) for e in entities)
    assert diagnostic.rule == ("board-text-over-hole" if variant == "hole" else None)
    assert regression.diagnostic_snapshot(native) == regression.diagnostic_snapshot(
        build_project(variant).run().board().validate())
    regression.review_artifacts(result, "Regression")


def test_extra_same_code_conflict_is_not_accepted_by_reviewed_policy(tmp_path):
    fixture = Path(__file__).resolve().parents[2] / "tests/fixtures/pcb_visual_regression"
    expected = json.loads((fixture / "advisory.json").read_text())
    accepted = build_project("advisory", expected).run()
    assert accepted.ok
    assert len(accepted.board().validate()) == 1
    artifacts = regression.review_artifacts(accepted, "Regression")
    assert artifacts["pcb.svg"] == (fixture / "advisory.svg").read_text()
    accepted.write(tmp_path / "advisory.volt")
    regression.check_bundle_diagnostics(tmp_path / "advisory.volt", accepted)
    broken = build_project("extra", expected).run()
    assert not broken.ok
    assert len(broken.board().validate()) == 2
    assert {d.code for d in broken.board().validate()} == {"PCB_VISUAL_LABEL_OUTSIDE_BOARD"}
    assert broken.board().validate()[1].entities == (volt.DiagnosticEntity("board_text", 2),)
    assert [f.name for f in broken.test_failures()] == ["exact_visual_policy"]


@pytest.mark.parametrize("damage", ["tag", "entity", "geometry", "code", "count"])
def test_svg_agreement_rejects_tampered_native_overlay(damage):
    import xml.etree.ElementTree as ET
    result = build_project("hole").run()
    board = result.board()
    root = ET.fromstring(board.to_svg())
    overlay = next(n for n in root.iter() if "diagnostic-overlay" in n.get("class", "").split())
    if damage == "tag":
        overlay.tag = "{http://www.w3.org/2000/svg}line"
    elif damage == "entity":
        overlay.set("data-overlay-entities", "board_text:999")
    elif damage == "geometry":
        overlay.set("x", "999")
    elif damage == "code":
        overlay.set("data-diagnostic-code", "WRONG_CODE")
    else:
        parent = next(n for n in root.iter() if overlay in list(n))
        parent.remove(overlay)
    with pytest.raises(AssertionError):
        regression.check_svg(ET.tostring(root), regression.diagnostic_snapshot(board.validate()))


@pytest.mark.parametrize("damage", ["extension", "python", "missing", "extra"])
def test_consumer_runner_rejects_stale_package_before_import(tmp_path, monkeypatch, damage):
    package = tmp_path / "build/dev/python/volt"
    package.mkdir(parents=True)
    (package / "__init__.py").write_text("# reviewed Python package\n")
    (package / "_volt.so").write_bytes(b"reviewed extension")
    expected = regression.package_identity(package)
    assert set(expected) == {"__init__.py", "_volt.so"}
    policy = tmp_path / "policy.json"
    policy.write_text(json.dumps({"volt_revision": "reviewed-revision", "inputs": {},
                                  "volt_package_files": expected}))
    if damage == "extension":
        (package / "_volt.so").write_bytes(b"stale extension")
    elif damage == "python":
        (package / "__init__.py").write_text("# stale Python package\n")
    elif damage == "missing":
        (package / "__init__.py").unlink()
    else:
        (package / "stale.py").write_text("# leftover package module\n")
    output = tmp_path / "output"
    monkeypatch.setattr(regression.sys, "argv", [str(_SCRIPT), "--consumer-root", str(tmp_path),
                        "--volt-root", str(tmp_path), "--policy", str(policy), "--output", str(output)])
    monkeypatch.setattr(regression.subprocess, "run", lambda *args, **kwargs: None)
    monkeypatch.setattr(regression.subprocess, "check_output", lambda *args, **kwargs: "")
    original_path = list(regression.sys.path)
    with pytest.raises(AssertionError, match="Volt built package differs from the reviewed identity"):
        regression.main()
    assert regression.sys.path == original_path  # Failure precedes package/consumer import.
    assert not output.exists()


def test_consumer_runner_ignores_valid_stale_bytecode(tmp_path):
    import os
    import py_compile
    import subprocess
    import sys

    package = tmp_path / "build/dev/python/volt"
    package.mkdir(parents=True)
    source = package / "__init__.py"
    stale = 'raise RuntimeError("UNREVIEWED CACHE")\n'
    reviewed = 'raise RuntimeError("REVIEWED SOURCE") \n'
    assert len(stale) == len(reviewed)
    source.write_text(stale)
    stamp = source.stat().st_mtime
    py_compile.compile(str(source), doraise=True,
                       invalidation_mode=py_compile.PycInvalidationMode.TIMESTAMP)
    source.write_text(reviewed)
    os.utime(source, (stamp, stamp))  # Retain a valid timestamp/size cache header.
    policy = tmp_path / "policy.json"
    policy.write_text(json.dumps({"volt_revision": "reviewed-revision", "inputs": {},
                                  "volt_package_files": regression.package_identity(package)}))
    baseline = subprocess.run([sys.executable, "-B", "-c",
        "import sys; sys.path.insert(0, sys.argv[1]); import volt", str(package.parent)],
        capture_output=True, text=True)
    assert "RuntimeError: UNREVIEWED CACHE" in baseline.stderr
    # Isolate revision/source checks: this regression targets actual Python import behavior.
    driver = """
import runpy, sys
runner = runpy.run_path(sys.argv[1])
runner['subprocess'].run = lambda *args, **kwargs: None
runner['subprocess'].check_output = lambda *args, **kwargs: ''
sys.argv = sys.argv[1:]
runner['main']()
"""
    output = tmp_path / "output"
    checked = subprocess.run([sys.executable, "-B", "-c", driver, str(_SCRIPT),
        "--consumer-root", str(tmp_path), "--volt-root", str(tmp_path),
        "--policy", str(policy), "--output", str(output)], capture_output=True, text=True)
    assert "RuntimeError: REVIEWED SOURCE" in checked.stderr
    assert "RuntimeError: UNREVIEWED CACHE" not in checked.stderr
    assert not output.exists()
