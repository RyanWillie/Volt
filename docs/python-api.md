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
- export deterministic manufacturing packages from project results

Richer ERC, a simulation foundation, and deeper PCB flows remain planned layers. The
Python API should not introduce semantics that those future kernel layers cannot load,
validate, serialize, or inspect.

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

`Design` composes one bound logical `Circuit` owner with a separate bound
`SchematicDocument` owner over that circuit. The binding retains the Circuit for the full
SchematicDocument lifetime. PCB projection data remains on the current Circuit binding until
its separately admitted ownership slice. Future kernel layers may add constraints and reports
without moving EDA meaning into Python.

Python handles should be lightweight views over kernel-owned IDs:

```text
Design
  owns bound Circuit
  owns bound SchematicDocument (retains and borrows Circuit)

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
and instantiate concrete components through the C++ mutation API. Positional display
values are stored as kernel component properties. Natural
keyword arguments such as `resistance`, `capacitance`, `tolerance`, `voltage_rating`, and
net `voltage` lower plain numbers into typed kernel electrical attributes.

Diagnostics are inspectable Python objects created from kernel-produced diagnostic data:

```python
for diagnostic in design.validate():
    print(diagnostic.severity, diagnostic.code, diagnostic.message)
```

`Design.validate()` runs the default logical suite and exact-part Voltage/Current ERC using
the Design's retained exact part-library closure. It does not require selected parts for
logical-only designs. `Design.validate_for_pcb()` includes the same default diagnostics once,
then adds PCB-readiness checks such as missing exact selections. Footprint and pad geometry are
resolved and validated by BoardResolution and board validation rather than copied into the
logical Circuit.

`Design.validate_selected_part_erc(library)` remains the focused selected-part-only entry
point for checking a Design against one explicitly supplied native library snapshot. It does
not replace the retained closure used by `validate()` or add logical/PCB-readiness diagnostics.

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
    pcb = design.add_board("Main")
    pcb.set_rectangular_outline(origin=(0, 0), size=(32, 18))
    pcb.place(design.component("J1"), at=(5, 9), locked=True)
    pcb.place(design.component("R1"), at=(15, 7))
    pcb.place(design.component("D1"), at=(24, 7), rotation=180)
    return pcb


result = project.run()
result.write("dist/status-led.volt")
```

`Design.add_board(name)` creates one complete physical alternative over the Design's
logical Circuit. Names are exact, non-empty `BoardName` values; duplicate names are
rejected. `Design.board(name)` performs exact lookup, while `Design.board()` is valid only
when exactly one Board exists. `Design.boards()` enumerates zero, one, or many Boards in
ascending unsigned UTF-8 BoardName byte order. Each Board independently owns its outline,
stackup, rules, placements, footprints, routing, and physical digest; none of those
operations can change logical connectivity, selected parts, DNP/BOM meaning, or schematic
presentation.

The schematic and PCB stages above are intentionally short to show the framework shape.
A clean `result.ok` also requires normal projection completeness, such as schematic
visual net coverage and selected physical parts for placed PCB components.

`project.run()` executes registered stages in order and returns `ProjectResult`.
`result.ok` is false when default diagnostics have errors or stage-attached tests fail.
Later stages always receive a `volt.BuildContext`, even for the common single-design case;
use `context.design()` to reach that design and `context.resource(...)` for explicit
authoring resources.
`result.write(path)` writes a deterministic, immutable ProjectBundle v2 directory through
the native typed graph builder. Its required default graph contains every logical model and
Schematic, every named authoring Board with exactly one matching `CompiledBoard` and compact
`BoardScene`, project diagnostics and tests, and the exact reachable selected
component/part/symbol/footprint closure. `profile="viewer"` additionally admits only the
exact GLB assets consumed by each compiled Board; the default profile has no `models3d`
closure. SVG, KiCad, BOM, CPL, fabrication, STEP, and whole-board GLB copies are opt-in
exports and are not emitted by the Python path's empty default export selection. The closed
typed C++ `ExportSelection` API is the explicit opt-in surface for supported exports.

The output path must be new or an existing empty directory. A successful v2 write is
all-or-nothing, and any later write to the same non-empty destination rejects rather than
rewriting historical output. Project Board artifacts use deterministic project-design then
BoardName byte ordering. Use composite selectors such as `product:Compact` whenever more
than one Board makes an omitted or bare BoardName selector ambiguous.

### Verified Project CLI workflow

The canonical command path is source-explicit:

```sh
volt init controller
cd controller
volt check --json
volt build --output build/controller.volt --export board-svg --json
volt inspect --bundle build/controller.volt --json
volt diff build/controller.volt build/controller.volt --json
volt export --bundle build/controller.volt --output dist/controller --json
```

`check` and `build` each execute the declared `volt.toml` entrypoint exactly once in an
isolated subprocess. `build` writes and reopens one verified v2 ProjectBundle. A project
with failed diagnostics or product tests returns exit 1, but its structurally valid bundle
is still persisted with the exact diagnostics and test evidence. The result reports
structural validity, target readiness, and overall test/diagnostic outcome separately.

