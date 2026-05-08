# Typed Electrical Semantics Design

## Purpose

Volt needs a kernel-owned foundation for electrical meaning before ERC grows beyond the
current broad `PinRole` checks.

The goal is:

```text
Volt should become simulation-ready without becoming simulation-specific yet.
```

This design defines the model trajectory for typed quantities and units, typed component
values and ratings, richer pin electrical specs, ERC consumption, Python authoring, and
the future simulation boundary. It does not add a simulation engine, SPICE integration,
solver APIs, schematic implementation, or PCB work.

## Ownership Rule

`PropertyMap` remains useful for arbitrary metadata, import provenance, datasheet links,
lifecycle notes, contributor annotations, and escape-hatch facts that Volt does not yet
understand.

Core electrical meaning should move into typed kernel-owned fields when Volt needs to:

- validate it
- serialize it with stable semantics
- expose it consistently through C++ and Python
- use it for ERC
- use it as a future simulation contract

Examples:

```text
metadata:
  datasheet = "https://..."
  lifecycle = "active"
  vendor_note = "alternate approved by hardware team"

typed electrical meaning:
  resistance = 330 ohm
  tolerance = +/- 1 percent
  voltage_rating = 75 V
  pin drive kind = push-pull output
  pin voltage domain = VDD_IO
```

If a fact changes EDA behavior, the C++ kernel owns the fact. Python may make it easier to
author, but Python must lower it into kernel data or kernel mutation APIs.

Volt should store design-defining electrical inputs, not every electrical concept that can
be calculated from them. A field belongs in the model when it affects connectivity,
component selection, ERC, constraints, serialization, or future simulation setup.

Examples:

- store a resistor's resistance, tolerance, and power rating
- store a capacitor's capacitance and voltage rating
- store a supply net's nominal voltage or allowed voltage range when checks depend on it
- do not store derived current, node voltage, charge, temperature, or power dissipation as
  design fields unless they are explicit constraints or saved analysis results

This keeps the kernel small while leaving room for future analysis layers to produce
state and result data without confusing those results with circuit design intent.

## Typed Quantities And Units

Volt should introduce a small quantity model before moving electrical values and ratings
out of string properties.

Recommended first vocabulary:

```text
Unit
  dimension: resistance | capacitance | inductance | voltage | current | power |
             frequency | time | temperature | ratio
  scale: integer exponent or canonical multiplier
  symbol: display spelling such as ohm, kOhm, V, mA, W, Hz, s, percent

Quantity
  dimension: UnitDimension
  value: decimal-compatible numeric value
  unit: Unit

Tolerance
  mode: absolute | percent
  plus: Quantity or ratio
  minus: Quantity or ratio

Range
  minimum: optional Quantity
  maximum: optional Quantity
```

The kernel should store quantities in a canonical dimension-aware form so `10 kOhm`,
`10000 ohm`, and Python-authored plain numbers passed through helper defaults can be
compared deterministically. Display spelling can be preserved later if user-facing
formatting needs it, but validation should not depend on the original string.

Structural checks belong at construction or mutation boundaries:

- quantity dimension must match the field that stores it
- ranges must have compatible dimensions
- range minimum cannot exceed range maximum
- percent tolerance belongs to ratio-like fields, not arbitrary strings

Do not introduce implicit unit guessing in the kernel. Authoring helpers may provide
contextual defaults, such as treating `resistance=330` in `Design.R(...)` as ohms, but
the helper must produce an explicit `Quantity` before entering model data.

## Component Values And Ratings

Typed values should be attached to kernel component definitions, component instances, and
selected physical parts based on ownership:

```text
ComponentDefinition
  nominal electrical behavior common to the logical device
  examples: kind = resistor, expected value dimension = resistance

ComponentInstance
  design-specific value choices
  examples: resistance = 330 ohm, capacitance = 100 nF

PhysicalPart
  manufacturer-specific ratings and limits
  examples: tolerance = +/-1%, voltage_rating = 75 V, power_rating = 0.1 W
```

Avoid a giant universal component field list. Start with a small typed electrical
attribute vocabulary whose entries define:

- the canonical name, such as `resistance` or `voltage_rating`
- the expected dimension
- the valid owner, such as component instance, selected part, pin spec, net, or constraint
- the default authoring unit for plain numbers
- whether the attribute is design input or constraint

Common attributes can be added or removed as the model matures without making every
component carry irrelevant fields. Specialized structures should be introduced only when a
validation or simulation contract needs stronger relationships than named typed
attributes can express. Future analysis results should have separate ownership rather
than being stored as ordinary circuit design attributes.

Initial design-input attributes should likely cover:

- resistance, capacitance, inductance
- tolerance
- voltage rating
- current rating
- power rating
- operating temperature range

Design-quality diagnostics, not mutation failures, should report missing or unsuitable
choices such as a resistor instance with no resistance value or a selected part whose
rating is lower than a declared design requirement.

## Pin Electrical Specs

`PinRole` is useful as a coarse compatibility hint, but it is too broad to be the basis
for expanded ERC. Volt should evolve toward a richer `PinElectricalSpec` associated with
`PinDefinition`.

Suggested shape:

