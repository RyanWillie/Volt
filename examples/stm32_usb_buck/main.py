"""Generate the Volt-native STM32 USB buck logical benchmark artifacts."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import volt

from .stm32_board import build_design


@dataclass(frozen=True)
class BenchmarkArtifacts:
    logical_json: Path
    validation_report: Path


def validation_report_json(report: volt.DiagnosticReport) -> str:
    counts = {"errors": 0, "warnings": 0, "infos": 0}
    diagnostics = []
    for diagnostic in report:
        if diagnostic.severity == "error":
            counts["errors"] += 1
        elif diagnostic.severity == "warning":
            counts["warnings"] += 1
        else:
            counts["infos"] += 1
        diagnostics.append(
            {
                "severity": diagnostic.severity,
                "code": diagnostic.code,
                "message": diagnostic.message,
                "entities": [
                    {"kind": entity.kind, "index": entity.index}
                    for entity in diagnostic.entities
                ],
            }
        )
    return json.dumps(
        {
            "summary": counts,
            "diagnostics": diagnostics,
        },
        indent=2,
        sort_keys=True,
    ) + "\n"


def write_artifacts(output_dir: Path | str | None = None) -> BenchmarkArtifacts:
    if output_dir is None:
        output_dir = Path(__file__).resolve().parent / "artifacts"
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    design = build_design()
    logical_json = output_path / "stm32_usb_buck.volt.json"
    validation_report = output_path / "stm32_usb_buck.validation.json"

    design.write(logical_json)
    validation_report.write_text(validation_report_json(design.validate()), encoding="utf-8")
    return BenchmarkArtifacts(logical_json=logical_json, validation_report=validation_report)


if __name__ == "__main__":
    write_artifacts()
