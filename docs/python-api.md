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
- `design.R("10k")` lowers to kernel component definition and instance data.
- `design.validate()` returns kernel-produced diagnostics.
- `design.write("board.volt.json")` serializes kernel-owned circuit data.

Python should not contain Python-only EDA meaning. If removing the Python runtime would
make a design impossible to load, validate, serialize, or inspect, the data belongs in the
kernel first.

## Binding Boundary

The first Python implementation should use an optional binding target over the stable
public kernel API. `pybind11` is the preferred first binding library because it is mature,
widely understood, and fits the project's current CMake-based C++ workflow. `nanobind`
may be reconsidered later if binding compile time, binary size, or call overhead becomes a
measured problem.

The binding stack should be layered:

```text
volt.kernel
  thin bindings over C++ kernel objects, IDs, diagnostics, reader, and writer

volt
  Pythonic authoring API backed by volt.kernel
```

Most users should write against `volt`, not `volt.kernel`. The lower-level module exists
for tests, debugging, advanced inspection, and future tooling.

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

r_top = d.R("10k")
r_bottom = d.R("20k")

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

## Composition

Reusable circuit construction should start as ordinary Python functions that receive a
`Design` and explicit ports:

```python
def voltage_divider(d, vin, vout, gnd, top="10k", bottom="20k"):
    r_top = d.R(top)
    r_bottom = d.R(bottom)

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
