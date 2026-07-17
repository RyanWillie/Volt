---
name: volt-basics
description: Orient a fresh agent to Volt and route to the right skill. Use when getting started with Volt, choosing which Volt skill to apply, learning how to use Volt, understanding Volt project structure, or building an end-to-end Volt design for the first time.
---

# Volt Basics: Orientation and Routing

Read this first if you are new to Volt or unsure which skill to use. First read `../shared-volt-architecture.md`.

## What Is Volt

Volt is an EDA (electronics design automation) tool. The Python API is an ergonomic authoring surface over a C++ kernel that owns circuit state, schematic projection state, and PCB projection state. Python lowers every meaningful operation into kernel-owned data; it does not own EDA semantics independently.

```python
import volt
```

## Design vs Project

| | `volt.Design` | `volt.Project` |
|---|---|---|
| **Use when** | Loose scripting, experiments, reusable sub-circuits, one-file examples | Product workflows — deterministic staged builds, full artifact bundles, project tests |
| **Creates** | One logical circuit owned by the kernel | Registered stages that author `Design`, `Schematic`, and `Board` models in order |
| **Output** | `design.write(path)` for logical JSON; `sch.write_json(path)` / `sch.write_svg(path)` for schematics | `result.write(path)` for a deterministic bundle; `result.write_artifacts(path)` for flat viewer files |

**Rule of thumb:** use `volt.Project` whenever the work should produce repeatable build artifacts, run diagnostics across all stages, or record product-intent tests. Use `volt.Design` directly only for short explorations or reusable helper functions that a `Project` stage will call.

## The Three Stages

A `volt.Project` runs three registered stages in order. Each stage is a plain Python function decorated with a stage handle:

```python
project = volt.Project("my-board", version="0.1.0")

@project.design
def design():
    d = volt.Design("my-board")
    # ... create nets, components, connect pins ...
    return d

@project.schematic
def schematic(context: volt.BuildContext) -> volt.Schematic:
    d = context.design()
    sheet = d.schematic("Main")
    # ... place components, draw wires ...
    return sheet

@project.board
def board(context: volt.BuildContext) -> volt.Board:
    d = context.design()
    pcb = d.add_board("Main")
    # ... set outline, place footprints, route copper ...
    return pcb
```

**Stage order is fixed:** `@project.design` runs first, `@project.schematic` second, `@project.board` third. You may register only a subset of stages.

### Passing data between stages with ProjectResource

A stage can return a tuple of models and `volt.ProjectResource` values. Later stages retrieve those resources from the `BuildContext`:

```python
@project.design
def design():
    d = volt.Design("my-board")
    nets = {"VCC": d.net("VCC", kind="power"), "GND": d.net("GND", kind="ground")}
    return d, volt.ProjectResource("nets", nets)

@project.schematic
def schematic(context: volt.BuildContext) -> volt.Schematic:
    d = context.design()
    nets = context.resource("nets", dict)   # retrieve by name + expected type
    # ...
```

`context.design()` returns the sole design for the common single-design case. `context.resource(name, expected_type)` retrieves a named resource and type-checks it.

## Lifecycle and Artifacts

```python
result = project.run()          # executes all registered stages
assert result.ok                # False when errors exist or tests fail

# Option 1: structured bundle (recommended for products)
result.write("dist/my-board.volt")
# Writes: logical JSON, schematic JSON/SVG, PCB JSON/SVG,
#         diagnostics/diagnostics.json, diagnostics/tests.json, manifest.volt.json

# Option 2: flat files (useful for viewers and examples)
artifacts = result.write_artifacts("dist/", slug="my-board")
# artifacts.logical_json, .schematic_json, .schematic_svg, .pcb_json, .pcb_svg, ...

# Option 3: manufacturing handoff
result.write_manufacturing_package(
    "dist/my-board-manufacturing",
    board="Main",           # optional board selector
    archive=True,           # produce a deterministic zip
)
# Raises ManufacturingPackageError when result is not ok or fab-critical loss is detected
```

Use `project.run_through(project.design)` to execute only the design stage when iterating on logical authoring without the cost of schematic and board builds.

## Decision Table: Which Skill to Use

| Intent | Skill |
|---|---|
| Define a reusable component (pins, symbol, footprint, selected part) | `volt-component-authoring` |
| Author logical connectivity (nets, instances, Project stages, tests) | `volt-circuit-authoring` |
| Build or fix a schematic (placement, wires, labels, ports, sugar API) | `volt-schematic-authoring` |
| Set board outline, layers, DRC rules, manufacturing package | `volt-pcb-authoring` |
| Place and route footprints to a professional standard | `volt-pcb-layout` |

If you are doing an end-to-end build from scratch, read `volt-circuit-authoring` first, then `volt-schematic-authoring`, then `volt-pcb-authoring`. The `hello-world.md` reference in this skill shows the full flow condensed.

## Validation Posture

Volt separates structural errors from design-quality diagnostics:

- **Structural errors raise Python exceptions** immediately at the mutation site. Examples: dangling IDs, duplicate component references, a pin already connected to another net, an invalid selected-part pin-pad mapping. These cannot be suppressed.
- **Design-quality problems are diagnostics.** Examples: unconnected required pins, single-pin nets, schematic visual coverage gaps, DRC violations. Inspect them with `result.diagnostics` or in `diagnostics/diagnostics.json`.

`result.ok` is `True` when there are no error-severity diagnostics and no failed project tests. A clean `result.ok` is necessary but not sufficient for visual surfaces: always open the schematic SVG and PCB SVG to verify layout, coverage, and readability before calling the result polished.

## Quick Reference: Hello World

See `references/hello-world.md` for a minimal end-to-end LED board example derived from the real source examples. It shows the complete `volt.Project` flow — design, schematic sugar, board layout, and artifact output — in under 80 lines of annotated code.
