# 5 V LED indicator

This is one complete, self-contained Volt reference project:

`5 V input (J1) -> 330 ohm series resistor (R1) -> red LED (D1) -> ground return`

Project layout:

- `volt.toml` — discovery configuration.
- `main.py` — exact parts, Circuit, schematic, Board, and project tests.
- `README.md` — workflow and artifact guide.

`main.py` owns the three exact part declarations, the example-local `volt.Library`,
canonical Circuit connectivity, authored schematic, one two-layer `Indicator` Board, and
project tests. `volt.toml` is the discovery entrypoint. The project does not publish a
standalone part-library artifact or depend on a reusable parts catalogue.

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

The immutable `indicator.volt` ProjectBundle contains the logical model, `Main`
schematic, authored `Indicator` Board, resolved immutable `CompiledBoard`, `BoardScene`,
diagnostics, project tests, exact selected-part closure, and four selected outputs.
The offline `inspect` and `export` commands read only that verified bundle; they do not
import or execute `main.py`.

The selected outputs are the schematic SVG, Board SVG, BOM, and CPL—the smallest useful
render/manufacturing set supported by ProjectBundle publication. Open both SVGs as
rendered images before treating the layout as reviewed.

Electrical limitation: Volt records the 5 V net, exact-part Voltage/Current data, and the
human-readable `330 ohm` value. It does not currently derive LED current or resistor
dissipation, and this project makes neither claim.
