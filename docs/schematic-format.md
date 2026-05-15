# Schematic Projection File Format

Volt schematic files persist the kernel-owned `Schematic` projection model for
save/load, rendering, and future import/export adapters. They store presentation state,
not logical connectivity.

The v1 format is deterministic JSON:

```json
{
  "format": "volt.schematic",
  "version": 1,
  "symbol_definitions": [],
  "sheets": [],
  "symbol_instances": [],
  "wire_runs": [],
  "net_labels": []
}
```

Schematic files are read against an already-loaded logical `Circuit`. Component
references use logical circuit local IDs such as `component:0`; the reader validates those
references through the bound circuit. A schematic file must not create, merge, split, or
otherwise mutate nets.

## Local IDs

Schematic-local IDs are document structure, not domain meaning:

- `symbol_def:<number>`
- `sheet:<number>`
- `symbol_instance:<number>`
- `wire_run:<number>`
- `net_label:<number>`

References inside the schematic document must point to existing IDs of the correct type.
`component:<number>` and `net:<number>` references point into the associated logical
circuit document.

## Symbol Definitions

`SymbolDefinition` rows own structured vector geometry and symbol pin presentation:

```json
{
  "id": "symbol_def:0",
  "name": "Resistor",
  "pins": [
    { "name": "1", "number": "1", "anchor": { "x": 0, "y": 0 }, "orientation": "Left" }
  ],
  "primitives": [
    { "type": "line", "start": { "x": 0, "y": 0 }, "end": { "x": 20, "y": 0 } }
  ]
}
```

Supported primitive types are `line`, `rectangle`, `circle`, `arc`, and `text`.
Coordinates and numeric geometry values must be finite.

Valid orientations are `Right`, `Down`, `Left`, and `Up`.

## SVG Rendering

Volt can render a `Schematic` projection to deterministic SVG. The SVG writer consumes
the loaded kernel model and the associated logical circuit; it does not own or mutate
connectivity. SVG output includes basic sheet geometry, placed symbol primitives, visible
symbol pins, symbol text, wire runs, net labels, and component reference labels from the
logical circuit.

SVG is an output artifact. It should not be treated as the canonical schematic source,
because the structured `volt.schematic` model is the data that can be validated,
round-tripped, edited, and adapted to future renderers or EDA import/export tools.

## Sheets And Instances

Sheets own page-level placement lists:

```json
{
  "id": "sheet:0",
  "name": "Main",
  "symbol_instances": ["symbol_instance:0"],
  "wire_runs": ["wire_run:0"],
  "net_labels": ["net_label:0"]
}
```

Symbol instances present existing logical components:

```json
{
  "id": "symbol_instance:0",
  "sheet": "sheet:0",
  "symbol_definition": "symbol_def:0",
  "component": "component:0",
  "position": { "x": 40, "y": 20 },
  "orientation": "Right"
}
```

Wire runs and net labels present existing logical nets:

```json
{
  "id": "wire_run:0",
  "sheet": "sheet:0",
  "net": "net:0",
  "points": [
    { "x": 20, "y": 20 },
    { "x": 40, "y": 20 }
  ]
}
```

```json
{
  "id": "net_label:0",
  "sheet": "sheet:0",
  "net": "net:0",
  "position": { "x": 20, "y": 16 },
  "orientation": "Right"
}
```

Net labels do not store independent label text. The visible label is derived from the
referenced logical net's canonical name, so schematic labels cannot create hidden net
semantics that contradict the circuit.

## Geometry Semantics

Wire geometry governs presentation validity over existing `NetId` values. It must not
create, merge, split, or rename logical nets.

Segment relationships have these meanings:

- A visual crossing is not a connection by itself. Crossing wire runs may represent
  different logical nets because no junction is implied.
- An endpoint touch, including a T-junction where one segment endpoint lies on another
  segment, is a visual join for wire runs on the same logical net.
- An overlap is a visual join for wire runs on the same logical net.
- Endpoint touches and overlaps between different `NetId` values are collisions. The
  kernel rejects them because they visually imply connectivity that the logical circuit
  does not own.
- A crossing becomes a connected junction only when an explicit schematic junction object
  is present at that point. The v1 file format does not yet persist junction objects, so
  visual crossings in v1 are non-connections.

Same-net wire runs may touch or overlap to present one logical net. Same-net crossings are
allowed as geometry over the same `NetId`, but a renderer should not draw a junction dot
for a crossing unless explicit junction data exists.

The sheet's `symbol_instances`, `wire_runs`, and `net_labels` lists must match the
objects whose `sheet` field points to that sheet. This keeps the stored document
deterministic and prevents orphaned presentation objects.

## Deterministic Writer Rules

A canonical writer must:

- emit top-level fields in the order shown in this document
- emit arrays in kernel table/insertion order
- generate local IDs deterministically from table order
- preserve sheet symbol-instance, wire-run, and net-label order
- use the enum and primitive spellings shown here
- omit timestamps and process-local data
