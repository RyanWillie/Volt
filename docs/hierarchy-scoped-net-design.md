# Hierarchy and Scoped Net Design

Status: design proposal for review. APIs and JSON examples are illustrative.

## Core rule

Volt should let users compose reusable subcircuits by making modules explicit kernel
objects, making ports the boundary between scopes, and making net names scoped labels over
canonical `NetId`s.

```text
NetId             internal identity, never derived from text name
NetName           user-facing label, scoped by hierarchy
Port              explicit connection point between module interior and parent scope
ModuleInstance    root-level instance scope with its own concrete local nets
```

Matching net names must not create connectivity across hierarchy. In the first slice,
connections across a module boundary happen only through a module instance port binding.

## Problem

Python functions can create repeated circuitry, but they only provide program structure.
After execution, the kernel currently sees a flat set of components, pins, and nets. That
means the kernel cannot reliably answer:

- which components belong to a reusable block;
- whether a net named `FB` is local to one buck converter or accidentally shared;
- which nets are allowed to cross a block boundary;
- where a diagnostic belongs in repeated hierarchy;
- how to round-trip reusable structure through JSON.

## First-slice non-goals

- No schematic sheets, symbol placement, wires, labels, or drawing behavior.
- No PCB model or footprint placement behavior.
- No protocol-specific buses such as SPI/I2C/USB as kernel objects.
- No generic bus/interface system yet.
- No aliases or net ties yet.
- No global-net import rules yet.
- No nested modules yet; root-level module instances only.
- No name-derived internal identity.
- No automatic net merging based on matching text names.

## Minimal vocabulary for the first slice

| Concept | Purpose | Owns / references |
| --- | --- | --- |
| `ModuleDefinition` | Reusable logical block template. | Owns template component instances, template local nets, and port definitions. |
| `ModuleInstance` | Root-level instantiation of a module definition. | References a `ModuleDefinition`; owns instance name and port bindings. Nested module instances are deferred. |
| `PortDefinition` | Declared boundary point of a module. | Name, optional direction/role, optional requirement, and exactly one internal module net. |
| `PortBinding` | Explicit connection edge between a module instance port net and a parent net. | References module instance, port definition, instance-local concrete net, and parent `NetId`. |
| Scoped local net names | Reuse names like `FB` or `SW` in repeated module instances without collision. | Names label template nets; instantiation creates concrete `NetId`s with origin metadata. |

Deferred concepts: net aliases, net ties, global-net import rules, generic
buses/interfaces, schematic paths, and PCB behavior should be separate follow-up designs.

## Recommended first-slice decisions

- **Modules are templates.** A `ModuleDefinition` describes reusable internal structure.
- **Instantiation creates concrete circuit entities.** Validation continues to run on
  concrete `ComponentId`, `PinId`, and `NetId` as it does today.
- **Concrete entities keep origin metadata.** Components, pins, and nets created by a
  module instance record their originating module definition entity and owning module
  instance.
- **Ports map to one internal module net.** A `PortDefinition` is not a pin-like second
  connectivity system; it names one internal template net as the module boundary.
- **Port bindings are explicit connectivity edges, not net merges.** The instance-local
  port net and parent net remain distinct `NetId`s. The binding is consumed by validation,
  serialization, and future projection/export code.
- **Validation traverses port-binding edges for electrical continuity.** Connectivity and
  ERC-style checks that ask whether two pins are electrically connected must treat a port
  binding as continuity between the instance-local concrete net and the parent net, while
  preserving both `NetId`s for naming, hierarchy, serialization, and diagnostics.
- **Root-level module instances only.** Nested module instances are deferred.
- **No aliases, net ties, buses, global net imports, schematic concepts, or PCB concepts
  yet.**

## Authoring shape

Current Python helpers are useful, but not persisted as hierarchy:

```python
def add_buck(name, vin, gnd, vout):
    sw = d.net(f"{name}_SW")
    fb = d.net(f"{name}_FB")
    u = d.instantiate(buck_regulator, ref=f"U_{name}")
    u.pin("VIN").connect(vin)
    u.pin("GND").connect(gnd)
    u.pin("SW").connect(sw)
    u.pin("FB").connect(fb)

add_buck("A", vin, gnd, vout_a)
add_buck("B", vin, gnd, vout_b)
```

Proposed module authoring lowers into kernel-owned hierarchy:

