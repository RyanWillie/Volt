#!/usr/bin/env python3
"""Test-only independent ngspice 46 oracle; never a public Volt AC backend.

Decks are authored independently of Volt's compiler. wrdata columns are frequency
then adjacent real/imaginary pairs in printed deck order. Vdrive current is from
input to ground; the test current source also points input to ground, hence
Z=-V(input) for a one ampere test stimulus. All source amplitudes are peak values.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1] / "tests/fixtures/linear_ac"


def expected(case, frequency):
    omega = 2 * math.pi * frequency
    if case == "rc":
        gain = 1 / complex(1, omega * 1000 * 1e-6)
        return [1 + 0j, gain, -(1 - gain) / 1000]
    if case == "rl":
        gain = complex(0, omega * 0.1) / complex(100, omega * 0.1)
        return [1 + 0j, gain, -(1 - gain) / 100]
    impedance = complex(10, omega * 0.01 - 1 / (omega * 1e-5))
    if case == "rlc":
        current = 1 / impedance
        return [1 + 0j, 1 - 10 * current, -current]
    return [-impedance, -impedance + 10]


def collect(executable):
    version = subprocess.run(
        [executable, "--version"], check=True, capture_output=True, text=True
    ).stdout
    if "ngspice-46 :" not in version:
        raise RuntimeError("The independent reference is pinned to ngspice 46")
    cases = []
    for case in ["rc", "rl", "rlc", "impedance"]:
        deck = ROOT / (case + ".cir")
        with tempfile.TemporaryDirectory(prefix="volt-ac-oracle-") as directory:
            run = subprocess.run(
                [executable, "-b", str(deck)],
                cwd=directory,
                capture_output=True,
                text=True,
                check=True,
            )
            raw = (Path(directory) / "observations.txt").read_text()
        names = (
            ["input_voltage_v", "output_voltage_v", "source_current_a"]
            if case in ["rc", "rl"]
            else (
                ["input_voltage_v", "after_resistor_voltage_v", "source_current_a"]
                if case == "rlc"
                else ["input_voltage_v", "after_resistor_voltage_v"]
            )
        )
        rows = []
        for line in raw.splitlines()[1:]:
            columns = [float(x) for x in line.split()]
            if not columns:
                continue
            frequency = columns[0]
            values = [
                complex(columns[i], columns[i + 1]) for i in range(1, len(columns), 2)
            ]
            analytical = expected(case, frequency)
            for name, value, target in zip(names, values, analytical, strict=True):
                absolute = 1e-11 if name.endswith("_a") else 1e-9
                if abs(value - target) > absolute + 1e-9 * abs(target):
                    raise RuntimeError(
                        f"Independent oracle disagrees with analysis: {case}/{name}"
                    )
            rows.append(
                {
                    "frequency_hz": frequency,
                    "observations": {
                        name: {"real": v.real, "imaginary": v.imag}
                        for name, v in zip(names, values, strict=True)
                    },
                    "analytical": {
                        name: {"real": v.real, "imaginary": v.imag}
                        for name, v in zip(names, analytical, strict=True)
                    },
                }
            )
        cases.append(
            {
                "id": case,
                "deck": deck.name,
                "deck_sha256": hashlib.sha256(deck.read_bytes()).hexdigest(),
                "columns": names,
                "raw_wrdata": raw,
                "raw_wrdata_sha256": hashlib.sha256(raw.encode()).hexdigest(),
                "points": rows,
            }
        )
    return {
        "format": "volt.linear-ac-reference-corpus",
        "version": 1,
        "backend": "ngspice",
        "backend_version": "46",
        "settings": {
            "mode": "batch",
            "analysis": "explicit inclusive linear sweeps",
            "temperature_c": 27,
            "nominal_temperature_c": 27,
            "direct_solver": "SPARSE 1.3",
            "source_amplitude": "peak",
            "time_convention": "Re(phasor*exp(j*omega*t))",
            "wrdata": "numdgt=17, wr_vecnames, wr_singlescale; adjacent real/imaginary pairs",
            "branch_polarity": "current positive from authored first node to second node",
            "impedance": "V(input)/(-Itest)",
        },
        "version_banner": version,
        "native_comparison": {
            "relative_tolerance": 1e-7,
            "voltage_absolute_tolerance_v": 1e-9,
            "current_absolute_tolerance_a": 1e-11,
            "impedance_absolute_tolerance_ohm": 1e-6,
            "wrapped_phase_tolerance_rad": 1e-5,
        },
        "oracle_analytical_comparison": {
            "relative_tolerance": 1e-9,
            "voltage_absolute_tolerance_v": 1e-9,
            "current_absolute_tolerance_a": 1e-11,
        },
        "cases": cases,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ngspice", default=shutil.which("ngspice"))
    parser.add_argument(
        "--write",
        action="store_true",
        help="Regenerate the explicitly version-pinned recorded corpus",
    )
    args = parser.parse_args()
    if not args.ngspice:
        parser.error("ngspice 46 is required")
    result = collect(args.ngspice)
    destination = ROOT / "ngspice-46-results.json"
    if args.write:
        destination.write_text(json.dumps(result, indent=2) + "\n")
    else:
        recorded = json.loads(destination.read_text())
        for actual, saved in zip(result["cases"], recorded["cases"], strict=True):
            if actual["deck_sha256"] != saved["deck_sha256"]:
                raise RuntimeError("Reference deck bytes have changed")
            for current, previous in zip(
                actual["points"], saved["points"], strict=True
            ):
                for name, value in current["observations"].items():
                    expected_value = previous["observations"][name]
                    a = complex(value["real"], value["imaginary"])
                    b = complex(expected_value["real"], expected_value["imaginary"])
                    if abs(a - b) > 1e-11 + 1e-9 * abs(b):
                        raise RuntimeError("Pinned reference observations changed")
    print(
        "ngspice 46 AC reference: four independent decks passed analytical and recorded comparisons"
    )


if __name__ == "__main__":
    main()
