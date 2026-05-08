# Python API Boundary

Volt's Python layer should be an expressive authoring surface over kernel-owned state. It
should make circuit generation pleasant without becoming the circuit kernel.

The current focus is logical circuit generation:

- create component definitions and instances
- create nets
- connect pins to nets
- attach values, selected parts, and properties
- validate through kernel diagnostics
- serialize deterministic logical circuit files

Schematic drawing, PCB design, and richer ERC remain planned layers. The Python API should
not introduce semantics that those future kernel layers cannot load, validate, serialize,
or inspect.

## Core Rule

```text
Python is syntax over kernel-owned state.
```

Python objects may provide convenient handles, overloaded operators, and concise catalog
helpers. Meaningful operations must lower into C++ kernel data or C++ kernel mutation
APIs.

Examples:

- `net += pin` lowers to a kernel connectivity mutation.
- `design.R(resistance=10_000)` lowers to kernel component definition and instance data.
- `design.validate()` returns kernel-produced diagnostics.
- `design.write("board.volt.json")` serializes kernel-owned circuit data.

Python should not contain Python-only EDA meaning. If removing the Python runtime would
make a design impossible to load, validate, serialize, or inspect, the data belongs in the
kernel first.

This also applies to typed electrical semantics. The public Python API should prefer
natural component-specific keyword arguments over requiring users to multiply by Volt unit
objects:

```python
r1 = d.R(resistance=330, tolerance=0.01, ref="R1")
c1 = d.C(capacitance=100e-9, voltage_rating=16, ref="C1")
vdd = d.net("VDD", voltage=3.3)
```

Python helpers may use documented contextual defaults for plain numbers, such as ohms for
`resistance` or volts for `voltage`. Those defaults are an authoring contract, not kernel
unit guessing: helpers must lower values into kernel-owned quantities, ratings, pin
specs, or constraints before they affect ERC or persistence. Explicit unit objects or
string parsers can still exist for uncommon units and importers, but they should not be
required for ordinary authoring.

## Binding Boundary

The first Python implementation should use an optional binding target over the stable
public kernel API. `pybind11` is the preferred first binding library because it is mature,
widely understood, and fits the project's current CMake-based C++ workflow. `nanobind`
may be reconsidered later if binding compile time, binary size, or call overhead becomes a
measured problem.

The binding stack should be layered:

```text
volt._volt
  private bindings over C++ kernel operations, IDs, diagnostics, and writer

volt
  Pythonic authoring API backed by volt._volt
```

Most users should write against `volt`, not `volt._volt`. The private module exists to
keep the first binding small and explicit while the public API settles. A public lower
level inspection module can be added later when the kernel surface it should expose is
clear.

## Design Root

Python should expose a `Design` root:

```python
import volt

design = volt.Design("divider")
```

For the first implementation, `Design` owns one kernel logical circuit. Future kernel
layers may add schematic projections, PCB layouts, constraints, and reports under the same
root, but the first Python milestone should stay focused on logical circuit generation.

Python handles should be lightweight views over kernel-owned IDs:

```text
Design
  owns kernel Design or Circuit

Component handle
  references ComponentId

Pin handle
  references PinId

Net handle
  references NetId
```

Handles may cache display labels for convenience, but the kernel remains authoritative. If
a handle and the kernel disagree, the kernel wins.

## Authoring Style

The Python API should feel like a circuit language rather than a direct C++ mirror:

```python
import volt

d = volt.Design("voltage_divider")

vin = d.net("VIN", kind="power")
vout = d.net("VOUT")
gnd = d.net("GND", kind="ground")

r_top = d.R(resistance=10_000)
r_bottom = d.R(resistance=20_000)

vin += r_top[1]
vout += r_top[2], r_bottom[1]
gnd += r_bottom[2]

report = d.validate()
d.write("voltage_divider.volt.json")
```

This syntax is intentionally ergonomic, but the operation sequence is still explicit:

1. Create nets in the kernel.
2. Create component instances in the kernel.
3. Connect pins to nets through kernel mutation APIs.
4. Validate through kernel validation passes.
5. Serialize kernel-owned data.

## Current MVP

The first implemented Python slice supports logical authoring only:

```python
import volt

design = volt.Design("led")

vcc = design.net("VCC", kind="power")
led_a = design.net("LED_A")
gnd = design.net("GND", kind="ground")

j1 = design.connector_1x02(ref="J1")
r1 = design.R("330 ohm", ref="R1")
d1 = design.LED(ref="D1")

vcc += j1[1], r1[1]
led_a += r1[2], d1["A"]
gnd += d1["K"], j1[2]

report = design.validate()
assert not report.has_errors

design.write("led.volt.json")
```

`Design`, `Component`, `Pin`, and `Net` are lightweight Python handles over kernel-owned
IDs. The Python package does not own component definitions, net membership, validation
rules, or serialization semantics.

Catalog helpers such as `Design.R()`, `Design.C()`, `Design.LED()`, and
`Design.connector_1x02()` define reusable kernel component definitions lazily per design
and instantiate concrete components through the C++ mutation API. In the current MVP,
legacy positional component values are stored as kernel component properties. Natural
keyword arguments such as `resistance`, `capacitance`, `tolerance`, `voltage_rating`, and
net `voltage` lower plain numbers into typed kernel electrical attributes.

Diagnostics are inspectable Python objects created from kernel-produced diagnostic data:

```python
for diagnostic in design.validate():
    print(diagnostic.severity, diagnostic.code, diagnostic.message)
```

