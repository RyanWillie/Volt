# Component Metadata Foundation Design

## Purpose

Volt needs a metadata model that lets a logical circuit describe both electrical intent
and selected physical implementation without merging those concepts into one overloaded
`Component` object.

The immediate goal is to support values and part-selection metadata for examples such as:

```text
R1
  logical device: Resistor
  value: 330 ohm
  selected part: Yageo RC0603FR-07330RL
  package: 0603
  footprint reference: Resistor_SMD:R_0603_1608Metric
  pin map:
    logical pin 1 -> physical pad 1
    logical pin 2 -> physical pad 2
```

This is still circuit-kernel work. It does not introduce schematic pages, schematic
symbols, board objects, footprint geometry, routing, or manufacturing output.

## Vocabulary

### Logical Device Definition

A logical device definition describes the reusable electrical shape of a thing:

- name, such as `Resistor`, `LED`, or `STM32G0`
- logical pin definitions
- generic pin electrical semantics
- connection requirements
- definition-level properties, such as category or default value metadata

This is what `ComponentDefinition` is becoming. It is not a schematic symbol and it is not
a purchasable part.

### Component Instance

A component instance is one occurrence of a logical device in a circuit:

- reference designator, such as `R1`, `D1`, or `U1`
- link to a logical device definition
- instance-level properties, such as `value = 330 ohm`
- optional selected part reference

This is what `ComponentInstance` represents.

### Selected Part

A selected part describes a concrete purchasable or approved physical implementation:

- manufacturer
- manufacturer part number
- optional supplier metadata
- package reference
- footprint reference
- logical pin to physical pad mapping
- part-level properties, such as tolerance, voltage rating, and power rating

This should be represented separately from `ComponentDefinition` so that one logical
device can be implemented by multiple parts/packages.

### Package

A package is the physical body style, such as `0603`, `SOT-23-5`, `SOIC-8`, or `QFN-48`.
At this stage it should be a validated reference/value object, not geometry.

### Footprint Reference

A footprint reference names a board land-pattern definition without owning footprint
geometry yet. It should be able to represent an external library-style reference:

```text
library: Resistor_SMD
name: R_0603_1608Metric
```

The footprint geometry layer is explicitly deferred.

### Pin/Pad Mapping

Pin/pad mapping belongs with the selected physical implementation, not the logical device
definition.

The mapping answers:

```text
logical PinDefId -> physical pad name/number
```

This prevents a logical `LED` or `OpAmp` definition from accidentally baking in one
package/vendor convention.

## Design Approach

### Recommended Approach

Use first-class typed concepts for core relationships and a small property system for
extensible metadata.

Typed model fields should represent structural relationships:

- component instance -> component definition
- component instance -> selected part
- selected part -> package reference
- selected part -> footprint reference
- selected part -> pin/pad mappings

Properties should carry additional facts:

- `value = "330 ohm"` initially
- `tolerance = "1%"`
- `power_rating = "0.1 W"`
- `datasheet = "https://..."`

This keeps connectivity and part selection type-safe while avoiding a giant field list.

### Alternative 1: Put Everything In Generic Properties

This is flexible and easy to serialize, but it makes important invariants invisible. For
example, a footprint reference or pin mapping stored as arbitrary strings would be hard to
validate and hard to bind cleanly to Python.

This approach is rejected for structural relationships.

### Alternative 2: Fully Typed Domain Model Immediately

This would add dedicated types for quantities, units, tolerances, voltage/current ratings,
supplier offers, lifecycle status, compliance, and assembly metadata now.

This is too much for the current kernel. Those fields are useful later, but adding them
before serialization and real examples would create speculative API surface.

## Initial Implementation Slices

### Slice 1: Core Properties

Add `volt-core` property primitives:

- `PropertyKey`
- `PropertyValue`
- `PropertyMap`

Initial `PropertyValue` variants:

- string
- bool
- signed integer
- double

Do not add quantities/units in this slice. Values like `330 ohm` can begin as strings, and
proper quantities can be added once unit semantics are designed.

Add property maps to:

- `ComponentDefinition`
- `ComponentInstance`

This lets Volt represent logical metadata and instance-specific metadata without changing
the core connectivity model.

### Slice 2: Physical Part Selection

Add circuit-level physical selection types:

- `ManufacturerPart`
- `PackageRef`
- `FootprintRef`
- `PinPadMapping`
- `PhysicalPart`

Add IDs/storage only if needed by `Circuit`; otherwise begin as value objects and attach
them to `ComponentInstance` through an optional selected-part value.

Validation for this slice:

- package reference must be non-empty
- footprint library/name must be non-empty
- pin/pad mappings must reference logical pins from the component definition
- mappings must not duplicate physical pads

## API Shape

Expected usage after the first two slices:

```cpp
auto resistor = circuit.add_component_definition(ComponentDefinition{
    "Resistor",
    std::vector{pin_1, pin_2},
    PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}}},
});

auto r1 = circuit.instantiate_component(resistor, ReferenceDesignator{"R1"});
circuit.component(r1).properties().set(PropertyKey{"value"}, PropertyValue{"330 ohm"});

circuit.select_part(
    r1,
    PhysicalPart{
        ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        PackageRef{"0603"},
        FootprintRef{"Resistor_SMD", "R_0603_1608Metric"},
        std::vector{
            PinPadMapping{pin_1, "1"},
            PinPadMapping{pin_2, "2"},
        },
    });
```

The exact mutability shape may differ depending on the current `Circuit` accessor style,
but the conceptual split should remain.

## Invariants

Structural integrity belongs at mutation boundaries:

- empty property keys are rejected
- empty package references are rejected
- empty footprint library/name values are rejected
- selected-part mappings cannot reference pins outside the logical component definition
- one physical pad should not be mapped from multiple logical pins unless an explicit
  future model supports intentional aliases

Design-quality checks belong in diagnostics:

- instance has no selected part
- selected part has no footprint reference
- value metadata missing for passives
- part ratings may be unsuitable

## Testing Strategy

Use TDD for each slice.

Core property tests:

- `PropertyKey` rejects empty values
- `PropertyValue` stores each supported primitive type
- `PropertyMap` sets, replaces, retrieves, and reports missing keys
- component definitions and instances preserve property maps

Physical selection tests:

- value objects reject empty structural labels
- physical part stores manufacturer/package/footprint/pin-map data
- component instance can carry or select a physical part
- invalid pin mappings fail at the `Circuit` mutation boundary

Integration test:

- update the LED example so `R1` has `value = 330 ohm`
- add selected parts/package/footprint references for `R1`, `D1`, and `J1`
- keep `validate_circuit` clean for logical ERC

## Explicit Deferrals

The following are not part of this design:

- schematic symbols or symbol graphics
- footprint geometry
- PCB pads, traces, zones, or board layers
- unit-aware quantities
- supplier inventory/pricing
- serialization format
- Python bindings

Those layers should build on this vocabulary later, not be mixed into the first metadata
foundation.
