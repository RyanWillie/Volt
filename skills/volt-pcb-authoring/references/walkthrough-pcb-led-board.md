# Walkthrough: pcb_led_board

**Source:** `examples/pcb_led_board/main.py`

This is a narrated read of the first-board LED PCB example. It covers the complete path from design readiness through board structure definition to project output, using the exact code from the example. For placement and routing craft, see `volt-pcb-layout`.

---

## What the Example Does

A minimal three-component LED circuit (J1 connector, R1 resistor, D1 LED) realized as a 32×18 mm two-layer PCB. The example demonstrates:

1. Building the logical design (nets, parts, selected parts, footprints)
2. Authoring the schematic
3. Defining board structure (layers, stackup, outline, holes)
4. Placing and connecting components via the layout session
5. Running the project and validating
6. Writing artifacts including KiCad export and manufacturing artifacts

---

## Step 1: Design Readiness — `build_design()`

From `examples/pcb_led_board/main.py` lines 62–108:

```python
def build_design() -> tuple[volt.Design, dict[str, volt.Net], dict[str, volt.Component]]:
    design = volt.Design("pcb-led-board")

    nets = {
        "+3V3": design.net("+3V3", kind="power", voltage=3.3),
        "LED_A": design.net("LED_A"),
        "GND": design.net("GND", kind="ground"),
    }
    parts = {
        "J1": design.connector_1x02(ref="J1"),
        "R1": design.R("330 ohm", ref="R1", resistance=330, tolerance=0.01),
        "D1": design.LED(ref="D1"),
    }

    nets["+3V3"] += parts["J1"][1], parts["R1"][1]
    nets["LED_A"] += parts["R1"][2], parts["D1"]["A"]
    nets["GND"] += parts["D1"]["K"], parts["J1"][2]

    parts["J1"].select_part(
        manufacturer="Generic",
        part_number="HDR-1x02-2.54mm",
        package="2.54mm-1x02",
        footprint=_header_1x02(),
        pin_pads={1: "1", 2: "2"},
    )
    parts["R1"].select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-07330RL",
        package="0603",
        footprint=_passive_0603(("passives", "R_0603_1608Metric")),
        pin_pads={1: "1", 2: "2"},
        ...
    )
    parts["D1"].select_part(
        manufacturer="Lite-On",
        part_number="LTST-C190KRKT",
        package="0603",
        footprint=_passive_0603(("leds", "LED_0603_1608Metric")),
        pin_pads={"A": "1", "K": "2"},
    )
    for part in parts.values():
        part.dnp(False)
    return design, nets, parts
```

**What to notice:**

- **Selected parts before board work.** All three components have `select_part(...)` called — footprint geometry, pin-pad mappings, manufacturer data. `design.validate_for_pcb()` would flag any component without a selected part.
- **`part.dnp(False)` marks all parts as fitted.** DNP state is part of BOM/CPL readiness. Unfitted parts can still appear on the board but are excluded from CPL assembly rows.
- **Nets carry electrical semantics.** `kind="power"` / `kind="ground"` lower into typed kernel pin semantics that ERC consumes.
- **Footprint definitions are Python objects.** `_header_1x02()` and `_passive_0603(...)` return `volt.FootprintDefinition` (alias for `volt.Footprint`) with `FootprintPad.through_hole` / `FootprintPad.surface_mount` pads. These definitions are owned by the kernel when `select_part` is called.

---

## Step 2: Board Structure — `build_board()`

From `examples/pcb_led_board/main.py` lines 162–174 (the structure block):

```python
def build_board(
    design: volt.Design,
    nets: dict[str, volt.Net],
    parts: dict[str, volt.Component],
) -> volt.Board:
    board = design.board("First Board LED")
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(32.0, 18.0))
    board.add(volt.Hole(center=(3.0, 3.0), diameter=2.7, role="mounting", label="MH1"))
    board.add(volt.Hole(center=(29.0, 15.0), diameter=2.7, role="mounting", label="MH2"))
```

