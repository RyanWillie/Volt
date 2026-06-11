# Python API Boundary

Volt's Python layer should be an expressive authoring surface over kernel-owned state. It
should make circuit generation pleasant without becoming the circuit kernel.

The current Python surface covers logical circuit generation, schematic projection
authoring, PCB layout authoring, and staged project runs:

- create component definitions and instances
- create nets
- connect pins to nets
- attach values, selected parts, and properties
- validate through kernel diagnostics
- serialize deterministic logical circuit files
- create schematic sheets
- place existing logical components with built-in schematic symbols
- draw schematic wire runs from anchors, pins, ports, and explicit points
- place schematic net labels, power/ground ports, junctions, sheet ports, and
  no-connect markers over existing logical nets and pins
- serialize deterministic schematic projection files
- author PCB board outlines, layers, footprint placement, board primitives, and copper
  routing over kernel-owned board state
- serialize deterministic PCB projection files
- define reusable `Part` objects in buildable `Library` collections
- validate library parts for board readiness, pad mapping, footprint geometry, and
  serializability
- run staged projects with default diagnostics, product-intent tests, and bundle output

Richer ERC, a simulation foundation, manufacturing outputs, and deeper PCB flows remain
planned layers. The Python API should not introduce semantics that those future kernel
layers cannot load, validate, serialize, or inspect.

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

Python exposes a `Design` root:

```python
import volt

design = volt.Design("divider")
```

`Design` owns one kernel logical circuit and can create kernel-owned schematic and PCB
projection data over that circuit. Future kernel layers may add constraints and reports
under the same root.

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

## Current Logical Authoring

Logical authoring starts from ordinary Python handles:

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

Net-class authoring follows the same boundary. For example,
`design.net_class(current=1.0, temp_rise=10)` is Python syntax over a kernel-owned
`NetClass`; the IPC calculator, resulting rule value, and provenance are stored,
validated, and serialized by the C++ kernel.

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

`Design.validate()` runs the default logical validation suite. `Design.validate_for_pcb()`
adds PCB-readiness checks, including selected physical part requirements, without making
selected parts mandatory for logical-only designs.

## Project Framework

`Project` is the canonical Python entry point when a design should behave like a product
workflow instead of a loose script. It keeps the common flow explicit: design first,
schematic second, PCB third. Stage decorators register the functions that actually author
those models:

```python
project = volt.Project("status-led", version="0.1.0")


@project.design
def design():
    d = volt.Design("status-led")
    vcc = d.net("VCC", kind="power")
    led_a = d.net("LED_A")
    gnd = d.net("GND", kind="ground")
    j1 = d.connector_1x02(ref="J1")
    r1 = d.R("330 ohm", ref="R1")
    d1 = d.LED(ref="D1")

    vcc += j1[1], r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"], j1[2]
    return d


@project.schematic
def schematic(context):
    design = context.design()
    sheet = design.schematic("Main")
    sheet.place(design.component("J1"), at=(45, 60))
    sheet.place(design.component("R1"), at=(80, 60))
    sheet.place(design.component("D1"), at=(115, 60))
    return sheet


@project.board
def board(context):
    design = context.design()
    pcb = design.board("Main")
    pcb.set_rectangular_outline(origin=(0, 0), size=(32, 18))
    pcb.place(design.component("J1"), at=(5, 9), locked=True)
    pcb.place(design.component("R1"), at=(15, 7))
    pcb.place(design.component("D1"), at=(24, 7), rotation=180)
    return pcb


result = project.run()
result.write("dist/status-led.volt")
```

The schematic and PCB stages above are intentionally short to show the framework shape.
A clean `result.ok` also requires normal projection completeness, such as schematic
visual net coverage and selected physical parts for placed PCB components.

