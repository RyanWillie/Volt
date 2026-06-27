---
name: volt-circuit-authoring
description: Author or review canonical Volt logical circuits. Use when creating nets, connecting pins, instantiating component instances, setting typed electrical intent, wiring logical connectivity, building a volt.Project workflow, adding project tests, or inspecting diagnostics for a logical design.
---

# Volt Circuit Authoring

Use this skill to build canonical Volt logical circuits — nets, component instances, connectivity, typed electrical intent, a `volt.Project` workflow, and project tests. First read `../shared-volt-architecture.md`.

Cross-links:
- **Upstream** — `volt-component-authoring` (define component shapes, PinSpec, footprints, symbols, select_part)
- **Downstream** — `volt-schematic-authoring` (visualize existing connectivity), `volt-pcb-authoring` (implement connectivity on a board)

---

## Workflow

### Step 1 — Name and create the Design

```python
import volt

design = volt.Design("timer-555-led-blinker")
```

One `Design` owns one kernel logical circuit. Its name is the stable artifact identifier.

### Step 2 — Create nets

```python
nets = {
    "+5V":   design.net("+5V",  kind="power",  voltage=5.0),
    "GND":   design.net("GND",  kind="ground"),
    "OUT":   design.net("OUT"),
    "LED_A": design.net("LED_A"),
}
```

`design.net(name, *, kind="signal", voltage=None)` — `kind` is one of `"signal"`, `"power"`, `"ground"`. `voltage` is a float in volts and lowers into a kernel-owned typed electrical attribute. Both are optional; omit them for ordinary signal nets. See `references/connectivity.md` for the full net API.

### Step 3 — Instantiate components

Use catalog helpers for standard passives and generic parts, or `design.instantiate(definition, ...)` for custom-defined types (see `volt-component-authoring`):

```python
parts = {
    "J1": design.instantiate(supply_definition, ref="J1"),
    "U1": design.instantiate(timer_definition, ref="U1", properties={"value": "NE555"}),
    "RA": design.R("100 kOhm", ref="R1"),
    "RB": design.R("47 kOhm",  ref="R2"),
    "CT": design.C("1 uF",     ref="C1"),
    "RLED": design.R("1 kOhm", ref="R3"),
    "DLED": design.LED(ref="D1"),
}
```

Catalog helpers (`R`, `C`, `CP`, `L`, `LED`, `diode`, `connector_1x02`, etc.) define a reusable kernel definition lazily and return a `Component` handle. Instantiation is implicit.

### Step 4 — Connect pins with `+=`

The `+=` operator is the canonical connectivity API. It lowers to a kernel mutation that joins a pin to a net:

```python
timer = parts["U1"]

nets["+5V"] += (
    parts["J1"][1],
    timer["VCC"],
    timer["RESET"],
    parts["RA"][1],
)
nets["DISCH"] += timer["DISCH"], parts["RA"][2], parts["RB"][1]
nets["OUT"]   += timer["OUT"],   parts["RLED"][1]
nets["LED_A"] += parts["RLED"][2], parts["DLED"]["A"]
nets["GND"]   += parts["J1"][2], timer["GND"], parts["DLED"]["K"]
```

Pin access: `component[number]` for numeric pins, `component["NAME"]` for named pins. Both forms return the same kernel pin handle. See `references/connectivity.md` for all `+=` forms.

### Step 5 — Encode typed electrical intent

Assign `kind` and `voltage` to power/ground nets. For net-class routing constraints use `design.net_class(...)`. Keep design-quality findings in diagnostics and project tests — not in workarounds.

```python
vdd = design.net("VDD", kind="power", voltage=3.3)
nc  = design.net_class(current=1.5, temp_rise=10.0)
nc.assign(vdd)   # assign the net class to the net
```

### Step 6 — Wrap in a Project

Use `volt.Project` for product workflows so design, schematic, PCB, diagnostics, tests, and bundles run in a deterministic order:

