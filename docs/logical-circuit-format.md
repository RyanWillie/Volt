# Logical Circuit File Format

Volt logical circuit files persist the canonical `Circuit` model for save/load,
interchange, validation, golden fixtures, and generated output from authoring tools. They
store logical circuit state, not authoring-builder state, schematic placement, or PCB
geometry.

The v1 format is deterministic JSON.

```json
{
  "format": "volt.logical_circuit",
  "version": 1,
  "pin_definitions": [],
  "component_definitions": [],
  "components": [],
  "pins": [],
  "nets": []
}
```

## Identity Model

The format separates document structure, provenance, and domain meaning:

```text
Local IDs are for document structure.
Source metadata is for provenance.
Domain fields are for electrical and physical meaning.
```

Every persisted entity has a required document-local ID using a typed prefix:

- `pin_def:<number>`
- `component_def:<number>`
- `component:<number>`
- `pin:<number>`
- `net:<number>`

Local IDs are non-semantic. A component ID such as `component:0` is independent of the
component's reference designator, so it can survive a rename from `R1` to `R101`.
References within the document use these local IDs and must point to existing entities of
the correct type.

Local IDs are scoped to one logical circuit file. They are not library identities, catalog
identities, or globally unique UUIDs. Writers should preserve loaded local IDs when
possible. Writers serializing a fresh in-memory `Circuit` may generate IDs
deterministically from table order.

## Library Provenance

Reusable definitions may include optional `source` metadata:

```json
{
  "id": "component_def:0",
  "name": "Resistor",
  "source": {
    "namespace": "volt.passives",
    "name": "resistor_2pin",
    "version": "1.0.0"
  },
  "pins": ["pin_def:0", "pin_def:1"],
  "properties": {}
}
```

`source` means the local definition was derived from or imported from that library item.
It does not guarantee the local definition remains identical to the library item forever.
The local row is the snapshot used by this circuit. Future formats may add source hashes,
revisions, or explicit modified-from-source flags.

A component instance traces to library provenance through its definition:

```text
R1 -> component:0 -> component_def:0 -> source volt.passives/resistor_2pin@1.0.0
```

Physical selected-part identity remains separate and uses typed domain fields such as
manufacturer name, manufacturer part number, package, footprint, and pin/pad mappings.

## Deterministic Writer Rules

A canonical writer must produce stable output for the same circuit and same writer
version:

- emit top-level fields in the order shown in this document
- emit entity arrays in kernel table/insertion order
- preserve loaded local IDs when possible
- generate missing local IDs deterministically from table order
- emit properties in lexicographic key order
- emit electrical attributes in lexicographic key order
- omit empty `electrical_attributes` objects
- preserve net pin order
- preserve selected-part pin/pad mapping order
- use the enum spellings shown in this document
- omit timestamps and other process-local data from canonical output

## Property Values

`PropertyValue` carries a kind, so properties are encoded with explicit type tags:

```json
{
  "value": { "type": "string", "value": "330 ohm" },
  "fitted": { "type": "boolean", "value": true },
  "count": { "type": "integer", "value": 3 },
  "gain": { "type": "number", "value": 1.5 }
}
```

Valid property types are `string`, `boolean`, `integer`, and `number`.

## Typed Electrical Attributes

Components, selected physical parts, and nets may carry optional
`electrical_attributes`. The field is absent when no typed electrical attributes are
present. Attribute names are stable strings chosen by the kernel/catalog contract for
that owner, and values are dimensioned payloads:

```json
{
  "resistance": { "type": "quantity", "dimension": "resistance", "value": 330 },
  "tolerance": { "type": "tolerance", "mode": "percent", "dimension": "ratio", "minus": 0.01, "plus": 0.01 },
  "voltage_rating": { "type": "quantity", "dimension": "voltage", "value": 75 },
  "input_voltage_range": { "type": "range", "dimension": "voltage", "minimum": 3, "maximum": 5.5 }
}
```

Valid dimensions are:

- `resistance`
- `capacitance`
- `inductance`
- `voltage`
- `current`
- `power`
- `frequency`
- `time`
- `temperature`
- `ratio`

