"""Explicitly prepare, compile, and solve the logical-only divider."""

from pathlib import Path
import sys

import volt

from main import main


def run(destination: Path) -> int:
    if destination.exists():
        raise FileExistsError(f"output already exists: {destination}")
    destination.mkdir(parents=True)

    project = main()
    result = project.run_through(project.design)
    design = result.design("divider")
    design_nets = {net.name: net for net in design.nets()}
    dc_input = volt.prepare_electrical_input(design)
    supply = dc_input.net(design_nets["SUPPLY"])
    midpoint = dc_input.net(design_nets["MIDPOINT"])
    ground = dc_input.net(design_nets["GROUND"])
    request = volt.DcRequest(
        "divider-operating-point",
        dc_input,
        reference=ground,
        sources=(
            volt.DcVoltageSource(
                "supply-5v",
                supply,
                ground,
                volt.Quantity(volt.UnitDimension.VOLTAGE, 5),
            ),
        ),
        probes=(
            volt.DcVoltageProbe("midpoint-voltage", midpoint, ground),
            volt.DcSourceCurrentProbe("supply-current", "supply-5v"),
        ),
    )

    (destination / "canonicalrequest.json").write_bytes(
        request.to_json().encode("utf-8")
    )
    result.write(destination / "divider.volt")

    compiled = volt.compile_electrical(request)
    (destination / "compile-report.json").write_bytes(
        compiled.to_json().encode("utf-8")
    )
    if not compiled.complete:
        return 1

    solved = volt.solve_dc(compiled.model)
    (destination / "solve-report.json").write_bytes(solved.to_json().encode("utf-8"))
    if not solved.success:
        return 1
    (destination / "solution.json").write_bytes(
        solved.solution.to_json().encode("utf-8")
    )
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} OUTPUT_DIRECTORY")
    raise SystemExit(run(Path(sys.argv[1])))