```python
buck = volt.Module("BuckConverter")
vin_in = buck.local_net("VIN")
vout_out = buck.local_net("VOUT")
gnd_in = buck.local_net("GND")
sw = buck.local_net("SW")
fb = buck.local_net("FB")

buck.port("VIN", net=vin_in, role="power_input")
buck.port("VOUT", net=vout_out, role="power_output")
buck.port("GND", net=gnd_in, role="ground")

u = buck.instantiate(buck_regulator, ref="U")
vin_in.connect(u.pin("VIN"))
gnd_in.connect(u.pin("GND"))
vout_out.connect(u.pin("VOUT"))
sw.connect(u.pin("SW"))
fb.connect(u.pin("FB"))

board = volt.Design("power_board")
vin = board.net("VIN", kind="power", voltage=12.0)
gnd = board.net("GND", kind="ground")

buck_a = board.instantiate(buck, name="BUCK_A")
buck_b = board.instantiate(buck, name="BUCK_B")

buck_a.port("VIN").connect(vin)
buck_a.port("GND").connect(gnd)
buck_a.port("VOUT").connect(board.net("VOUT_A", voltage=5.0))

buck_b.port("VIN").connect(vin)
buck_b.port("GND").connect(gnd)
buck_b.port("VOUT").connect(board.net("VOUT_B", voltage=3.3))
```

`/BUCK_A/FB` and `/BUCK_B/FB` are distinct concrete `NetId`s with the same local name.
They are not connected unless a future explicit mechanism ties or aliases them.

## Illustrative C++ API shape

```cpp
auto buck = circuit.hierarchy().add_module_definition(ModuleDefinition{"BuckConverter"});

auto vinInternal = circuit.hierarchy().add_template_net(buck, NetName{"VIN"});
auto voutInternal = circuit.hierarchy().add_template_net(buck, NetName{"VOUT"});
auto gndInternal = circuit.hierarchy().add_template_net(buck, NetName{"GND"});
auto sw = circuit.hierarchy().add_template_net(buck, NetName{"SW"});
auto fb = circuit.hierarchy().add_template_net(buck, NetName{"FB"});

auto vinPort = circuit.hierarchy().add_port_definition(
    buck, PortDefinition{"VIN", vinInternal, PortRole::PowerInput});
auto voutPort = circuit.hierarchy().add_port_definition(
    buck, PortDefinition{"VOUT", voutInternal, PortRole::PowerOutput});
auto gndPort = circuit.hierarchy().add_port_definition(
    buck, PortDefinition{"GND", gndInternal, PortRole::Ground});

auto reg = circuit.instantiate_template_component(
    buck, buckRegulatorDef, ReferenceDesignator{"U"});
circuit.connect(vinInternal, circuit.template_pin(reg, "VIN"));
circuit.connect(gndInternal, circuit.template_pin(reg, "GND"));
circuit.connect(voutInternal, circuit.template_pin(reg, "VOUT"));
circuit.connect(sw, circuit.template_pin(reg, "SW"));
circuit.connect(fb, circuit.template_pin(reg, "FB"));

auto buckA = circuit.instantiate_root_module(buck, InstanceName{"BUCK_A"});
auto buckB = circuit.instantiate_root_module(buck, InstanceName{"BUCK_B"});

circuit.bind_port(buckA, vinPort, vinNet);
circuit.bind_port(buckA, gndPort, gndNet);
circuit.bind_port(buckA, voutPort, voutANet);

circuit.bind_port(buckB, vinPort, vinNet);
circuit.bind_port(buckB, gndPort, gndNet);
circuit.bind_port(buckB, voutPort, voutBNet);
```

## Name resolution rules for the first slice

| Operation | Rule | Example |
| --- | --- | --- |
| Create template local net | Name must be unique within the module definition's local net namespace. | `BuckConverter.FB` is allowed once per module definition. |
| Create port | Name must be unique within the module definition's port namespace and must reference exactly one internal template net. | `port VIN -> internal net VIN`. |
| Instantiate module | Each instance gets distinct concrete nets copied from template local nets. | `/A/FB` and `/B/FB` are different `NetId`s. |
| Bind port | Records an explicit edge from the instance's concrete internal port net to a parent concrete net; does not merge `NetId`s. | `BUCK_A.VIN -> root VIN`. |
| Lookup by unqualified name | Inside a module definition, unqualified names resolve only within that definition's local namespace for this slice. | `FB` resolves to template net `BuckConverter.FB`. |

## Validation diagnostics enabled

