# Walkthrough: 555 Blinker main.py

**Primary source:** `examples/timer_555_led_blinker/main.py`
**Supporting files:** `examples/timer_555_led_blinker/components.py`, `examples/timer_555_led_blinker/project_tests.py`

This walkthrough narrates how the three project stages and their tests compose into a single `volt.Project` in the 555 LED blinker example. Read it alongside the source file.

---

## File Layout

```
examples/timer_555_led_blinker/
  __init__.py
  main.py              ← Project assembly + CLI entry point
  components.py        ← build_design(): Design, nets, parts, selected parts
  schematic.py         ← build_schematic(context): Schematic
  board.py             ← build_board(context): Board
  project_tests.py     ← register_project_tests(project): all @project.*.test
  artifacts/           ← written at runtime, not committed
```

The project is intentionally split by concern. `main.py` is the assembly point; it imports the builder functions and registers them without knowing their implementation details.

---

## Step 1 — `build_project()` assembles the three stages

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

**Key observations:**

1. The `@project.design` function calls `build_design()` (from `components.py`) which returns the `Design`, a `nets` dict, and a `parts` dict.
2. The function wraps the non-model objects in `volt.ProjectResource(name, value)` so later stages can retrieve them via `context.resource(...)`.
3. `build_schematic` and `build_board` are registered as plain callable arguments — both receive a `volt.BuildContext` when the stage runs.
4. `register_project_tests(project)` (from `project_tests.py`) attaches tests to the stage handles. The project handle is passed in so tests have access to `project.design`, `project.schematic`, and `project.board`.

---

## Step 2 — `_require_clean(result)` is a project-local gate

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
        raise RuntimeError("555 LED blinker validation failed: " + details)
```

This function goes beyond `result.ok`. It requires `result.diagnostics` to be completely empty (no warnings, no infos) and all tests to pass. Use this pattern in examples that must always be spotless. For projects with known/expected diagnostics, check `result.ok` and use `project.expect_diagnostic(...)` instead.

---

## Step 3 — `main()` runs, gates, then writes

```python
def main() -> None:
    output_path = Path(__file__).resolve().parent / "artifacts"
    output_path.mkdir(parents=True, exist_ok=True)

    result = run_project()
    _require_clean(result)

    result.write(output_path / f"{EXAMPLE_SLUG}.volt")
    result.write_artifacts(
        output_path,
        slug=EXAMPLE_SLUG,
        pcb_svg_options={"pad_net_overlays": False, "ratsnest_edges": False},
    )
```

**Two write calls serve different consumers:**

- `result.write(output_path / "timer_555_led_blinker.volt")` — deterministic directory bundle, consumed by CI and the Volt viewer. Contains sub-directories with logical JSON, schematic JSON/SVG, PCB JSON/SVG, BOM, CPL, diagnostics, and `manifest.volt.json`.
- `result.write_artifacts(output_path, slug=..., pcb_svg_options=...)` — flat single-file artifacts for the examples viewer and quick inspection. The `pcb_svg_options` dict passes rendering hints to the PCB SVG renderer.

The write only runs after `_require_clean` passes. If the project has any diagnostic or test failure the `RuntimeError` stops execution before any file is touched.

---

## How the Stages Connect at Runtime

When `project.run()` executes:

1. **design stage** — calls `design()` (no argument); receives `(Design, ProjectResource("nets", ...), ProjectResource("parts", ...))`. Extracts the `Design` as the canonical model; stores both resources.
2. **schematic stage** — calls `build_schematic(context)` where `context` is a `volt.BuildContext` holding the design and the two resources. `build_schematic` calls `context.design()` and `context.resource("nets", dict)` to retrieve them.
3. **board stage** — calls `build_board(context)` similarly.
4. **tests** — each stage's tests run immediately after that stage completes, before the next stage starts.

After all stages run, `ProjectResult` collects default diagnostics by running `design.validate()`, `design.validate_bom_readiness()`, schematic validation, and PCB validation automatically. No explicit validation call is needed in the stage functions.

---

## Multi-File Project Example

For a larger reference, see `examples/stm32_usb_buck/main.py`. That project:

- Registers `project.expect_diagnostic(code=..., severity=...)` calls for known schematic readability warnings.
- Uses `context.resource("stm32_board", Stm32UsbBuckBoard)` with a typed second argument to retrieve a custom board object.
- Checks `result.ok` and inspects `result.unexpected_diagnostics` and `result.missing_expected_diagnostics` before writing.
- Demonstrates that `ProjectResource` can carry any Python object — in this case a custom `Stm32UsbBuckBoard` dataclass that wraps the `Design` and domain-specific data.

The `stm32_usb_buck` project is split across `main.py`, `stm32_board.py`, `power_blocks.py`, `connection_blocks.py`, `utility_blocks.py`, `schematic_*.py`, and supporting modules. Each module receives the design or context as an explicit argument; there is no hidden global design state.