**What to notice:**

- **No `set_design_rules` call here.** Kernel defaults apply. For production designs, always call `set_design_rules` explicitly and then verify against your manufacturer's capability profile.
- **`set_layer_stack` receives the layer index variables** `(front, back)`, not layer name strings. The tuple order defines the physical stack top-to-bottom.
- **`thickness=1.6` is the total board thickness in mm** — standard for most two-layer FR4 manufacturing.
- **`volt.Hole` with `label=`** — the label (`"MH1"`, `"MH2"`) appears in the PCB JSON and is used for mechanical referencing in the inspection HTML.
- **Hole diameter 2.7 mm** for M2.5 standoffs (add annular ring + manufacturing tolerance for finished size).

---

## Step 3: Layout Session (Structure Boundary)

From `examples/pcb_led_board/main.py` lines 175–195 (layout session — routing, belongs to `volt-pcb-layout`):

```python
    with board.layout(unit=1.0) as layout:
        header = layout.place(
            parts["J1"],
            at=board.edge("left").center().right(5.0),
            orient="right",
            locked=True,
        )
        resistor = layout.two_pad(parts["R1"]).at((15.0, 7.0)).anchor("center").right()
        led = layout.two_pad(parts["D1"]).at(resistor.center.right(9.0)).anchor("center").left()

        layout.connect(header[1], resistor[1], layer=front, width=0.25, mode="direct")
        layout.connect(
            resistor[2],
            led.A,
            layer=front,
            width=0.25,
            through=(layout.node((20.0, 3.0)),),
            mode="direct",
        )
        layout.connect(header[2], led.K, layer=front, width=0.25, mode="direct")
    return board
```

This block is shown for completeness but belongs to `volt-pcb-layout`. The `board.layout(...)` session owns placement and routing; structure (layers, outline, holes) must be committed before the layout session opens.

---

## Step 4: Project Framework and Tests

From `examples/pcb_led_board/main.py` lines 203–247:

```python
def build_project() -> volt.Project:
    project = volt.Project("pcb-led-board", description="First-board LED PCB example")

    @project.design
    def design():
        project_design, nets, parts = build_design()
        return (
            project_design,
            volt.ProjectResource("nets", nets),
            volt.ProjectResource("parts", parts),
        )

    @project.schematic
    def schematic(context: volt.BuildContext) -> volt.Schematic:
        return author_schematic(
            context.design(),
            context.resource("nets", dict),
            context.resource("parts", dict),
        )

    @project.board
    def board(context: volt.BuildContext) -> volt.Board:
        return build_board(
            context.design(),
            context.resource("nets", dict),
            context.resource("parts", dict),
        )

    @project.design.test
    def led_path_is_connected(check) -> None:
        check.net("+3V3").connects("J1.1", "R1.1")
        check.net("LED_A").connects("R1.2", "D1.A")
        check.net("GND").connects("D1.K", "J1.2")
        check.no_connection("+3V3", "GND")

    @project.schematic.test
    def schematic_places_design_parts(check) -> None:
        check.places("J1", "R1", "D1")

    @project.board.test
    def board_places_design_parts(check) -> None:
        check.has_outline()
        check.places("J1", "R1", "D1")

    return project
```

**What to notice:**

- **Stage decorators pass `context`** to schematic and board builders. `context.design()` returns the design output from the `@project.design` stage. `context.resource("nets", dict)` retrieves an explicitly passed `ProjectResource`.
- **`check.has_outline()`** asserts that the board has a defined outline — a common minimum board test.
- **`check.places("J1", "R1", "D1")`** asserts that all three components have board placements.
- **Design tests** (`led_path_is_connected`) encode product intent — these run at the logical stage before physical layout, so circuit-level bugs are caught independently of the PCB.

---

## Step 5: Run, Validate, Write

From `examples/pcb_led_board/main.py` lines 254–272:

