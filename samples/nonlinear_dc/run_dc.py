"""Save, reopen and explicitly solve accepted and failed native DC flows."""
from pathlib import Path
import json
import sys

import volt
from main import main, request_for


def run(destination):
    destination = Path(destination)
    if destination.exists():
        raise FileExistsError(destination)
    destination.mkdir(parents=True)
    project = main()
    result = project.run_through(project.design)
    result.write(destination / "diode.volt")
    # The reopened input has no source/library dependency.
    graph = volt.ProjectBundle.open(destination / "diode.volt").graph
    input = graph.loaded_project.circuits[0].electrical_input()
    summaries = {}
    for name, supply in (("accepted", 5), ("domain-limited", -1)):
        request = request_for(input, supply)
        directory = destination / name
        directory.mkdir()
        (directory / "request.json").write_text(request.to_json())
        compiled = volt.compile_electrical(request)
        assert compiled.complete
        (directory / "compile-report.json").write_text(compiled.to_json())
        report = volt.solve_dc(compiled.model, volt.NonlinearDcSolveOptions())
        (directory / "solve-report.json").write_text(report.to_json())
        assert report.success == (name == "accepted")
        if report.success:
            (directory / "solution.json").write_text(report.solution.to_json())
        summaries[name] = {"input": str(input.identity.logical),
                           "compiled": str(compiled.model.identity),
                           "analysis": str(report.analysis_identity),
                           "outcome": str(report.outcome)}
    (destination / "identities.json").write_text(json.dumps(summaries, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} OUTPUT_DIRECTORY")
    raise SystemExit(run(sys.argv[1]))
