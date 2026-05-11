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
  "symbol_instances": []
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

References inside the schematic document must point to existing IDs of the correct type.
`component:<number>` references point into the associated logical circuit document.

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
symbol pins, symbol text, and component reference labels from the logical circuit.

SVG is an output artifact. It should not be treated as the canonical schematic source,
because the structured `volt.schematic` model is the data that can be validated,
round-tripped, edited, and adapted to future renderers or EDA import/export tools.

## Sheets And Instances

Sheets own page-level placement lists:

```json
{
  "id": "sheet:0",
  "name": "Main",
  "symbol_instances": ["symbol_instance:0"]
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

The sheet's `symbol_instances` list must match the instances whose `sheet` field points
to that sheet. This keeps the stored document deterministic and prevents orphaned
presentation objects.

## Deterministic Writer Rules

A canonical writer must:

- emit top-level fields in the order shown in this document
- emit arrays in kernel table/insertion order
- generate local IDs deterministically from table order
- preserve sheet symbol-instance order
- use the enum and primitive spellings shown here
- omit timestamps and process-local data
