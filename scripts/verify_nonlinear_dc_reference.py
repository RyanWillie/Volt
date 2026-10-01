#!/usr/bin/env python3
"""Check archived independent diode decks; regenerate only with explicit ngspice 46."""
from __future__ import annotations

import argparse
from decimal import Decimal, localcontext
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / "tests" / "fixtures" / "nonlinear_dc"
CASES = {
    "forward_1v": (0.517173532980596803, 0.000482826467019403197),
    "forward_5v": (0.574476925589298085, 0.004425523074410701915),
    "picoamp_forward": (0.0999999532376448791, 4.67623551208893189e-11),
    "mild_reverse": (-0.0499999991446960765, -8.55303923528116476e-13),
}
PRINTED = re.compile(r"^\s*(v\(junction\)|@djunction\[(?:id|gd)\])\s*=\s*([^\s]+)\s*$", re.M)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def observations(stdout: str) -> dict[str, float]:
    result = {name: float(value) for name, value in PRINTED.findall(stdout)}
    if set(result) != {"v(junction)", "@djunction[id]", "@djunction[gd]"}:
        raise ValueError("independent oracle observations missing")
    if not all(math.isfinite(value) for value in result.values()):
        raise ValueError("nonfinite oracle observation")
    return result


def exact_si_roots() -> None:
    # Independent 70-digit monotone bisection; no native model or solver is imported.
    sources = ("1", "5", "0.1", "-0.05")
    with localcontext() as context:
        context.prec = 70
        emission = Decimal("1.380649e-23") * Decimal("300.15") / Decimal("1.602176634e-19")
        saturation = Decimal("1e-12")
        for (name, (expected_voltage, expected_current)), source in zip(CASES.items(), sources):
            drive = Decimal(source)
            low, high = Decimal("-0.05"), Decimal("0.8")
            for _ in range(240):
                middle = (low + high) / 2
                residual = (drive - middle) / 1000 - saturation * ((middle / emission).exp() - 1)
                if residual > 0:
                    low = middle
                else:
                    high = middle
            voltage = (low + high) / 2
            current = (drive - voltage) / 1000
            if abs(float(voltage) - expected_voltage) > 1e-16:
                raise ValueError(f"{name}: canonical voltage differs from high-precision root")
            if abs(float(current) - expected_current) > max(1e-24, 1e-15 * abs(expected_current)):
                raise ValueError(f"{name}: canonical current differs from high-precision root")


def validate(evidence: dict) -> None:
    exact_si_roots()
    if evidence["format"] != "volt.nonlinear-dc-independent-reference" or evidence["version"] != 1:
        raise ValueError("unknown independent evidence contract")
    if evidence["backend"]["version"] != "ngspice-46" or set(evidence["cases"]) != set(CASES):
        raise ValueError("oracle version or case set differs")
    for name, (voltage, current) in CASES.items():
        case = evidence["cases"][name]
        for extension in ("cir", "stdout", "stderr"):
            raw = (DIRECTORY / f"{name}.{extension}").read_bytes()
            if len(raw) > 65536 or digest(raw) != case["sha256"][extension]:
                raise ValueError(f"{name}: archived {extension} hash/size mismatch")
        if case["returncode"] != 0:
            raise ValueError(f"{name}: oracle execution failed")
        actual = observations((DIRECTORY / f"{name}.stdout").read_text())
        if actual != case["observations"]:
            raise ValueError(f"{name}: raw and normalized observations differ")
        for key, target, floor in (("v(junction)", voltage, 1e-6), ("@djunction[id]", current, 1e-9)):
            if abs(actual[key] - target) > floor + 1e-6 * abs(target):
                raise ValueError(f"{name}: independent oracle differs from exact-SI scalar root")


def regenerate(executable: Path, date: str) -> dict:
    executable = executable.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="volt-diode-oracle-") as scratch:
        environment = {"PATH": str(executable.parent), "LC_ALL": "C", "TMPDIR": scratch}
        version = subprocess.run([str(executable), "--version"], cwd=scratch, env=environment,
                                 capture_output=True, timeout=30, check=True)
        version_bytes = version.stdout + version.stderr
        if not re.search(rb"ngspice-46\b", version_bytes):
            raise ValueError("explicit independent reference requires ngspice 46")
        (DIRECTORY / "ngspice-version.txt").write_bytes(version_bytes)
        cases = {}
        for name in CASES:
            completed = subprocess.run([str(executable), "-n", "-b", str(DIRECTORY / f"{name}.cir")],
                                       cwd=scratch, env=environment, capture_output=True, timeout=30,
                                       check=True)
            for extension, raw in (("stdout", completed.stdout), ("stderr", completed.stderr)):
                if len(raw) > 65536:
                    raise ValueError("bounded independent oracle output exceeded 64 KiB")
                (DIRECTORY / f"{name}.{extension}").write_bytes(raw)
            cases[name] = {
                "command": ["ngspice", "-n", "-b", f"{name}.cir"],
                "returncode": completed.returncode,
                "sha256": {ext: digest((DIRECTORY / f"{name}.{ext}").read_bytes())
                           for ext in ("cir", "stdout", "stderr")},
                "observations": observations(completed.stdout.decode()),
            }
    return {
        "format": "volt.nonlinear-dc-independent-reference", "version": 1,
        "executed_on": date,
        "backend": {"version": "ngspice-46", "executable": str(executable),
                    "executable_sha256": digest(executable.read_bytes()),
                    "version_sha256": digest(version_bytes)},
        "process": {"timeout_seconds": 30, "max_output_bytes_per_stream": 65536,
                    "environment_keys": ["PATH", "LC_ALL", "TMPDIR"], "isolated_cwd": True,
                    "init_files_disabled": True},
        "comparison": {"voltage_absolute": 1e-6, "current_absolute": 1e-9, "relative": 1e-6,
                       "meaning": "external model comparison; no manufacturer calibration"},
        "cases": cases,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ngspice", type=Path)
    parser.add_argument("--executed-on")
    args = parser.parse_args()
    target = DIRECTORY / "ngspice-46-results.json"
    if args.ngspice is not None:
        if args.executed_on is None or not re.fullmatch(r"\d{4}-\d{2}-\d{2}", args.executed_on):
            parser.error("--ngspice requires --executed-on YYYY-MM-DD")
        evidence = regenerate(args.ngspice, args.executed_on)
        validate(evidence)
        target.write_text(json.dumps(evidence, indent=2) + "\n")
    else:
        if args.executed_on is not None:
            parser.error("--executed-on requires --ngspice")
        evidence = json.loads(target.read_text())
        validate(evidence)
    version_bytes = (DIRECTORY / "ngspice-version.txt").read_bytes()
    if digest(version_bytes) != evidence["backend"]["version_sha256"]:
        raise ValueError("archived version identity mismatch")
    print("independent diode decks, raw evidence and exact-SI scalar roots verified")


if __name__ == "__main__":
    main()
