# Project Tests Reference

**Source:** `python/volt/project_checks.py`, `examples/timer_555_led_blinker/project_tests.py`

Project tests encode product intent — the specific behaviors the design must never drift from. They are not a replacement for kernel diagnostics; they answer "does this circuit connect what it should?" rather than "is this circuit well-formed?"

---

## Registering Tests

Attach tests to any stage with the `.test` decorator:

```python
@project.design.test
def power_and_ground_are_separate(check) -> None:
    ...

@project.schematic.test
def schematic_places_design_parts(check) -> None:
    ...

@project.board.test
def board_places_design_parts(check) -> None:
    ...
```

Each decorated function receives a `check` object matching the stage. Test failures appear in `result.test_failures()` and `diagnostics/tests.json`. A failed test makes `result.ok` `False`.

---

## Design Stage: `check` Methods

### `check.net(name).connects(*pins)`

Assert that a named net connects every requested component pin. Pins are identified as `"REFDES.pin_name"` or `"REFDES.pin_number"` (both forms are accepted for the same pin).

```python
check.net("+5V").connects("J1.1", "U1.VCC", "U1.RESET")
check.net("GND").connects("J1.2", "U1.GND", "D1.K")
```

Raises `AssertionError` if any listed pin is missing from the net, or if the net does not exist.

### `check.no_connection(first, second)`

Assert that two nets share no connected pin labels (i.e., they are not shorted).

```python
check.no_connection("+5V", "GND")
```

### `check.ok()`

Smoke-test that only needs the stage to run successfully. (The method accepts an optional positional string; there is no `message=` keyword — call it as `check.ok()` or `check.ok("note")`.)

```python
@project.design.test
def stage_completes(check) -> None:
    check.ok()
```

---

## Schematic Stage: `check` Methods

### `check.places(*references)`

Assert that the schematic places every listed component reference designator.

```python
@project.schematic.test
def schematic_places_design_parts(check) -> None:
    check.places("J1", "U1", "R1", "R2", "C1", "C2", "C3", "R3", "D1")
```

Raises `AssertionError` if any reference is absent from the schematic's placed symbols.

---

## Board Stage: `check` Methods

### `check.has_outline()`

Assert the board has a non-empty mechanical outline.

```python
check.has_outline()
```

### `check.places(*references)`

Assert the board places every listed component reference designator.

```python
check.places("J1", "U1", "R1", "R2", "C1", "C2", "C3", "R3", "D1")
```

---

## Verbatim Test File — 555 Blinker

From `examples/timer_555_led_blinker/project_tests.py`:

```python
def register_project_tests(project: volt.Project) -> None:
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

The tests are registered in a helper function (`register_project_tests`) and called from `build_project()`. This keeps test definitions out of `main.py` and co-located with the components file for readability.

---

## Multi-Model Stage Tests

When a stage returns multiple models, `check` is a multi-model helper. Use `check.names()` to assert counts/order, `check.design("name")` to target one model, and `check.designs()` to iterate.

```python
@project.design.test
def all_variants_present(check) -> None:
    assert check.names() == ("main-controller", "debug-adapter")
    check.design("main-controller").net("VCC").connects("J1.1")
    for design_check in check.designs():
        design_check.no_connection("VCC", "GND")

@project.board.test
def all_boards_have_outline(check) -> None:
    for board_check in check.boards():
        board_check.has_outline()
```

Equivalent multi-model methods:

| Stage | Single-model | Multi-model accessor |
|---|---|---|
| design | `check.net/no_connection/ok` | `check.designs()`, `check.design(name)`, `check.names()` |
| schematic | `check.places/ok` | `check.schematics()`, `check.schematic(name)`, `check.names()` |
| board | `check.places/has_outline/ok` | `check.boards()`, `check.board(name)`, `check.names()` |

---

## Inspecting Results

```python
result = project.run()

# All failed tests in stage order
for failure in result.test_failures():
    print(failure.stage, failure.name, failure.message)

# Check diagnostics/tests.json written by result.write(...)
# or result.write_artifacts(...)
```

`result.test_failures()` returns `tuple[ProjectTestResult, ...]`. Each `ProjectTestResult` has `.stage`, `.name`, `.ok`, and `.message` fields.