`inspect`, `diff`, and `export --bundle` never import project source. They reopen the
immutable bundle through the native Q3 reader and operate only on its verified typed
views. Inspection includes logical models, Schematics, every named Board, CompiledBoards,
diagnostics, tests, selected parts, BoardScenes, selected exports, dependency locks, and
artifact metadata. `diff` compares typed artifact identity and content digest; exit 0 means
identical, exit 1 means different, and exit 2 is a command or integrity failure.

Builds select exports explicitly and the empty selection emits no extras:

```sh
volt build --output build/controller.volt \
  --export schematic-svg --schematic controller:Main \
  --export board-svg --board controller:Control

volt build --output build/controller-step.volt \
  --export step --part vendor.parts@1.0:MCU-123
```

Board-backed selections require `design:Board` when more than one named Board exists.
Schematic and STEP selections likewise use the exact candidates reported by a typed
failure. Supported selections lower to the closed native export-request vocabulary:
`schematic-svg`, `board-svg`, `board-layer-image`, `bom`, `cpl`, `step`, `kicad-pcb`,
`fabrication`, and `whole-board-glb`. If the current native producer does not implement a
selected kind, build returns a typed `selected-export-failed` error; the CLI never falls
back to a Python exporter. `volt export` copies only selected-export artifacts already in
the verified bundle and refuses an existing destination.

Commands with `--json` use `volt.cli-result` schema version 1 for both success and error
responses. Exit status is stable: 0 for success, 1 for a completed check/build/diff whose
result does not pass its gate, and 2 for command, selector, source, publication, or bundle
integrity failure. Human output is deterministic and concise.

`result.write_manufacturing_package(path, board=None, manufacturing_profile=None,
archive=False)` writes the full deterministic manufacturing handoff package for one
project board. It is the single supported manufacturing-package entrypoint. The method writes
native Gerber/Excellon files, BOM, CPL, diagnostics, profile metadata, native
fabrication coverage, manifest, inspection HTML, and optional deterministic zip archive.
If the project result is not ok or native fabrication reports fab-critical loss, the
method raises `volt.ManufacturingPackageError` and does not write an orderable-looking
package. Missing or ambiguous board selectors raise `LookupError`.

```python
result = project.run()
package = result.write_manufacturing_package(
    "dist/status-led-manufacturing",
    board="Control",
    manufacturing_profile={
        "path": "profiles/generic.volt.json",
        "resolved_path": "profiles/generic.volt.json",
    },
    archive=True,
)
print(package.archive)
```

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

## Exact Library Parts

Board-ready component identity is declared once in a versioned `Library`. Instantiating an
exact `Part` selects its native `LibraryPartRef`; there is no later mutable physical-selection
step:

```python
library = volt.Library("acme.passives", version="1.0.0")
resistor = library.part(
    "RC0603FR-07330RL",
    pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
    symbol=resistor_symbol,
    manufacturer="Yageo",
    mpn="RC0603FR-07330RL",
    package="0603",
    footprint=volt.Footprint(
        ("Resistor_SMD", "R_0603_1608Metric"),
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-0.75, 0), size=(0.8, 0.9)),
            volt.FootprintPad.surface_mount("2", at=(0.75, 0), size=(0.8, 0.9)),
        ),
    ),
    pads={
        1: "1",
        2: "2",
    },
    voltage_rating=75,
    prefix="R",
)

r1 = d.instantiate(resistor, ref="R1")
```

`footprint` must be a complete `Footprint`, not only a `(library, name)` reference. `pads`
may use pin names or numbers. The complete library closure is immutable when its first Part
is instantiated, so define every member before authoring the circuit.

Logical JSON serializes only `selected_library_part`; Python does not retain a footprint
cache or alternate physical-part record. At each PCB/report boundary Volt resolves one named
Board against that exact selected closure and atomically validates the contract,
terminal-to-pad mapping, assets, digests, ownership, and capabilities.

The kernel rejects structurally invalid mappings, including missing or unknown logical pins,
duplicate physical pads, and incomplete footprint assets. A logical pin may map to more than
one physical pad when the selected package exposes tied lands, such as a tabbed regulator
package. `voltage_rating` lowers into a canonical typed `Voltage` limit in the exact Part.

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
r_sym = sch.place(r1, at=(40, 20), symbol="volt.passives:resistor")
d_sym = sch.place(d1, at=(110, 30), symbol="volt.optos:led")

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

`d.schematic(name)` creates or returns a kernel-owned sheet through the separate native
`SchematicDocument`. The bound `Circuit` exposes no schematic mutation, read, validation, or
codec forwarding methods. `sch.place()` stores a
`SymbolInstance` over an existing `ComponentId`, with a finite `(x, y)` position and a
kernel-owned `SymbolDefinition`. Common component helpers serialize stable namespaced
default symbol references such as `volt.passives:resistor`, `volt.optos:led`, and
`volt.connectors:connector_1x02`. Explicit symbol selection uses those canonical
namespaced identities.

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
