"""Exact native PCB diagnostic and review-artifact regression checks.

The policy is a reviewed ordered list, never inferred from the current build.
Geometry comes from kernel diagnostics; this module does not detect collisions.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import importlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

if not __debug__:
    raise RuntimeError("PCB regression checks require Python without optimization (-O)")


def diagnostic_snapshot(report):
    fields = ("code", "severity", "category", "message", "entities", "overlays", "measurement", "rule")
    return json.loads(json.dumps([
        {field: asdict(item)[field] for field in fields} for item in report
    ]))


def check_diagnostics(report, expected):
    actual = diagnostic_snapshot(report)
    if actual != expected:
        raise AssertionError("PCB diagnostic regression differs from the reviewed ordered policy:\n"
                             + json.dumps(actual, indent=2))


def _refs(entities):
    return " ".join(f"{entity['kind']}:{entity['index']}" for entity in entities)


def check_svg(svg, diagnostics):
    root = ET.fromstring(svg)
    labels = [node for node in root.iter() if "diagnostic-label" in node.get("class", "").split()]
    assert [(n.get("data-diagnostic-code"), n.get("data-entities")) for n in labels] == [
        (d["code"], _refs(d["entities"])) for d in diagnostics
    ]
    nodes = [node for node in root.iter() if "diagnostic-overlay" in node.get("class", "").split()]
    expected = [(i, j, d, o) for i, d in enumerate(diagnostics) for j, o in enumerate(d["overlays"])]
    assert len(nodes) == len(expected)
    for node, (i, j, diagnostic, overlay) in zip(nodes, expected):
        assert node.get("data-diagnostic-index") == str(i)
        assert node.get("data-overlay-index") == str(j)
        assert node.get("data-diagnostic-code") == diagnostic["code"]
        assert node.get("data-entities") == _refs(diagnostic["entities"])
        assert node.get("data-overlay-entities") == _refs(overlay["entities"])
        assert node.get("data-layers") == _refs(overlay["layers"])
        points = overlay["points"]
        kind = overlay["kind"]
        tag, token = {"bounding_box": ("rect", "bounding-box"),
                      "point": ("circle", "point"), "segment": ("line", "segment"),
                      "polygon": ("polygon", "polygon")}[kind]
        assert node.tag == f"{{http://www.w3.org/2000/svg}}{tag}"
        assert token in node.get("class").split()
        assert diagnostic["severity"] in node.get("class").split()
        if kind == "bounding_box":
            (x1, y1), (x2, y2) = points
            names, values = ("x", "y", "width", "height"), (min(x1, x2), min(y1, y2), abs(x2-x1), abs(y2-y1))
        elif kind == "point":
            names, values = ("cx", "cy"), points[0]
        elif kind == "segment":
            names, values = ("x1", "y1", "x2", "y2"), (*points[0], *points[1])
        else:
            assert kind == "polygon"
            names = ()
            values = ()
            actual_points = [[float(v) for v in p.split(",")] for p in node.get("points").split()]
            assert len(actual_points) == len(points)
            assert all(len(point) == 2 for point in actual_points)
            assert all(abs(a-b) <= 0.000001 for ap, ep in zip(actual_points, points) for a, b in zip(ap, ep))
        assert all(abs(float(node.get(name))-value) <= 0.000001 for name, value in zip(names, values))


def check_bundle_diagnostics(bundle, result):
    manifest = json.loads((bundle / "manifest.volt.json").read_text())
    path = next(a["path"] for a in manifest["artifacts"] if a["kind"] == "diagnostics")
    persisted = json.loads((bundle / path).read_text())["diagnostics"]
    expected = json.loads(json.dumps([asdict(d) for d in result.diagnostics]))
    assert len(persisted) == len(expected)
    for actual, diagnostic in zip(persisted, expected):
        assert {key: actual[key] for key in diagnostic} == diagnostic


def review_artifacts(result, board_name):
    board = result.board(board_name)
    native = diagnostic_snapshot(board.validate())
    project = [d for d in result.diagnostics if d.report == "pcb.board" and d.board == board_name]
    assert diagnostic_snapshot(project) == native
    assert all(d.stage == "board" and d.source == f"pcb:{board_name}" for d in project)
    document = board.to_json()
    logical = result.design().to_json()
    def identifiers(value):
        if isinstance(value, dict):
            found = {value["id"]} if isinstance(value.get("id"), str) else set()
            return found.union(*(identifiers(v) for v in value.values()))
        if isinstance(value, list):
            return set().union(*(identifiers(v) for v in value))
        return set()
    known = identifiers(json.loads(document)) | identifiers(json.loads(logical))
    for diagnostic in native:
        references = diagnostic["entities"] + [e for o in diagnostic["overlays"] for e in o["entities"] + o["layers"]]
        assert all(_refs([e]) in known for e in references)
    svg = board.to_svg(pad_net_overlays=False, ratsnest_edges=False)
    check_svg(svg, native)
    artifacts = {"pcb.json": document, "pcb.svg": svg,
                 "diagnostics.json": json.dumps([asdict(d) for d in result.diagnostics], indent=2) + "\n",
                 "logical.json": logical}
    for index, layer in enumerate(json.loads(document)["board"]["layers"]):
        artifacts[f"pcb.{layer['name'].replace('/', '_')}.svg"] = board.to_svg(
            pad_net_overlays=False, ratsnest_edges=False, layer=index)
    return artifacts


def input_identity(root, paths):
    return {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in paths}


def package_identity(package):
    # Bytecode caches are generated locally; pin all distributed package files.
    paths = sorted(str(p.relative_to(package)) for p in package.rglob("*")
                   if p.is_file() and "__pycache__" not in p.relative_to(package).parts)
    return input_identity(package, paths)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--consumer-root", type=Path, required=True)
    parser.add_argument("--volt-root", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    policy = json.loads(args.policy.read_text())
    root = args.consumer_root.resolve()
    volt_root = args.volt_root.resolve()
    revision = policy["volt_revision"]
    subprocess.run(["git", "-C", str(volt_root), "merge-base", "--is-ancestor", revision, "HEAD"], check=True)
    # Test/doc changes can use the same exact merged kernel dependency; production must match.
    delta = subprocess.check_output(["git", "-C", str(volt_root), "diff", revision, "--",
                                     "src", "include", "python/volt"], text=True)
    assert not delta, "Volt production sources differ from the pinned merged dependency"
    assert input_identity(root, policy["inputs"]) == policy["inputs"], "Consumer input identity changed"
    package = volt_root / "build/dev/python/volt"
    expected_package = policy["volt_package_files"]
    assert package_identity(package) == expected_package, "Volt built package differs from the reviewed identity"
    # -B prevents cache writes, but still reads caches. Use an empty cache namespace
    # for the entire consumer run so only reviewed Python source can be imported.
    with tempfile.TemporaryDirectory(prefix="volt-review-pycache-") as cache:
        sys.pycache_prefix = cache
        sys.dont_write_bytecode = True
        sys.path[:0] = [str(package.parent), str(root)]
        import volt
        assert Path(volt.__file__).resolve() == package / "__init__.py"
        extension = Path(volt._volt.__file__).resolve()
        assert extension.parent == package and extension.name in expected_package
        assert hashlib.sha256(extension.read_bytes()).hexdigest() == expected_package[extension.name]
        module = importlib.import_module(policy["module"])
        outputs = []
        for index in (1, 2):
            result = module.build_project().run()
            assert result.ok, f"Consumer project failed: {result.test_failures()}"
            check_diagnostics(result.board(policy["board"]).validate(), policy["diagnostics"])
            artifacts = review_artifacts(result, policy["board"])
            destination = args.output / f"build-{index}"
            destination.mkdir(parents=True, exist_ok=False)
            result.write(destination / "project.volt")
            check_bundle_diagnostics(destination / "project.volt", result)
            for name, content in artifacts.items():
                (destination / name).write_text(content, encoding="utf-8")
            outputs.append({str(p.relative_to(destination)): p.read_bytes()
                            for p in destination.rglob("*") if p.is_file()})
        assert outputs[0] == outputs[1], "Two pinned builds produced different artifacts"
        hashes = {name: hashlib.sha256(data).hexdigest() for name, data in outputs[0].items()}
        (args.output / "review-hashes.json").write_text(json.dumps(hashes, indent=2, sort_keys=True) + "\n")
        provenance = {"volt_revision": revision, "volt_package": str(Path(volt.__file__).resolve()),
                      "extension": str(extension), "extension_sha256": hashlib.sha256(extension.read_bytes()).hexdigest(),
                      "volt_package_files": expected_package,
                      "inputs": policy["inputs"], "board": policy["board"]}
        (args.output / "provenance.json").write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n")
        print(f"PASS: exact native policy, project/SVG agreement, {len(hashes)} matching artifacts")


if __name__ == "__main__":
    main()
