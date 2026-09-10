#!/usr/bin/env python3
"""Validate or explicitly regenerate the offline linear-DC ngspice evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORPUS_DIR = ROOT / "tests" / "fixtures" / "linear_dc"
CORPUS_PATH = CORPUS_DIR / "corpus.json"
EVIDENCE_PATH = CORPUS_DIR / "ngspice-46-results.json"
NUMBER = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"
PRINTED_VALUE = re.compile(rf"^\s*(.+?)\s*=\s*({NUMBER})\s*$")
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain one JSON object")
    return value


def deck_digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ngspice_version(executable: Path) -> str:
    completed = subprocess.run(
        [str(executable), "--version"], check=True, capture_output=True, text=True
    )
    match = re.search(r"ngspice-(\d+)", completed.stdout + completed.stderr)
    if match is None:
        raise RuntimeError("could not identify ngspice version")
    return f"ngspice-{match.group(1)}"


def run_deck(executable: Path, deck: Path) -> dict[str, float]:
    completed = subprocess.run(
        [str(executable), "-b", str(deck)], check=True, capture_output=True, text=True
    )
    observations: dict[str, float] = {}
    for line in completed.stdout.splitlines():
        match = PRINTED_VALUE.match(line)
        if match is not None:
            observations[match.group(1).strip().lower()] = float(match.group(2))
    return observations


def check_observation(case_id: str, name: str, expected: dict, actual: float) -> None:
    target = float(expected["value"])
    absolute = float(expected["absolute_tolerance"])
    relative = float(expected["relative_tolerance"])
    if not all(math.isfinite(value) and value >= 0.0 for value in (absolute, relative)):
        raise ValueError(f"{case_id}/{name} has an invalid comparison tolerance")
    if not math.isfinite(actual):
        raise ValueError(f"{case_id}/{name} produced a non-finite oracle value")
    limit = absolute + relative * abs(target)
    if abs(actual - target) > limit:
        raise ValueError(
            f"{case_id}/{name}: ngspice {actual:.17g} != analytical "
            f"{target:.17g} within {limit:.3g}"
        )


def validate(corpus: dict, evidence: dict) -> None:
    if corpus.get("format") != "volt.linear-dc-reference-corpus" or corpus.get("version") != 1:
        raise ValueError("unsupported linear-DC corpus contract")
    if evidence.get("format") != "volt.linear-dc-ngspice-evidence" or evidence.get("version") != 1:
        raise ValueError("unsupported ngspice evidence contract")
    if evidence.get("backend", {}).get("version") != "ngspice-46":
        raise ValueError("checked-in oracle evidence must remain pinned to ngspice-46")

    expected_cases = {case["id"]: case for case in corpus["supported_cases"]}
    compiled_cases = corpus["compiled_expectations"]
    if compiled_cases.keys() != expected_cases.keys():
        raise ValueError("compiled expectation case set differs from the analytical corpus")
    for case_id, expectation in compiled_cases.items():
        if expectation["coverage"] != "complete":
            raise ValueError(f"{case_id}: supported oracle case must require complete coverage")
        if expectation["branch_count"] < 1 or expectation["storage_count"] < 0:
            raise ValueError(f"{case_id}: invalid compiled graph count")
        if len(expectation["origins"]) != expectation["branch_count"]:
            raise ValueError(f"{case_id}: every compiled branch must have an expected origin")
    actual_cases = {case["id"]: case for case in evidence["cases"]}
    if actual_cases.keys() != expected_cases.keys():
        raise ValueError("oracle evidence case set differs from the analytical corpus")

    for case_id, case in expected_cases.items():
        deck = CORPUS_DIR / case["deck"]
        actual = actual_cases[case_id]
        if actual["deck"] != case["deck"] or actual["deck_sha256"] != deck_digest(deck):
            raise ValueError(f"{case_id}: deck identity differs from checked-in evidence")
        observations = actual["observations"]
        oracle_names = {
            name for name, expectation in case["expectations"].items() if "ngspice" in expectation
        }
        if observations.keys() != oracle_names:
            raise ValueError(f"{case_id}: normalized observation set differs from the corpus")
        for name in oracle_names:
            check_observation(case_id, name, case["expectations"][name], observations[name])


def regenerate(corpus: dict, executable: Path, executed_on: str) -> dict:
    version = ngspice_version(executable)
    if version != "ngspice-46":
        raise RuntimeError(f"reference regeneration requires ngspice-46, found {version}")
    cases = []
    for case in corpus["supported_cases"]:
        deck = CORPUS_DIR / case["deck"]
        printed = run_deck(executable, deck)
        observations = {}
        for name, expectation in case["expectations"].items():
            expression = expectation.get("ngspice")
            if expression is None:
                continue
            expression = expression.lower()
            if expression not in printed:
                raise RuntimeError(f"{case['id']}: ngspice did not print {expression}")
            observations[name] = printed[expression]
        cases.append(
            {
                "id": case["id"],
                "deck": case["deck"],
                "deck_sha256": deck_digest(deck),
                "command": ["ngspice", "-b", f"tests/fixtures/linear_dc/{case['deck']}"],
                "observations": observations,
            }
        )
    return {
        "format": "volt.linear-dc-ngspice-evidence",
        "version": 1,
        "backend": {
            "name": "ngspice",
            "version": version,
            "verified_executable": str(executable),
        },
        "executed_on": executed_on,
        "invocation": "python3 scripts/regenerate-dc-reference.py --regenerate --ngspice /opt/homebrew/bin/ngspice --executed-on YYYY-MM-DD",
        "cases": cases,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--regenerate", action="store_true", help="invoke ngspice and rewrite evidence")
    parser.add_argument("--ngspice", type=Path, help="explicit ngspice executable")
    parser.add_argument("--executed-on", help="recorded YYYY-MM-DD execution date")
    args = parser.parse_args()
    corpus = load_json(CORPUS_PATH)
    if args.regenerate:
        if args.ngspice is None or args.executed_on is None or DATE.fullmatch(args.executed_on) is None:
            parser.error("--regenerate requires --ngspice and --executed-on YYYY-MM-DD")
        evidence = regenerate(corpus, args.ngspice.resolve(), args.executed_on)
        validate(corpus, evidence)
        EVIDENCE_PATH.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
        print(f"wrote verified {EVIDENCE_PATH.relative_to(ROOT)}")
    else:
        if args.ngspice is not None or args.executed_on is not None:
            parser.error("--ngspice and --executed-on are only valid with --regenerate")
        validate(corpus, load_json(EVIDENCE_PATH))
        print("linear-DC analytical corpus and checked-in ngspice evidence are consistent")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (KeyError, OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