| Diagnostic | When it fires | Severity suggestion |
| --- | --- | --- |
| `DUPLICATE_LOCAL_NET_NAME` | Two local nets with same name in one module definition. | Error at mutation/load boundary. |
| `DUPLICATE_PORT_NAME` | Two ports with same name in one module definition. | Error at mutation/load boundary. |
| `UNBOUND_REQUIRED_PORT` | A required module port is not connected in an instance. | Warning/error depending on port requirement. |
| `PORT_DIRECTION_CONFLICT` | Port role/direction conflicts with connected net/pins. | Warning or error. |
| `PORT_INTERNAL_NET_MISSING` | A port definition references a missing or non-local template net. | Error at mutation/load boundary. |
| `MODULE_ORIGIN_MISSING` | A concrete entity created by module instantiation lacks valid origin metadata. | Error at load boundary. |

## Serialization sketch

The exact format can wait for implementation, but the first format should preserve module
definitions, template local nets, port-to-internal-net mapping, root-level module
instances, port-binding edges, and origin metadata or enough data to reconstruct it.

```json
{
  "module_definitions": [
    {
      "id": "module_def_0",
      "name": "BuckConverter",
      "local_nets": [
        { "id": "net_template_0", "name": "VIN" },
        { "id": "net_template_1", "name": "VOUT" },
        { "id": "net_template_2", "name": "GND" },
        { "id": "net_template_3", "name": "SW" },
        { "id": "net_template_4", "name": "FB" }
      ],
      "ports": [
        { "id": "port_0", "name": "VIN", "role": "power_input", "internal_net": "net_template_0" },
        { "id": "port_1", "name": "VOUT", "role": "power_output", "internal_net": "net_template_1" },
        { "id": "port_2", "name": "GND", "role": "ground", "internal_net": "net_template_2" }
      ]
    }
  ],
  "module_instances": [
    {
      "id": "module_0",
      "definition": "module_def_0",
      "name": "BUCK_A",
      "parent": "root",
      "port_bindings": [
        { "port": "port_0", "internal_net": "net_10", "parent_net": "net_0" },
        { "port": "port_1", "internal_net": "net_11", "parent_net": "net_2" },
        { "port": "port_2", "internal_net": "net_12", "parent_net": "net_1" }
      ]
    }
  ]
}
```

## Open design questions after the first slice

1. **Reference designators:** should child refs be local names like `U` under `/BUCK_A`,
   displayed as `BUCK_A.U`, or allocated globally as `U12` with hierarchy metadata?
2. **Nested modules:** after root-level instances work, how should modules inside modules
   be represented and expanded?
3. **Deletion/undo:** current entity tables do not support deletion. The first slice
   should avoid needing it.
4. **Global imports:** should modules eventually be allowed to reference globals
   internally, or should all cross-boundary connectivity require ports except carefully
   designed special cases?
5. **Aliases and ties:** what are the exact semantics for additional net names and
   intentional net joins once scoped nets exist?
6. **Export/projection traversal:** which future schematic, PCB, and netlist exporters
   should collapse or preserve port-bound nets?

## Smallest implementation slice after review

Do not implement everything above at once. The first in-memory kernel slice should likely
be:

1. Add `ModuleDefinitionId`, `ModuleInstanceId`, `PortDefId`, and origin metadata for
   concrete entities.
2. Create module definition templates with unique port names and local net names.
3. Require each port to reference exactly one internal template net.
4. Instantiate modules at root with unique instance names, creating concrete components,
   pins, and nets.
5. Bind required ports by recording explicit connectivity edges between instance-local
   concrete port nets and existing root nets.
6. Report unbound required ports.

Serialization/deserialization of definitions, instances, bindings, and origin metadata is
required before hierarchy can be considered complete, but it should be a follow-up slice
after the in-memory model lands. Minimal Python syntax should wait until the C++ kernel
model and serialization are stable.

## Decision summary

- Nets stay kernel-owned IDs.
- Names are labels, scoped by hierarchy.
- Ports are the explicit boundary between scopes.
- First slice uses template definitions expanded into concrete entities with origin
  metadata.
- First slice supports root-level module instances only; nesting is deferred.
- Port bindings are explicit connectivity edges, not net merges.
- Validation traverses port-binding edges for electrical continuity while preserving both
  `NetId`s.
- Aliases and ties are explicit future design intent, not first-slice features.
- Schematic and PCB projection layers are out of scope for this hierarchy slice.