Valid value encodings are:

- `quantity`: requires `dimension` and finite JSON-number `value`
- `tolerance`: requires `mode`, `dimension`, finite non-negative `minus`, and finite
  non-negative `plus`; valid modes are `absolute` and `percent`; percent tolerances use
  `ratio`
- `range`: requires `dimension` and at least one finite JSON-number bound, `minimum` or
  `maximum`; when both are present, `minimum` must not exceed `maximum`

The logical file persists design-bearing typed values, not Python authoring syntax,
default authoring units, UI labels, or catalog spec metadata. A missing
`electrical_attributes` field loads as an empty typed attribute map.

## Pin Definitions

Pin definitions describe reusable logical pins:

```json
{
  "id": "pin_def:0",
  "name": "A",
  "number": "1",
  "role": "Passive",
  "connection_requirement": "Required"
}
```

`role` values are the public `PinRole` spellings:

- `Passive`
- `PowerInput`
- `PowerOutput`
- `Ground`
- `DigitalInput`
- `DigitalOutput`
- `Bidirectional`
- `AnalogInput`
- `AnalogOutput`
- `NoConnect`

`connection_requirement` values are:

- `Optional`
- `Required`
- `MustNotConnect`

## Component Definitions

Component definitions describe reusable logical component shapes:

```json
{
  "id": "component_def:0",
  "name": "LED",
  "source": {
    "namespace": "volt.optos",
    "name": "led_2pin",
    "version": "1.0.0"
  },
  "pins": ["pin_def:0", "pin_def:1"],
  "properties": {}
}
```

`source` is optional. `pins` contains `pin_def` IDs in component-definition pin order.

## Components

Components are concrete design occurrences:

```json
{
  "id": "component:0",
  "definition": "component_def:0",
  "reference": "D1",
  "properties": {},
  "electrical_attributes": {
    "forward_voltage": { "type": "quantity", "dimension": "voltage", "value": 2 }
  },
  "selected_physical_part": {
    "manufacturer_part": {
      "manufacturer": "Lite-On",
      "part_number": "LTST-C190KRKT"
    },
    "package": "0603",
    "footprint": {
      "library": "leds",
      "name": "LED_0603_1608Metric"
    },
    "pin_pad_mappings": [
      { "pin": "pin_def:1", "pad": "1" },
      { "pin": "pin_def:0", "pad": "2" }
    ],
    "properties": {},
    "electrical_attributes": {
      "current_rating": { "type": "quantity", "dimension": "current", "value": 0.02 }
    }
  }
}
```

`selected_physical_part` is optional. A selected part maps logical `pin_def` IDs to
physical footprint pad labels. The mapping must exactly match the component definition's
pins: no missing logical pins, no foreign logical pins, no duplicate logical pins, and no
duplicate pad labels.

## Concrete Pins

Concrete pins are first-class persisted entities because nets connect concrete pins and
future diagnostics or views may need stable pin targets:

```json
{
  "id": "pin:0",
  "component": "component:0",
  "definition": "pin_def:0"
}
```

A concrete pin's `definition` must be one of the pins in its component's component
definition.

## Nets

Nets store canonical logical connectivity:

```json
{
  "id": "net:0",
  "name": "GND",
  "kind": "Ground",
  "pins": ["pin:1", "pin:3"],
  "electrical_attributes": {
    "voltage": { "type": "quantity", "dimension": "voltage", "value": 0 }
  }
}
```

`kind` values are the public `NetKind` spellings:

- `Signal`
- `Power`
- `Ground`
- `Clock`
- `Analog`
- `HighCurrent`

A pin may appear in at most one net.

Net `electrical_attributes` use the same typed payload encoding described above and are
for design-bearing net facts such as nominal voltage. Empty net electrical attribute maps
are omitted from canonical output and load as empty maps.

## Reader Validation

Reading a file is a mutation boundary. Readers must reject structurally invalid files,
including:

