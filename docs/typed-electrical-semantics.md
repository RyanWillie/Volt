# Typed Electrical Semantics

## Purpose

Volt has a kernel-owned foundation for electrical meaning that can support richer
validation without becoming simulation-specific.

The guiding rule remains:

```text
Volt should become simulation-ready without becoming simulation-specific yet.
```

This document records the current typed semantics model, the boundaries it protects, and
the remaining work needed before ERC, schematic, PCB, and future simulation layers can
lean on it confidently.

## Ownership Rule

`PropertyMap` remains useful for arbitrary metadata, import provenance, datasheet links,
lifecycle notes, contributor annotations, and escape-hatch facts that Volt does not yet
understand.

Core electrical meaning belongs in typed kernel-owned fields when Volt needs to:

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
  pin voltage range = 0 V to 5.5 V
```

If a fact changes EDA behavior, the C++ kernel owns the fact. Python may make it easier to
author, but Python must lower it into kernel data or kernel mutation APIs.

Volt should store design-defining electrical inputs, not every electrical concept that can
be calculated from them. A field belongs in the model when it affects connectivity,
component selection, ERC, constraints, serialization, PCB readiness, or future simulation
setup.

Examples:

- store a resistor's resistance, tolerance, and power rating
- store a capacitor's capacitance and voltage rating
- store a supply net's nominal voltage when checks depend on it
- store a pin's accepted voltage range when validation depends on it
- do not store derived current, node voltage, charge, temperature, or power dissipation as
  design fields unless they are explicit constraints or saved analysis results

This keeps the kernel small while leaving room for future analysis layers to produce
state and result data without confusing those results with circuit design intent.

## Current Foundation

The landed foundation includes:

- `UnitDimension`, `Quantity`, `Tolerance`, and `QuantityRange`
- `ElectricalAttributeSpec`, `ElectricalAttributeValue`, and `ElectricalAttributeMap`
- owner and dimension checks for typed electrical attributes
- component instance electrical attributes
- selected physical part electrical attributes
- net electrical attributes, including authored net voltage
- pin definition electrical attributes, including authored voltage ranges
- unified pin semantics on `PinDefinition` rather than a separate pin electrical spec
- logical JSON read/write support for typed electrical attributes
- validation entry points for general, connectivity, ERC, and PCB-readiness checks
- typed diagnostics for power/ground sanity, pin voltage range, selected-part voltage
  rating, and missing selected physical parts for PCB readiness
- Python authoring helpers over kernel-owned state

The important architectural result is that Python and JSON are no longer the only places
where these facts exist. The kernel can inspect, serialize, and validate them directly.

## Quantities And Attributes

The current quantity model is intentionally small:

```text
UnitDimension
  resistance | capacitance | inductance | voltage | current | power |
  frequency | time | temperature | ratio

Quantity
  dimension: UnitDimension
  value: canonical numeric value

Tolerance
  minus: ratio
  plus: ratio

QuantityRange
  dimension: UnitDimension
  minimum: optional Quantity
  maximum: optional Quantity
```

Display units, unit spelling, and scale-preserving formatting are deferred. Validation
does not depend on whether a user wrote `10 kOhm`, `10000 ohm`, or a Python helper
lowered a plain number to an explicit dimensioned quantity.

Typed electrical attributes are named values with an expected owner and dimension. This
avoids a giant universal component field list while still making common design facts
kernel-owned.

Current useful owners are:

```text
ComponentInstance
  design-specific value choices
  examples: resistance = 330 ohm, capacitance = 100 nF

SelectedPhysicalPart
  manufacturer-specific ratings and limits
  examples: tolerance = +/-1%, voltage_rating = 75 V, power_rating = 0.1 W

Net
  authored net-level electrical intent
  examples: voltage = 3.3 V

PinDefinition
  logical pin electrical limits
  examples: voltage_range = 0 V to 5.5 V
```

Structural checks happen at construction, mutation, or load boundaries:

- attribute name must be known
- owner must be allowed for that attribute
- quantity dimension must match the attribute
- ranges must have compatible dimensions
- range minimum cannot exceed range maximum

Design-quality issues remain diagnostics. For example, a selected part whose voltage
rating is too low for an authored net voltage is diagnosable; it should not make the
loaded circuit structurally invalid.

## Pin Semantics

Volt should not grow hundreds of narrow pin roles such as `threshold_input`,
`reset_input`, or `discharge_output`. Those names are useful datasheet vocabulary, but
they are not the fundamental model.

The current direction is a unified `PinSpec`/`PinDefinition` with a small set of
first-principles fields:

```text
PinDefinition
  name
  number
  role
  connection_requirement
  terminal_kind
  direction
  signal_domain
  drive_kind
  polarity
  electrical_attributes