`project.run()` executes registered stages in order and returns `ProjectResult`.
`result.ok` is false when default diagnostics have errors or stage-attached tests fail.
Later stages always receive a `volt.BuildContext`, even for the common single-design case;
use `context.design()` to reach that design and `context.resource(...)` for explicit
authoring resources.
`result.write(path)` writes a deterministic directory bundle with logical JSON,
schematic JSON/SVG, PCB JSON/SVG, diagnostics, test results, and
`manifest.volt.json`. The output path may be missing, empty, or an existing Volt
project-result bundle. Other pre-existing content is rejected so the write does not
delete unrelated files.

Stages can also own product-intent tests. These tests are not a replacement for kernel
diagnostics; they encode the specific behavior the product must keep while the circuit
iterates:

```python
@project.design.test
def power_path(check):
    check.net("VCC").connects("J1.1", "R1.1")
    check.net("GND").connects("J1.2", "D1.K")
    check.no_connection("VCC", "GND")


@project.schematic.test
def placed_on_sheet(check):
    check.places("J1", "R1", "D1")


@project.board.test
def board_placement(check):
    check.has_outline()
    check.places("J1", "R1", "D1")
```

When a stage returns multiple models, attached tests receive an explicit multi-model helper
instead of the single-model `check` surface. Use `check.names()` for aggregate assertions,
`check.design(...)` / `check.schematic(...)` / `check.board(...)` to target one model, and
`check.designs()` / `check.schematics()` / `check.boards()` to iterate deterministically in
stage return order:

```python
@project.design.test
def controller_variants(check):
    assert check.names() == ("main-controller", "debug-adapter")
    check.design("main-controller").net("VCC").connects("J1.1", "R1.1")
    for design in check.designs():
        design.no_connection("VCC", "GND")


@project.board.test
def all_boards_have_outline(check):
    for board in check.boards():
        board.has_outline()
```

Use `project.run_through(project.design)` when iterating on a stage without building the
later projections. The stage handle is the selector, so callers do not have to use
stringly stage names.

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
        volt.PinSpec("V-", 4, role="ground"),
        volt.PinSpec(
            "V+",
            8,
            role="power",
            voltage_range=(2.7, 5.5),
        ),
    ],
)

u1 = d.instantiate(opamp, ref="U1")
vout = d.net("VOUT")
vout += u1["OUT"]
```

`PinSpec` data lowers into kernel-owned pin definitions. `ComponentDefinition` and
`Component` Python objects are handles over kernel IDs; the Python layer does not own the
component model. The `role` argument is Python authoring shorthand only. Preset names such
as `passive`, `input`/`digital_input`, `output`/`digital_output`, `analog_input`,
`analog_output`, `bidirectional`, broad `power`, directional `power_input`, `power_output`,
`ground`, and `no_connect` lower immediately into generic pin fields and are not
persisted to logical JSON. Connection requirements are `required`, `optional`, and
`must_not_connect`.

`PinSpec` also accepts a small set of fundamental electrical semantics:
`terminal`, `direction`, `signal`, `drive`, `polarity`, and `voltage_range`. These are
not Python-only metadata; they lower into kernel-owned pin definitions and logical JSON.
ERC consumes these typed semantics for power/ground checks, such as reporting a typed
power input connected to a net with no typed supply source.

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
structurally invalid mappings, including missing logical pins, unknown pins, and duplicate
physical pads. A logical pin may map to more than one physical pad when the selected package
exposes tied lands, such as a tabbed regulator package.

Selected-part manufacturer information is not only BOM metadata. It is the selected
physical implementation record that owns package, footprint, logical-pin-to-pad mapping,
and selected-part electrical ratings. `voltage_rating` and `power_rating` lower into
typed selected-part electrical attributes.

## Module Definitions

Modules are reusable logical sub-circuits. Their Python API mirrors ordinary circuit
authoring where possible: define nets, instantiate component definitions, connect pins,
then instantiate the module in a parent design.

```python
d = volt.Design("front_end")

resistor = d.define_component(
    "Resistor",
    pins=[
        volt.PinSpec("1", 1),
        volt.PinSpec("2", 2),
    ],
)