## Custom Component Definitions

Python can define reusable logical component definitions when the built-in helpers are not
enough:

```python
opamp = d.define_component(
    "OpAmp",
    pins=[
        volt.PinSpec("OUT", 1, role="output"),
        volt.PinSpec("IN-", 2, role="input"),
        volt.PinSpec("IN+", 3, role="input"),
        volt.PinSpec("V-", 4, role="power"),
        volt.PinSpec("V+", 8, role="power"),
    ],
)

u1 = d.instantiate(opamp, ref="U1")
vout = d.net("VOUT")
vout += u1["OUT"]
```

`PinSpec` data lowers into kernel-owned pin definitions. `ComponentDefinition` and
`Component` Python objects are handles over kernel IDs; the Python layer does not own the
component model. Pin roles use the same concepts as the logical format, written naturally
in lowercase: `passive`, `input`/`digital_input`, `output`/`digital_output`,
`analog_input`, `analog_output`, `bidirectional`, `power`/`power_input`, `power_output`,
`ground`, and `no_connect`. Connection requirements are `required`, `optional`, and
`must_not_connect`.

## Selected Physical Parts

Component definitions describe the logical shape of a device. Component instances are
concrete occurrences in the design. A selected physical part attaches the implementation
choice for one component instance:

```python
r1 = d.R(resistance=330, tolerance=0.01, ref="R1")

r1.select_part(
    manufacturer="Yageo",
    part_number="RC0603FR-07330RL",
    package="0603",
    footprint=("Resistor_SMD", "R_0603_1608Metric"),
    pin_pads={
        1: "1",
        2: "2",
    },
    properties={"supplier": "Digi-Key"},
    voltage_rating=75,
    power_rating=0.1,
)
```

For custom components, `pin_pads` may use pin names:

```python
u1.select_part(
    manufacturer="Texas Instruments",
    part_number="TLV9002IDR",
    package="SOIC-8",
    footprint=("Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"),
    pin_pads={
        "OUT": "1",
        "IN-": "2",
        "IN+": "3",
        "V-": "4",
        "V+": "8",
    },
    voltage_rating=5.5,
)
```

`select_part()` lowers into the kernel's `PhysicalPart` selection for that component. The
manufacturer identity, package, footprint, pin-pad mapping, properties, and selected-part
ratings serialize through logical JSON as `selected_physical_part`. The kernel rejects
structurally invalid mappings, including missing logical pins, unknown pins, duplicate
logical pins, and duplicate physical pads.

Selected-part manufacturer information is not only BOM metadata. It is the selected
physical implementation record that owns package, footprint, logical-pin-to-pad mapping,
and selected-part electrical ratings. `voltage_rating` and `power_rating` lower into
typed selected-part electrical attributes.

## Composition

Reusable circuit construction should start as ordinary Python functions that receive a
`Design` and explicit ports:

```python
def voltage_divider(d, vin, vout, gnd, top=10_000, bottom=20_000):
    r_top = d.R(resistance=top)
    r_bottom = d.R(resistance=bottom)

    vin += r_top[1]
    vout += r_top[2], r_bottom[1]
    gnd += r_bottom[2]

    return {"top": r_top, "bottom": r_bottom, "out": vout}
```

This avoids hidden global design state and keeps data flow visible. Decorator syntax such
as `@subcircuit` is not required for v1. It can be considered later only if the underlying
kernel-backed block and hierarchy semantics are already clear.

An explicit block API may be added later:

```python
feedback = d.net("FEEDBACK")

with d.block("feedback"):
    voltage_divider(d, vin, feedback, gnd)
```

If blocks become meaningful, their hierarchy/provenance metadata should be kernel-owned.
Python block syntax should not create Python-only hierarchy.

## Error And Diagnostic Mapping

Structural errors should raise Python exceptions because they represent rejected kernel
mutations:

- missing IDs
- duplicate component references
- duplicate net names
- pin connected to more than one net
- selected part mappings that do not match the logical component definition

Design-quality issues should remain diagnostics:

```python
report = d.validate()
for diagnostic in report:
    print(diagnostic.severity, diagnostic.code, diagnostic.message)
```

Examples include unconnected required pins, single-pin nets, incompatible output drivers,
and future ERC findings. Python may format diagnostics, but validation logic belongs in
the kernel.

## Future Simulation Authoring

Volt should become simulation-ready without making Python the owner of simulation meaning.
Python may eventually let users author behavioral models, but those behaviors must attach
to kernel-owned model contracts with typed parameters, units, scheduling semantics,
state/result data, and validation.

SPICE should be treated as a possible future backend or export adapter, not the canonical
Python API shape. No Python simulation engine, SPICE integration, or solver API should be
added before the C++ kernel owns the model contracts.

## Future Projection Layers

Schematic and PCB Python APIs should follow the same rule:

```text
Python syntax creates or edits kernel-owned projection data.
Projection data references the circuit.
Projection data does not mutate circuit connectivity.
```

This is an ordering constraint, not only a style preference. Python schematic drawing
should not be implemented until the C++ kernel has a schematic representation, mutation
API, validation story, and serialization plan. Python PCB layout APIs should wait for the
same kernel foundation in the board layer.

For example, a future schematic API may place symbols or draw visual representations of
nets, but it should not create, merge, split, or reconnect logical nets. A future PCB API
may place footprints and route copper for an existing net, but it should not define the
netlist.

Those APIs are intentionally deferred until the logical circuit generation layer is stable.