- unsupported `format` or `version`
- missing required fields
- duplicate local IDs within an entity type
- local IDs with the wrong typed prefix
- references to missing IDs
- references to IDs of the wrong type
- duplicate component reference designators
- duplicate net names
- component definitions referencing missing pin definitions
- components referencing missing component definitions
- pins referencing missing components or pin definitions
- pins whose definitions are not part of their component definition
- nets referencing missing pins
- a pin appearing in more than one net
- selected part mappings that do not exactly match the component definition
- empty structural strings such as names, reference designators, package labels, footprint
  labels, manufacturer names, part numbers, and pad labels
- invalid enum or property type spellings
- invalid electrical attribute value types, dimensions, modes, numbers, or ranges

Design-quality issues remain diagnostics, not reader failures, when the structure is
valid. Examples include unconnected required pins, single-pin nets, incompatible driver
roles, and future power-domain findings.

## Example

A compact LED circuit may be represented as:

```json
{
  "format": "volt.logical_circuit",
  "version": 1,
  "pin_definitions": [
    { "id": "pin_def:0", "name": "+", "number": "1", "role": "Passive", "connection_requirement": "Required" },
    { "id": "pin_def:1", "name": "-", "number": "2", "role": "Passive", "connection_requirement": "Required" },
    { "id": "pin_def:2", "name": "1", "number": "1", "role": "Passive", "connection_requirement": "Required" },
    { "id": "pin_def:3", "name": "2", "number": "2", "role": "Passive", "connection_requirement": "Required" },
    { "id": "pin_def:4", "name": "A", "number": "1", "role": "Passive", "connection_requirement": "Required" },
    { "id": "pin_def:5", "name": "K", "number": "2", "role": "Passive", "connection_requirement": "Required" }
  ],
  "component_definitions": [
    { "id": "component_def:0", "name": "Two-pin connector", "pins": ["pin_def:0", "pin_def:1"], "properties": {} },
    { "id": "component_def:1", "name": "Resistor", "source": { "namespace": "volt.passives", "name": "resistor_2pin", "version": "1.0.0" }, "pins": ["pin_def:2", "pin_def:3"], "properties": { "category": { "type": "string", "value": "passive" } } },
    { "id": "component_def:2", "name": "LED", "source": { "namespace": "volt.optos", "name": "led_2pin", "version": "1.0.0" }, "pins": ["pin_def:4", "pin_def:5"], "properties": {} }
  ],
  "components": [
    { "id": "component:0", "definition": "component_def:0", "reference": "J1", "properties": {} },
    { "id": "component:1", "definition": "component_def:1", "reference": "R1", "properties": { "value": { "type": "string", "value": "330 ohm" } } },
    { "id": "component:2", "definition": "component_def:2", "reference": "D1", "properties": {} }
  ],
  "pins": [
    { "id": "pin:0", "component": "component:0", "definition": "pin_def:0" },
    { "id": "pin:1", "component": "component:0", "definition": "pin_def:1" },
    { "id": "pin:2", "component": "component:1", "definition": "pin_def:2" },
    { "id": "pin:3", "component": "component:1", "definition": "pin_def:3" },
    { "id": "pin:4", "component": "component:2", "definition": "pin_def:4" },
    { "id": "pin:5", "component": "component:2", "definition": "pin_def:5" }
  ],
  "nets": [
    { "id": "net:0", "name": "VCC", "kind": "Power", "pins": ["pin:0", "pin:2"] },
    { "id": "net:1", "name": "LED_A", "kind": "Signal", "pins": ["pin:3", "pin:4"] },
    { "id": "net:2", "name": "GND", "kind": "Ground", "pins": ["pin:5", "pin:1"] }
  ]
}
```

The example omits selected physical parts for brevity; component rows may include
`selected_physical_part` as shown above.

## Versioning

Version 1 readers must require:

- `format` equal to `volt.logical_circuit`
- `version` equal to `1`

The v1 reader may ignore unknown fields until an extension mechanism is defined. Unknown
fields are not preserved when rewriting canonical output, so producers must not rely on
unknown fields for data that should round-trip. Future versions should document migration
or rejection behavior before adding incompatible semantics. See
[schema-versioning.md](schema-versioning.md) for the shared compatibility policy.