divider = d.define_module("Divider")
vin = divider.port("VIN", kind="power", role="power_input")
out = divider.port("OUT")
r1 = divider.instantiate(resistor, ref="R1")

vin += r1[1]
out += r1[2]

vbat = d.net("VBAT", kind="power", voltage=12)
sense = d.net("SENSE")

div_a = d.instantiate(divider, ref="DIV_A")
vbat += div_a["VIN"]
sense += div_a["OUT"]

inner_r1 = div_a.component("R1")
```

The module body is not Python-only structure. `define_module()`, `module.port()`,
`module.net()`, `module.instantiate()`, and module-local connections all lower into
kernel-owned hierarchy data. Instantiating the module materializes concrete components and
nets with scoped names such as `DIV_A/R1` and `DIV_A/VIN`, then records origin metadata
and explicit port bindings in logical JSON.

The first module API deliberately supports root-level module instances containing
component templates. Nested modules, PCB data, and ERC rules over hierarchy are separate
future slices.

Modules and module instances also expose read-only inspection views for projection layers
and debugging:

```python
divider.template_nets()
divider.ports()
divider.components()
divider.connections()

div_a.net_origins()
div_a.component_origins()
div_a.port_bindings()
```

These methods return small immutable data objects with kernel IDs and labels. They are
not mutation handles; edits still go through the explicit module authoring methods above.

## Schematic Placement

Schematic authoring starts from the same `Design`. A schematic sheet can place existing
logical components; it does not create components, nets, or connectivity:

```python
d = volt.Design("led")

vcc = d.net("VCC", kind="power")
led_a = d.net("LED_A")
gnd = d.net("GND", kind="ground")

r1 = d.R(resistance=330, ref="R1")
d1 = d.LED(ref="D1")

vcc += r1[1]
led_a += r1[2], d1["A"]
gnd += d1["K"]

sch = d.schematic("Main")
r_sym = sch.place(r1, at=(40, 20), symbol="resistor")
d_sym = sch.place(d1, at=(110, 30), symbol="led")

vcc_port = sch.power("VCC", net=vcc, at=r_sym.pin(1).left(20))
gnd_port = sch.ground(net=gnd, at=d_sym.pin("K").down(30))

sch.wire(vcc).from_(vcc_port).to(r_sym.pin(1)).orthogonal()
sch.wire(led_a).from_(r_sym.pin(2)).via(r_sym.pin(2).right(30)).to(d_sym.pin("A")).orthogonal()
sch.wire(gnd).from_(d_sym.pin("K")).to(gnd_port).orthogonal()

sch.label(led_a, at=r_sym.pin(2).right(8), orient="Left")
sch.junction(led_a, at=r_sym.pin(2).right(35))

schematic_json = sch.to_json()
schematic_svg = sch.to_svg()
sch.write_json("led.schematic.volt.json")
sch.write_svg("led.svg")

