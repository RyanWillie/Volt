#!/usr/bin/env python3
"""Opt-in end-to-end check of Volt's public ngspice 46 DC adapter.

This is deliberately not a default CTest: it requires a caller-supplied ngspice 46 executable.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import _ngspice_dc_cases as cases
import volt


ROOT = Path(__file__).resolve().parents[1]
CORPUS_PATH = ROOT / "tests" / "fixtures" / "linear_dc" / "corpus.json"
NGSPICE_ARTIFACTS = {
    "request.json",
    "compile-report.json",
    "ngspice-analysis.json",
    "deck.cir",
    "ngspice-process.json",
    "ngspice-output.txt",
    "solve-report.json",
    "solution.json",
}
NATIVE_ARTIFACTS = {
    "request.json",
    "compile-report.json",
    "solve-report.json",
    "solution.json",
}


def _json(path: Path) -> dict:
    return json.loads(path.read_bytes())


def _cli_environment() -> dict[str, str]:
    environment = os.environ.copy()
    python_paths = [str(Path(path).resolve()) for path in sys.path if path]
    package_root = str(Path(volt.__file__).resolve().parents[1])
    environment["PYTHONPATH"] = os.pathsep.join(
        dict.fromkeys((package_root, *python_paths))
    )
    return environment


def _run_cli(cwd: Path, *arguments: str, environment: dict[str, str]) -> dict:
    completed = subprocess.run(
        [sys.executable, "-m", "volt.cli", "simulate", *arguments, "--json"],
        cwd=cwd,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"public CLI failed ({completed.returncode}):\n"
            f"stdout: {completed.stdout}\nstderr: {completed.stderr}"
        )
    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(
            f"public CLI did not emit one JSON envelope: {completed.stdout!r}"
        ) from error
    if payload.get("format") != "volt.cli-result" or payload.get("status") != "success":
        raise RuntimeError(f"public CLI returned an unexpected envelope: {payload}")
    return payload


def _probe_values(solution: dict) -> dict[str, float]:
    values = {
        probe["key"]: probe["value"]["si"]
        for probe in solution["observations"]["probes"]
    }
    nodes = {
        node["node"]: node["potential"]["si"]
        for node in solution["observations"]["nodes"]
    }
    for node in solution["model"]["nodes"]:
        origin = node["origin"]
        if origin["kind"] == "internal_node":
            values[f"{origin['node']}_voltage_v"] = nodes[node["id"]]
    return values


def _close(actual: float, expected: float, specification: dict) -> bool:
    limit = specification["absolute_tolerance"] + (
        specification["relative_tolerance"] * abs(expected)
    )
    return abs(actual - expected) <= limit


def _check_observations(case: dict, native: dict, ngspice: dict) -> int:
    native_values = _probe_values(native)
    ngspice_values = _probe_values(ngspice)
    checked = 0
    for name, expectation in case["expectations"].items():
        if "ngspice" not in expectation:
            continue
        if name not in native_values or name not in ngspice_values:
            raise AssertionError(f"{case['id']}/{name}: direct observation is missing")
        expected = expectation["value"]
        for backend, value in (("native", native_values[name]), ("ngspice", ngspice_values[name])):
            if not _close(value, expected, expectation):
                raise AssertionError(
                    f"{case['id']}/{name}: {backend} {value:.17g} is outside the corpus tolerance "
                    f"for {expected:.17g}"
                )
        if not _close(ngspice_values[name], native_values[name], expectation):
            raise AssertionError(
                f"{case['id']}/{name}: ngspice {ngspice_values[name]:.17g} and native "
                f"{native_values[name]:.17g} differ outside the corpus tolerance"
            )
        checked += 1
    return checked


def _check_native(output: Path, payload: dict) -> dict:
    if payload.get("backend", "native") != "native" or payload.get("backend_report") is not None:
        raise AssertionError("native CLI envelope has unexpected backend attribution")
    if payload.get("process") is not None:
        raise AssertionError("native CLI envelope must not contain external-process evidence")
    if set(path.name for path in output.iterdir()) != NATIVE_ARTIFACTS:
        raise AssertionError(f"unexpected native artifacts in {output}")
    return _json(output / "solution.json")


def _check_ngspice(output: Path, payload: dict, executable: Path) -> dict:
    if payload["backend"] != "ngspice":
        raise AssertionError("ngspice CLI envelope has incorrect backend attribution")
    if set(path.name for path in output.iterdir()) != NGSPICE_ARTIFACTS:
        raise AssertionError(f"unexpected ngspice artifacts in {output}")

    analysis = _json(output / "ngspice-analysis.json")
    process = _json(output / "ngspice-process.json")
    report = _json(output / "solve-report.json")
    solution = _json(output / "solution.json")
    deck = (output / "deck.cir").read_bytes()
    external_output = (output / "ngspice-output.txt").read_bytes()
    deck_identity = "sha256:" + hashlib.sha256(deck).hexdigest()

    if analysis["deck_identity"] != deck_identity:
        raise AssertionError("published deck bytes do not match their native identity")
    if analysis["model_identity"] != payload["compile_report"]["model"]["identity"]:
        raise AssertionError("adapter analysis is not bound to the compiled model")
    if analysis["request_identity"] != payload["compile_report"]["model"]["request_identity"]:
        raise AssertionError("adapter analysis is not bound to the canonical request")
    if report["provenance"]["deck_identity"] != analysis["deck_identity"]:
        raise AssertionError("solve report lost the generated deck identity")
    if report["provenance"]["mapping_identity"] != analysis["mapping"]["identity"]:
        raise AssertionError("solve report lost the generated mapping identity")
    if solution["provenance"] != report["provenance"]:
        raise AssertionError("solution and solve report disagree on backend provenance")

    branches = analysis["mapping"]["branches"]
    devices = [branch["device"] for branch in branches]
    headers = analysis["mapping"]["output"]["headers"]
    if len(devices) != len(set(devices)) or len(headers) != len(set(headers)):
        raise AssertionError("native adapter emitted ambiguous device or output-vector names")
    deck_text = deck.decode("utf-8")
    if any(
        not any(line.startswith(device + " ") for line in deck_text.splitlines())
        for device in devices
    ):
        raise AssertionError("analysis mapping names a device absent from the published deck")

    if process["version"] != "ngspice-46" or Path(process["executable"]) != executable:
        raise AssertionError(
            "process evidence does not identify the selected ngspice 46 executable"
        )
    if process["exit_code"] != 0 or process["output_filename"] != "volt-dc-output.txt":
        raise AssertionError("process evidence does not describe a successful bounded run")
    if process["output_byte_size"] != len(external_output) or not external_output:
        raise AssertionError("ngspice produced no machine-readable output")
    output_digest = "sha256:" + hashlib.sha256(external_output).hexdigest()
    if process["output_content_digest"] != output_digest:
        raise AssertionError("process evidence does not identify the ingested output bytes")
    return solution


def _write_fixture(work: Path) -> tuple[Path, Path, dict[str, Path]]:
    source = work / "source"
    source.mkdir()
    shutil.copyfile(Path(cases.__file__), source / "main.py")
    (source / "volt.toml").write_bytes(b'[project]\nentrypoint = "main:main"\n')

    project = cases.main()
    result = project.run_through(project.design)
    bundle = work / "corpus.volt"
    result.write(bundle)
    requests = {}
    request_dir = work / "requests"
    request_dir.mkdir()
    for case_id in cases.CASE_IDS:
        request = cases.request_for(result.design(case_id))
        path = request_dir / f"{case_id}.json"
        path.write_bytes(request.to_json().encode("utf-8"))
        requests[case_id] = path
    return source, bundle, requests


def run(executable: Path) -> None:
    executable = executable.expanduser().resolve()
    corpus = _json(CORPUS_PATH)
    corpus_cases = {case["id"]: case for case in corpus["supported_cases"]}
    if tuple(corpus_cases) != cases.CASE_IDS:
        raise AssertionError("Python integration fixture has drifted from the canonical corpus")

    with tempfile.TemporaryDirectory(prefix="volt-ngspice-integration-") as temporary:
        work = Path(temporary).resolve()
        source, bundle, requests = _write_fixture(work)
        native_root, ngspice_root = work / "native", work / "ngspice"
        native_root.mkdir()
        ngspice_root.mkdir()

        startup_sentinel = work / "ngspice-startup-poison-ran"
        startup_poison = f"shell touch {startup_sentinel}\nquit\n".encode("utf-8")
        (work / ".spiceinit").write_bytes(startup_poison)
        (source / ".spiceinit").write_bytes(startup_poison)
        poison_configuration = work / "poison-configuration"
        poison_configuration.mkdir()
        (poison_configuration / "spinit").write_bytes(startup_poison)
        poison_home = work / "poison-home"
        poison_home.mkdir()
        (poison_home / ".spiceinit").write_bytes(startup_poison)
        environment = _cli_environment()
        environment.update(
            {
                "HOME": str(poison_home),
                "NGSPICE_INPUT_DIR": str(poison_configuration),
                "NGSPICE_OSDI_DIR": str(poison_configuration),
                "SPICE_SCRIPTS": str(poison_configuration),
                "SPICE_USERINIT_DIR": str(poison_configuration),
            }
        )

        source_output = ngspice_root / "divider-source"
        source_payload = _run_cli(
            work,
            "--project", str(source),
            "--design", "divider",
            "--request", str(requests["divider"]),
            "--output", str(source_output),
            "--backend", "ngspice",
            "--ngspice", str(executable),
            environment=environment,
        )
        shutil.rmtree(source)
        offline = work / "source-free-working-directory"
        offline.mkdir()
        (offline / ".spiceinit").write_bytes(startup_poison)

        checked = 0
        for case_id in cases.CASE_IDS:
            native_output = native_root / case_id
            native_payload = _run_cli(
                offline,
                "--bundle", str(bundle),
                "--design", case_id,
                "--request", str(requests[case_id]),
                "--output", str(native_output),
                environment=environment,
            )
            ngspice_payload = _run_cli(
                offline,
                "--bundle", str(bundle),
                "--design", case_id,
                "--request", str(requests[case_id]),
                "--output", str(ngspice_root / case_id),
                "--backend", "ngspice",
                "--ngspice", str(executable),
                environment=environment,
            )
            if native_payload["compile_report"]["model"]["identity"] != (
                ngspice_payload["compile_report"]["model"]["identity"]
            ):
                raise AssertionError(
                    f"{case_id}: native and ngspice runs compiled different models"
                )
            expected_shape = corpus["compiled_expectations"][case_id]
            model = native_payload["compile_report"]["model"]
            if (
                not native_payload["compile_report"]["complete"]
                or len(model["branches"]) != expected_shape["branch_count"]
                or len(model["storage"]) != expected_shape["storage_count"]
            ):
                raise AssertionError(f"{case_id}: fixture drifted from the corpus model shape")
            native_solution = _check_native(native_output, native_payload)
            ngspice_solution = _check_ngspice(
                ngspice_root / case_id, ngspice_payload, executable
            )
            checked += _check_observations(
                corpus_cases[case_id], native_solution, ngspice_solution
            )
            print(f"PASS {case_id}")

        source_solution = _check_ngspice(source_output, source_payload, executable)
        bundle_payload = _json(ngspice_root / "divider" / "ngspice-analysis.json")
        source_analysis = _json(source_output / "ngspice-analysis.json")
        if source_analysis != bundle_payload:
            raise AssertionError("source and source-free bundle produced different native mappings")
        if _probe_values(source_solution) != _probe_values(
            _json(ngspice_root / "divider" / "solution.json")
        ):
            raise AssertionError("source and source-free bundle produced different observations")
        if startup_sentinel.exists():
            raise AssertionError("ngspice executed poisoned host startup configuration")

    print(
        f"ngspice 46 integration passed: {len(cases.CASE_IDS)} corpus cases plus "
        f"source/bundle parity, {checked} direct analytical observations"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ngspice",
        required=True,
        type=Path,
        help="explicit path to the ngspice 46 executable",
    )
    args = parser.parse_args()
    run(args.ngspice)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
