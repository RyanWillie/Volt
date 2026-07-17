# Project Framework Reference

**Source:** `python/volt/project.py`, `examples/timer_555_led_blinker/main.py`

`volt.Project` is the canonical Python entry point when a design should behave like a product workflow. It keeps the common flow explicit: design first, schematic second, PCB third.

---

## Constructing a Project

```python
project = volt.Project(
    "timer-555-led-blinker",
    description="555 LED blinker reference design",
)
```

**`volt.Project(name, *, version=None, description=None)`**

| Argument | Type | Meaning |
|---|---|---|
| `name` | `str` | Stable project identifier; appears in bundle manifest |
| `version` | `str \| None` | Optional version tag |
| `description` | `str \| None` | Human-readable description |

---

## The Three Stage Decorators

### `@project.design`

Runs first. Must return a `volt.Design` plus any `volt.ProjectResource` values needed by later stages. Takes no argument.

```python
@project.design
def design():
    project_design, nets, parts = build_design()
    return (
        project_design,
        volt.ProjectResource("nets", nets),
        volt.ProjectResource("parts", parts),
    )
```

The tuple form is used here: the `Design` is the canonical model; the `ProjectResource` values pass authoring state forward without making it part of the logical circuit.

### `@project.schematic`

Runs second. Receives a `volt.BuildContext`.

```python
@project.schematic
def schematic(context: volt.BuildContext) -> volt.Schematic:
    design = context.design()
    nets = context.resource("nets", dict)
    sheet = design.schematic("Main")
    # ... place symbols, draw wires ...
    return sheet
```

### `@project.board`

Runs third. Receives a `volt.BuildContext`.

```python
@project.board
def board(context: volt.BuildContext) -> volt.Board:
    design = context.design()
    pcb = design.add_board("Main")
    # ... place footprints, route copper ...
    return pcb
```

You may also register stages as plain function calls instead of decorators:

```python
project.schematic(build_schematic)
project.board(build_board)
```

---

## `volt.ProjectResource`

```python
volt.ProjectResource("nets", nets)
volt.ProjectResource("parts", parts)
```

**`ProjectResource(name, value)`** — `name` is a non-empty string key; `value` is any Python object. Resources are not canonical Volt models. They pass authoring state (dicts, boards, custom objects) between stages without serialization.

---

## `volt.BuildContext`

Later stage functions receive a `BuildContext` as their first argument:

```python
context.design()                    # the only Design; raises if there are multiple
context.design("timer-555-led-blinker")  # select by name when there are multiple

context.resource("nets", dict)      # look up by name; second arg asserts the type
context.resource("stm32_board")     # look up by name only
```

**`context.design(name=None) -> Design`** — returns the only design, or selects by name when multiple designs were returned.

**`context.resource(name, expected_type=None) -> object`** — returns the first resource with that name. Raises `LookupError` if not found or if multiple resources share the name. Raises `TypeError` if `expected_type` is given and the value does not match.

---

## Running the Project

```python
# Run all registered stages
result = build_project().run()

# Run only through the design stage (fast iteration)
project = build_project()
result = project.run_through(project.design)
```

`project.run()` executes registered stages in order and returns a `ProjectResult`.

`project.run_through(stage)` runs stages up to and including `stage`. The stage handle (not a string) is the selector.

---

## `ProjectResult` — Properties and Methods

```python
result.ok                     # bool: no errors and no test failures
result.clean                  # bool: no diagnostics at all and no test failures
result.status                 # str: "clean", "expected-diagnostics", or "failed"

result.diagnostics            # ProjectDiagnostics iterable
result.test_failures()        # tuple[ProjectTestResult, ...]

result.designs                # tuple[Design, ...]
result.schematics             # tuple[Schematic, ...]
result.boards                 # tuple[Board, ...]
result.design(name=None)      # the only Design, or by name
```

**`result.ok` is `False` when:**
- Any collected diagnostic has `severity == "error"` AND is not covered by a `project.expect_diagnostic(...)` declaration, **or**
- Any registered project test failed.

When `project.expect_diagnostic(...)` declarations are registered, `result.ok` requires that all unexpected diagnostics are absent AND all expected diagnostics matched.

---

## Writing Artifacts

```python
# Deterministic project-result bundle (preferred for CI)
result.write(output_path / "timer_555_led_blinker.volt")

# Flat example/viewer files
result.write_artifacts(
    output_path,
    slug="timer_555_led_blinker",
    pcb_svg_options={"pad_net_overlays": False, "ratsnest_edges": False},
)
```

**`result.write(path)`** writes a directory bundle:
- `logical/<name>.volt.json` — logical circuit JSON
- `schematic/<name>.volt.schematic.json` + `.svg`
- `pcb/<name>.volt.pcb.json` + `.svg` + `.kicad_pcb`
- `bom/bom.json` + `bom.csv`
- `diagnostics/diagnostics.json` + `tests.json`
- `manifest.volt.json`

The output path may be missing, empty, or an existing Volt project-result bundle. Pre-existing non-bundle content is rejected.

**`result.write_artifacts(path, *, slug=None, pcb_svg_options=None)`** writes flat single-file artifacts, suitable for example viewers.

---

## Expected Diagnostics

When a diagnostic is intentional and documented:

```python
project.expect_diagnostic(code="SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED", severity="warning")
```

With `expect_diagnostic` declarations, `result.ok` requires all unexpected diagnostics to be absent **and** all declared expectations to have matched at least once. Use `result.unexpected_diagnostics` and `result.missing_expected_diagnostics` to debug mismatches.

---

## Verbatim `build_project` / `run_project` / `main` — 555 Blinker

From `examples/timer_555_led_blinker/main.py`:

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


def run_project() -> volt.ProjectResult:
    return build_project().run()


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

`_require_clean` (also in `main.py`) inspects `result.diagnostics` and `result.test_failures()` and raises `RuntimeError` if either is non-empty. This is a project-local guard on top of `result.ok`; use it when the project is expected to be completely clean.