```text
PinElectricalSpec
  connection_requirement: Optional | Required | MustNotConnect
  terminal_kind: passive | power | ground | signal | no_connect
  direction: input | output | bidirectional | passive | not_applicable
  signal_kind: digital | analog | mixed | clock | reset | power_control | unknown
  drive_kind: high_impedance | open_drain | open_source | push_pull |
              current_source | current_sink | passive | unknown
  supply_role: supply_input | supply_output | reference | return | not_supply
  voltage_domain: optional domain name/reference
  accepted_voltage_range: optional Range<voltage>
  produced_voltage_range: optional Range<voltage>
  current_limit: optional Range<current>
  properties: PropertyMap for non-core annotations
```

This model separates concerns that are currently collapsed into enum values. For example:

- a GPIO can be digital, bidirectional, push-pull or open-drain, and tied to `VDD_IO`
- a regulator output can be a supply output with a produced voltage range
- a passive resistor pin can be passive with no direction
- a ground pin can be a supply return rather than a generic signal

`PinRole` can remain as a compatibility field or derived summary while the richer spec is
introduced. New ERC should consume `PinElectricalSpec` rather than adding increasingly
special cases to `PinRole`.

## ERC Consumption

ERC should run over canonical kernel-owned model data and produce diagnostics. It should
not fix a bad design, mutate nets, or infer missing electrical contracts from labels such
as `VCC` and `GND` except through explicit model fields or validated authoring helpers.

Staged ERC trajectory:

1. Keep existing structural connectivity invariants in `Circuit` mutation APIs.
2. Keep existing logical diagnostics for unconnected required pins, empty nets,
   single-pin nets, no-connect violations, and obvious output conflicts.
3. Add typed electrical semantics to the model and persistence format.
4. Rework richer ERC checks to read `PinElectricalSpec`, typed values, ratings, and
   declared constraints.
5. Add power-domain and rating checks only when the domain, voltage, current, and rating
   data is explicit enough to make findings diagnosable.

Examples of future diagnostics:

- power input has no compatible supply source on its net
- open-drain output net has no pull-up to a compatible voltage domain
- two push-pull outputs drive the same net
- component rating is below the declared net or environment requirement
- analog input is connected to a net outside its accepted voltage range

These are design-quality findings. They remain diagnostics unless they reveal a
structurally invalid mutation such as a pin connected to more than one net.

## Serialization Implications

The current logical circuit JSON format should not be stretched with ad hoc string
properties for core electrical meaning. Typed semantics should be added through explicit
versioned fields once the C++ model exists.

Expected format direction:

- keep generic `properties` for metadata
- add typed quantity encodings with dimension and canonical value
- add typed value/rating fields to component instances and selected physical parts
- add `electrical_spec` or equivalent fields to pin definitions
- preserve deterministic field order and round-trip fixtures
- reject malformed typed fields during load as structural format errors
- report bad design intent after load through validation diagnostics

Migration can preserve old string properties such as `value = "330 ohm"` as metadata
until a deliberate importer or authoring helper converts them into typed fields.

## Python Authoring Expectations

Python should make typed electrical authoring pleasant without owning electrical meaning.

Expected authoring style:

```python
r1 = d.R(resistance=330, tolerance=0.01, ref="R1")
c1 = d.C(capacitance=100, voltage_rating=16, ref="C1")
vdd = d.net("VDD", voltage=3.3)
```

Those calls must lower to kernel-owned quantities, component values, ratings, pin specs,
constraints, or net semantics. The public Python API should not require users to multiply
by Volt unit objects for common component helpers. Instead, helper definitions should own
the expected dimensions and default units for plain numbers, then pass explicit quantities
to the kernel.

Default units are part of the Python helper contract, not kernel guesswork. For example,
`Design.R(resistance=330)` can mean `330 ohm` because the resistor helper declares that
default. A capacitor helper may choose an ergonomic default such as nanofarads if that is
documented by the public API, but the kernel receives a dimensioned capacitance either
way. Python may still offer explicit unit objects or parsers for uncommon units and
importers, but those are conveniences rather than required authoring syntax.

Python exceptions remain appropriate for rejected structural mutations or invalid typed
values. ERC and design-quality issues remain kernel-produced diagnostics.

## Future Simulation Boundary

Volt should be simulation-ready, not simulation-specific.

The C++ kernel should eventually own:

- model contracts that describe what a component exposes to a simulator
- typed units and state variables
- declared parameters and ratings
- scheduling semantics for time, events, and analysis types if/when they exist
- simulation input and result data structures
- diagnostics for invalid or incomplete simulation setup

Python may eventually let users author behavioral models, but those behaviors must
register against kernel-owned contracts. Removing the Python runtime must not make the
design's electrical contracts, units, validation results, saved setup, or simulation
results impossible to inspect.

SPICE should be treated as a possible future backend or export adapter. It should not be
Volt's canonical simulation architecture unless a later design deliberately chooses that
after the kernel model contracts exist.

Explicitly deferred:

- no simulation engine
- no SPICE integration
- no solver APIs
- no transient, AC, DC, or mixed-signal analysis APIs
- no schematic or PCB implementation

## Implementation Slices

The design should land before issue `c9042835` basic power/ground checks or any similar
ERC expansion. Suggested implementation order:

1. Define quantity and unit value objects in `volt-core`.
2. Add typed passive values and selected-part ratings to the logical model.
3. Extend serialization with explicit typed fields and golden fixtures.
4. Add `PinElectricalSpec` beside or behind the existing `PinRole`.
5. Update authoring helpers and Python bindings to produce typed kernel data.
6. Expand ERC against typed semantics and constraints.
7. Only then design simulation contracts or backend adapters.

Each slice should have tests that prove structural invalid data is rejected at the
mutation or load boundary, while bad design intent is reported through diagnostics.
