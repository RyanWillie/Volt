"""Validate source-free nonlinear behavior using an installed wheel and native fixture."""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile

import volt


def main() -> None:
    producer = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="volt-wheel-diode-") as scratch:
        directory = Path(scratch)
        native = directory / "native"
        subprocess.run([str(producer), str(native)], check=True, capture_output=True)
        graph = volt.ProjectBundle.open(native / "project.volt").graph
        electrical_input = graph.loaded_project.circuits[0].electrical_input()
        request = volt.DcRequest.from_json(electrical_input, (native / "request.json").read_bytes())
        model = volt.compile_electrical(request).model
        report = volt.solve_dc(model, volt.NonlinearDcSolveOptions())
        expected = json.loads((native / "report.json").read_text())
        actual = json.loads(report.to_json())
        assert report.success and report.solution is not None
        assert actual["analysis_identity"] == expected["analysis_identity"]
        assert actual["model"] == expected["model"]
        reference = json.loads((native / "solution.json").read_text())
        observed = json.loads(report.solution.to_json())
        for sequence in ("nodes", "branches", "probes"):
            for target, value in zip(reference["observations"][sequence], observed["observations"][sequence], strict=True):
                assert target.keys() == value.keys()
                for key in target:
                    if isinstance(target[key], dict) and "si" in target[key]:
                        assert target[key]["dimension"] == value[key]["dimension"]
                        floor = 1e-9 if target[key]["dimension"] == "voltage" else 1e-12
                        assert abs(target[key]["si"] - value[key]["si"]) <= floor + 1e-9 * abs(target[key]["si"])
                    else:
                        assert target[key] == value[key]
        linear = volt.solve_dc(model)
        assert not linear.success and linear.solution is None
        unrelated = directory / "unrelated"
        unrelated.mkdir()
        for name, request_name, flags, exit_code, outcome in (
            ("success", "request.json", [], 0, "converged"),
            ("domain", "failed-request.json", [], 1, "domain_limited"),
            ("budget", "request.json", ["--max-iterations", "1"], 1, "iteration_limit")):
            output = directory / name
            completed = subprocess.run(
                [sys.executable, "-m", "volt.cli", "simulate", "--bundle", str(native / "project.volt"),
                 "--design", "main", "--request", str(native / request_name),
                 "--method", "diode-newton", "--output", str(output), "--json", *flags],
                cwd=unrelated, capture_output=True, text=True)
            assert completed.returncode == exit_code, completed.stdout + completed.stderr
            result = json.loads((output / "solve-report.json").read_text())
            assert result["outcome"] == outcome
            assert (output / "solution.json").exists() == (exit_code == 0)
        assert {d["code"] for d in linear.diagnostics} <= set(volt.ANALYSIS_DIAGNOSTIC_CODES)
    print("installed wheel source-free identities, observations and success/failure CLI behavior passed")


if __name__ == "__main__":
    main()