```

This lets authoring stay compact while preserving behavior:

```python
ne555 = d.define_component(
    "NE555 timer",
    pins=[
        volt.PinSpec("GND", 1, role="ground", terminal="ground"),
        volt.PinSpec(
            "TRIG", 2, role="analog_input", terminal="signal",
            direction="input", signal="analog", voltage_range=(0, 5.5)
        ),
        volt.PinSpec(
            "OUT", 3, role="output", terminal="signal",
            direction="output", signal="digital", drive="push_pull"
        ),
        volt.PinSpec(
            "RESET", 4, role="input", terminal="signal",
            direction="input", signal="digital", polarity="active_low"
        ),
        volt.PinSpec("CTRL", 5, role="analog_input", terminal="signal"),
        volt.PinSpec("THRESH", 6, role="analog_input", terminal="signal"),
        volt.PinSpec(
            "DISCH", 7, role="output", terminal="signal",
            direction="output", signal="digital", drive="open_drain"
        ),
        volt.PinSpec(
            "VCC", 8, role="power", terminal="power",
            direction="input", voltage_range=(4.5, 16)
        ),
    ],
    properties={"category": "timer"},
)
```

The names `TRIG`, `THRESH`, and `DISCH` still exist as pin names. The kernel-visible
semantics are the simpler facts: analog input, digital output, open-drain, active-low,
required connection, and voltage range.

Future additions should follow the same rule: add a core field only when it represents a
general electrical concept that validation, serialization, import/export, PCB, or future
simulation layers need. Otherwise prefer typed attributes, metadata, or higher-level
authoring helpers that lower into the existing fields.

## Validation Consumption

Validation should run over canonical kernel-owned model data and produce diagnostics. It
should not fix a bad design, mutate nets, or infer missing electrical contracts from
labels such as `VCC` and `GND` except through explicit model fields or validated authoring
helpers.

Current validation entry points are:

```text
validate_circuit
  broad logical validation for normal authoring feedback

validate_connectivity
  focused connectivity checks

validate_electrical_rules
  electrical semantics checks over typed model data

validate_for_pcb
  PCB-readiness checks that require selected physical parts
```

Current typed checks include:

- required pins that are left unconnected
- no-connect pin violations
- empty and single-pin nets
- obvious output conflicts
- power/ground sanity checks
- net voltage against connected pin voltage ranges
- selected-part voltage rating against authored net voltage
- missing selected physical parts when validating for PCB output

Remaining validation work should build on explicit data:

- current limits and power capability checks
- no-connect assertions as stored design intent, distinct from pin roles
- selected-part compatibility beyond voltage rating
- drive and domain compatibility once the constraint model is clearer
- hierarchy and scoped-net validation after those primitives exist
- netclass/rule-class validation once reusable rule classes exist

These are design-quality findings. They remain diagnostics unless they reveal a
structurally invalid mutation such as a pin connected to more than one net.

## Serialization

The logical circuit JSON format now serializes typed electrical attributes for the model
owners that exist today:

- component instances
- selected physical parts
- nets
- pin definitions

Generic `properties` remain for metadata. Core electrical meaning should not be added as
ad hoc strings when a typed kernel field exists.

Loading malformed typed fields is a structural format error. Loading a well-formed but
bad design should succeed and allow validation to report diagnostics.

## Python Authoring

Python provides ergonomic syntax over kernel-owned state:

```python
r1 = d.R(resistance=330, tolerance=0.01, ref="R1")
c1 = d.C(capacitance=100e-9, voltage_rating=16, ref="C1")
vdd = d.net("VDD", voltage=3.3)

r1.select_part(
    manufacturer="Yageo",
    part_number="RC0603FR-07330RL",
    package="0603",
    footprint=("Resistor_SMD", "R_0603_1608Metric"),
    pin_pads={1: "1", 2: "2"},
    voltage_rating=75,
    power_rating=0.1,
)

diagnostics = d.validate_for_pcb()
```

Default units are part of the Python helper contract, not kernel guesswork. For example,
`Design.R(resistance=330)` can mean `330 ohm` because the resistor helper declares that
default. The kernel receives a dimensioned quantity either way.

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
- no schematic or PCB implementation in this layer

## Next Slices

The typed semantics foundation is now in place. The next work should be small and
dependency-aware:

1. Finish the documentation refresh so contributors understand the current model.
2. Write a focused hierarchy and scoped-net design page before adding new logical
   topology.
3. Add the smallest hierarchy/scoped-net vertical slice.
4. Add no-connect assertions as explicit stored design intent.
5. Add selected-part compatibility checks beyond voltage rating.
6. Add current and power capability checks once the relevant constraints are explicit.
7. Design netclasses/rule classes only after the reusable constraint vocabulary is clear.

Each slice should have tests that prove structural invalid data is rejected at the
mutation or load boundary, while bad design intent is reported through diagnostics.
