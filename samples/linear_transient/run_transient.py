"""Author a request/bundle, then explicitly run native transient simulation."""
from pathlib import Path
import sys

import volt
from volt.cli import main as cli_main
from main import main


def startup_request(input):
    """1 V post-start source, explicit zero capacitor voltage, tau = 1 ms."""
    supply, output, ground = input.nets
    volts = lambda value: volt.Quantity(volt.UnitDimension.VOLTAGE, value)
    return volt.TransientRequest(
        "rc-startup", input,
        volt.TransientTimeGrid([volt.seconds(t) for t in (0, 0.001, 0.003)]),
        reference=ground,
        sources=[volt.TransientVoltageSource("drive", supply, ground,
                                            volt.TransientWaveform(volts(1)))],
        probes=[volt.DcVoltageProbe("out", output, ground)],
        initial_state=[volt.TransientInitialState(
            input, input.occurrences[1], "body",
            volt.TransientStorageKind.CAPACITOR_VOLTAGE, volts(0))],
    )


def run(destination):
    destination = Path(destination).resolve()
    if destination.exists():
        raise FileExistsError(f"output already exists: {destination}")
    destination.mkdir(parents=True)
    project = main()
    result = project.run_through(project.design)
    design = result.design("startup")
    request = startup_request(volt.prepare_electrical_input(design))
    request_path = destination / "request.json"
    request_path.write_text(request.to_json(), encoding="utf-8")
    bundle = destination / "startup.volt"
    result.write(bundle)
    return cli_main([
        "simulate", "--bundle", str(bundle), "--design", "startup",
        "--analysis", "transient", "--backend", "native",
        "--request", str(request_path), "--output", str(destination / "results"),
        "--h-min", "1e-12", "--h-initial", "5e-5", "--h-max", "1e-4",
        "--max-trials", "100000", "--max-accepted-steps", "100000",
        "--relative-tolerance", "1e-7", "--absolute-voltage-tolerance", "1e-9",
        "--absolute-current-tolerance", "1e-12", "--json",
    ])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} OUTPUT_DIRECTORY")
    raise SystemExit(run(sys.argv[1]))
