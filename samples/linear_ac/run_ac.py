"""Prepare a canonical request/bundle, then explicitly execute the native CLI workflow."""
from pathlib import Path
import math
import sys
import volt
from volt.cli import main as cli_main
from main import main


def run(destination):
    destination = Path(destination).resolve()
    if destination.exists():
        raise FileExistsError(f"output already exists: {destination}")
    destination.mkdir(parents=True)
    project = main()
    result = project.run_through(project.design)
    design = result.design("lowpass")
    nets = {net.name: net for net in design.nets()}
    input = volt.prepare_ac_input(design)
    supply, output, ground = (input.net(nets[name]) for name in ("INPUT", "OUTPUT", "GROUND"))
    cutoff = 1 / (2 * math.pi * 1000 * 1e-6)
    request = volt.AcRequest(
        "rc-response", input,
        volt.AcFrequencySweep([volt.hertz(cutoff / 10), volt.hertz(cutoff), volt.hertz(cutoff * 10)]),
        reference=ground,
        sources=[volt.AcVoltageSource("drive", supply, ground, volt.Quantity(volt.UnitDimension.VOLTAGE, 1))],
        probes=[volt.AcVoltageProbe("out", output, ground), volt.AcVoltageProbe("in", supply, ground)],
        gains=[volt.AcGainProbe("gain", "out", "in")],
    )
    request_path = destination / "request.json"
    request_path.write_text(request.to_json(), encoding="utf-8")
    result.write(destination / "lowpass.volt")
    return cli_main(["simulate", "--bundle", str(destination / "lowpass.volt"),
                     "--design", "lowpass", "--analysis", "ac", "--backend", "native",
                     "--request", str(request_path), "--output", str(destination / "results")])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} OUTPUT_DIRECTORY")
    raise SystemExit(run(sys.argv[1]))