```python
def build_project() -> volt.Project:
    project = volt.Project(
        "timer-555-led-blinker",
        description="555 LED blinker reference design",
    )

    @project.design
    def design():
        project_design, nets, parts = build_design()
        return (
            project_design,
            volt.ProjectResource("nets", nets),
            volt.ProjectResource("parts", parts),
        )

    project.schematic(build_schematic)
    project.board(build_board)
    register_project_tests(project)
    return project
```

The `@project.design` function returns the `Design` model plus any `ProjectResource` values. Later stages receive a `volt.BuildContext` and call `context.design()` / `context.resource("nets", dict)` to access them. See `references/project-framework.md` for the full API.

### Step 7 — Add project tests

```python
@project.design.test
def power_and_ground_are_separate(check) -> None:
    check.net("+5V").connects("J1.1", "U1.VCC", "U1.RESET")
    check.net("GND").connects("J1.2", "U1.GND", "D1.K")
    check.no_connection("+5V", "GND")

@project.schematic.test
def schematic_places_design_parts(check) -> None:
    check.places("J1", "U1", "R1", "R2", "C1", "C2", "C3", "R3", "D1")

@project.board.test
def board_places_design_parts(check) -> None:
    check.has_outline()
    check.places("J1", "U1", "R1", "R2", "C1", "C2", "C3", "R3", "D1")
```

See `references/project-tests.md` for all check methods and multi-model patterns.

### Step 8 — Run, check, write artifacts

```python
result = build_project().run()
if not result.ok:
    raise RuntimeError("validation failed")

result.write(output_path / "project.volt")
result.write_artifacts(output_path, slug="timer_555_led_blinker")
```

`result.ok` is `False` when default diagnostics have errors or any registered project test fails. `result.write(path)` writes a deterministic directory bundle. `result.write_artifacts(path, slug=...)` writes flat viewer/example files.

---

## Diagnostics vs. Exceptions

| Situation | What happens |
|---|---|
| Duplicate component reference, duplicate net name, pin already on a net, invalid selected-part pin-pad mapping | **Exception** at the mutation call site |
| Unconnected required pin, single-pin net, incompatible drivers, voltage rating below net voltage | **Diagnostic** from `design.validate()` or collected by the Project run |
| PCB-readiness gaps (no selected part, no footprint) | **Diagnostic** from `design.validate_for_pcb()` |

Diagnostics are not raised. Inspect `result.diagnostics` and `diagnostics/diagnostics.json`. Use `project.expect_diagnostic(code=...)` only when the finding is intentional and documented.

---

## Validation Checklist

- Run `build_project().run()` and require `result.ok` unless expected diagnostics are part of the contract.
- Inspect `diagnostics/diagnostics.json` and `diagnostics/tests.json` for unexpected errors, missing expected diagnostics, and failed project tests.
- Inspect logical JSON for stable component definitions, concrete components, concrete pins, nets, selected parts, typed electrical attributes, and deterministic writer output.
- For board-ready circuits, run PCB and BOM readiness validation before starting layout.
- Confirm no net is shorted to another via a project test or by inspecting `design.nets()`.

---

## References

- `references/connectivity.md` — `design.net(...)`, `+=` forms, pin access, net classes
- `references/project-framework.md` — `volt.Project`, stages, `ProjectResource`, `BuildContext`, `run`, `run_through`, `result.ok/write/write_artifacts`
- `references/project-tests.md` — `@project.*.test`, `check.net/connects/no_connection/places/has_outline`, multi-model forms
- `references/walkthrough-555-main.md` — narrated read of `examples/timer_555_led_blinker/main.py`
- `docs/python-api.md` §"Current Logical Authoring", §"Project Framework", §"Error And Diagnostic Mapping"
- `docs/logical-circuit-format.md`
- `examples/timer_555_led_blinker/` — primary worked example (components, main, project_tests)
- `examples/stm32_usb_buck/` — multi-file project example with `expect_diagnostic`, `context.resource`, and typed BuildContext