```python
def write_artifacts(output_dir: Path | str | None = None) -> volt.ProjectArtifactPaths:
    if output_dir is None:
        output_dir = Path(__file__).resolve().parent / "artifacts"
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    result = run_project()
    _require_clean(result)

    project_bundle = output_path / f"{EXAMPLE_SLUG}.volt"
    result.write(project_bundle)
    kicad_export = result.board().to_kicad_pcb()
    if kicad_export.warnings:
        raise RuntimeError(
            "PCB LED board KiCad export reported loss: "
            + ", ".join(warning.construct for warning in kicad_export.warnings)
        )
    artifacts = result.write_artifacts(output_path, slug=EXAMPLE_SLUG)
    return artifacts
```

**What to notice:**

- **`_require_clean(result)`** (defined at lines 14–25) raises a `RuntimeError` if the result has any diagnostics or failing tests. This is the recommended pattern for enforcing a clean gate before writing artifacts.
- **`result.write(project_bundle)`** writes the full Volt project bundle (logical JSON, schematic JSON/SVG, PCB JSON/SVG, diagnostics, manifest) to `pcb_led_board.volt/`.
- **`result.board().to_kicad_pcb()`** exports the KiCad PCB adapter document. The example treats any KiCad loss warnings as a hard error — a badge-quality posture. Production designs should use `project.expect_diagnostic` for any intentionally-accepted losses.
- **`result.write_artifacts(output_path, slug=EXAMPLE_SLUG)`** writes flat example artifacts (JSON, SVG, BOM, CPL) for viewer and documentation consumption.

Note that this example does not call `write_manufacturing_package` — it uses `write_artifacts` instead, which is the flat example/viewer path. For a full manufacturing handoff with Gerbers, call `result.write_manufacturing_package(...)` as shown in `references/manufacturing-package.md`.

---

## Validation Helper Pattern

From `examples/pcb_led_board/main.py` lines 14–25:

```python
def _require_clean(result: volt.ProjectResult) -> None:
    diagnostics = [
        f"{diagnostic.source}:{diagnostic.code}"
        for diagnostic in result.diagnostics
    ]
    failures = [
        f"{failure.stage}:{failure.name}"
        for failure in result.test_failures()
    ]
    if diagnostics or failures:
        details = ", ".join((*diagnostics, *failures))
        raise RuntimeError("PCB LED board example validation failed: " + details)
```

Copy or adapt this pattern for your own examples. It surfaces both diagnostic codes and test failures in a single error string, making CI failures readable.

---

## Summary: Structure-Only Checklist for This Example

| Step | Code line(s) | What it does |
|---|---|---|
| Create design | `volt.Design("pcb-led-board")` | Kernel-owned design state |
| Define nets | `design.net("+3V3", kind="power", voltage=3.3)` | Typed power/ground nets |
| Instantiate components | `design.connector_1x02(ref="J1")` etc. | Kernel component instances |
| Select parts | `part.select_part(...)` | Footprint, manufacturer, pin-pad mappings |
| Mark fitted | `part.dnp(False)` | BOM/CPL assembly flag |
| Get board handle | `design.board("First Board LED")` | Board projection |
| Add layers | `board.add_layer("F.Cu", role="copper", side="top")` | Copper + stackup indices |
| Set stackup | `board.set_layer_stack((front, back), thickness=1.6)` | Stack order + board thickness |
| Set outline | `board.set_rectangular_outline(origin=(0.0, 0.0), size=(32.0, 18.0))` | Board boundary |
| Add holes | `board.add(volt.Hole(center=(3.0, 3.0), diameter=2.7, role="mounting", label="MH1"))` | Mechanical NPTH holes |
| Run project | `project.run()` | All stages + tests |
| Validate | `result.ok` / `_require_clean(result)` | Gate before output |
| Write bundle | `result.write(project_bundle)` | Full Volt project output |
| KiCad export | `result.board().to_kicad_pcb()` | Adapter document + loss check |
