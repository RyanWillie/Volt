# 5 V LED indicator

This is one complete, self-contained Volt reference project:

`5 V input (J1) -> 330 ohm series resistor (R1) -> red LED (D1) -> ground return`

Project layout:

- `volt.toml` — discovery configuration.
- `parts.py` — example-local exact parts, symbols, footprints, and electrical records.
- `main.py` — Circuit connectivity, schematic, and Board authoring.
- `project_tests.py` — product-intent checks for the three project stages.
- `README.md` — workflow and artifact guide.

`parts.py` owns the three exact part declarations and the example-local `volt.Library`.
`main.py` owns canonical Circuit connectivity, the authored schematic, and one two-layer
`Indicator` Board whose current routes all use `F.Cu`. `project_tests.py` owns the
product-intent checks registered by the project. `volt.toml` is the discovery entrypoint.
The project does not publish a standalone part-library artifact or depend on a reusable
parts catalogue.

From this directory, with Volt installed:

```sh
volt check --json
volt build \
  --output build/indicator.volt \
  --export schematic-svg \
  --export board-svg \
  --export bom \
  --export cpl \
  --json
volt inspect --bundle build/indicator.volt --json
volt export \
  --bundle build/indicator.volt \
  --output build/offline-exports \
  --json
```

On macOS, open the rendered outputs directly:

```sh
open build/indicator.volt/artifacts/schematic_svg/*.svg
open build/indicator.volt/artifacts/board_svg/*.svg
```

On other platforms, open those two SVG files in a browser or image viewer.

The immutable `indicator.volt` ProjectBundle contains the logical model, `Main`
schematic, authored `Indicator` Board, resolved immutable `CompiledBoard`, `BoardScene`,
diagnostics, project tests, exact selected-part closure, and four selected outputs.
The offline `inspect` and `export` commands read only that verified bundle; they do not
import or execute `main.py`, `parts.py`, or `project_tests.py`.

The selected outputs are the schematic SVG, Board SVG, BOM, and CPL—the smallest useful
render/manufacturing set supported by ProjectBundle publication. Open both SVGs as
rendered images before treating the layout as reviewed.

Electrical limitation: Volt records the 5 V net, exact-part Voltage/Current data, and the
human-readable `330 ohm` value. It does not currently derive LED current or resistor
dissipation, and this project makes neither claim.