loaded = d.load_schematic_json(schematic_json)
```

`d.schematic(name)` creates or returns a kernel-owned sheet. `sch.place()` stores a
`SymbolInstance` over an existing `ComponentId`, with a finite `(x, y)` position and a
kernel-owned `SymbolDefinition`. Common component helpers serialize stable namespaced
default symbol references such as `volt.passives:resistor`, `volt.optos:led`, and
`volt.connectors:connector_1x02`. The legacy explicit names `resistor`, `capacitor`,
`led`, and `connector_1x02` remain accepted by `sch.place(..., symbol=...)` for older
scripts.

`sch.place(...)` returns a `SchematicSymbol` handle. `symbol.pin(key)` returns a
`SchematicPinAnchor` containing the sheet coordinate, the kernel-owned logical pin, the
pin name, number, and transformed orientation. Anchors have `.left(distance)`,
`.right(distance)`, `.up(distance)`, and `.down(distance)` helpers for nearby labels,
ports, and bends. `symbol.pin_anchor(number)` remains available when only the coordinate
tuple is needed.

`sch.wire(net)` returns a builder: start with `from_()`, add explicit intermediate
points with `via()`, append the endpoint with `to()`, then call `direct()` or
`orthogonal()`. Orthogonal routing inserts one bend only for a simple two-point diagonal
route; explicit `via()` points are preserved. Direct point authoring is still available
as `sch.wire(net, points=[...])`. Every wire stores a `WireRun` over an existing `NetId`.

`sch.label(net, at=..., orient=...)` stores a `NetLabel` over that same canonical net;
the visible text comes from the logical net name, not from a separate schematic-only
string. `sch.power()`, `sch.ground()`, `sch.junction()`, `sch.sheet_port()`,
`sch.off_page()`, and `sch.no_connect(symbol.pin("NC"), reason="...")` lower to
kernel-owned schematic objects. These helpers visualize connectivity and design intent
that already exists in the logical circuit. They do not connect pins, create nets, or
merge net names.

`Design.nets()` and `Net.pins()` expose kernel net membership for inspection and
authoring convenience. They are not alternate mutation handles; connectivity still
changes through the logical circuit APIs.

`Design.to_json()` still writes the logical circuit. `Schematic.to_json()` and
`Schematic.write_json(path)` write the `volt.schematic` document JSON. The schematic
document is owned alongside the logical circuit as a project artifact: it stores sheets,
symbols, wire runs, labels, ports, junctions, no-connect markers, and presentation
metadata that reference existing logical `ComponentId`, `PinId`, and `NetId` values.

`Design.load_schematic_json(text)` and `Design.load_schematic(path)` replace the current
schematic document after the kernel reader validates every logical reference against the
design's circuit. Stale references fail at load time; schematic objects still cannot
connect pins, create nets, or merge logical connectivity.

`Schematic.to_svg()` and `Schematic.write_svg(path)` render the same kernel-owned
document to deterministic SVG for viewing. SVG is an output artifact, not the source of
truth.

## Function Composition

Non-hierarchical reusable construction can still be ordinary Python functions that receive
a `Design` and explicit ports:

```python
def voltage_divider(d, vin, vout, gnd, top=10_000, bottom=20_000):
    r_top = d.R(resistance=top)
    r_bottom = d.R(resistance=bottom)

    vin += r_top[1]
    vout += r_top[2], r_bottom[1]
    gnd += r_bottom[2]

    return {"top": r_top, "bottom": r_bottom, "out": vout}
```

This avoids hidden global design state and keeps data flow visible when the design does
not need persisted hierarchy. Decorator syntax such as `@subcircuit` is not required for
v1. It can be considered later only if the underlying kernel-backed block and hierarchy
semantics are already clear.

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
and selected physical parts whose `voltage_rating` is below a connected net's nominal
`voltage`. Python may format diagnostics, but validation logic belongs in the kernel.

## Future Simulation Authoring

Volt should become simulation-ready without making Python the owner of simulation meaning.
Python may eventually let users author behavioral models, but those behaviors must attach
to kernel-owned model contracts with typed parameters, units, scheduling semantics,
state/result data, and validation.

SPICE should be treated as a possible future backend or export adapter, not the canonical
Python API shape. No Python simulation engine, SPICE integration, or solver API should be
added before the C++ kernel owns the model contracts.

## Projection API Boundary

Schematic and PCB Python APIs follow the same rule:

```text
Python syntax creates or edits kernel-owned projection data.
Projection data references the circuit.
Projection data does not mutate circuit connectivity.
```

This remains an ordering constraint, not only a style preference. Python schematic
helpers such as wires, labels, and renderer helpers lower into kernel-owned projection
data. Python PCB layout helpers place footprints and route copper for existing components
and nets. Neither surface should create, merge, split, or reconnect logical nets, and
future projection APIs should follow the same kernel-first boundary.
