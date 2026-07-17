# Board Structure Reference

**Sources:** `python/volt/pcb.py` (Board, Hole, CapabilityProfile classes); `examples/timer_555_led_blinker/board.py` (quoted below); `examples/pcb_led_board/main.py` (quoted below).

This reference covers the five-step board structure sequence: layers → stackup → design rules → outline → holes. Placement and routing are covered by `volt-pcb-layout`.

---

## Quick-Reference: Signature Table

| Method / class | Required keyword args | Optional keyword args |
|---|---|---|
| `design.add_board(name)` | `name: str` | — |
| `board.add_layer(name, ...)` | `role: str`, `side: str` | `thickness=0.0`, `enabled=True`, `copper_weight=None` |
| `board.set_layer_stack(layers, ...)` | `thickness: float` | `dielectrics=None` |
| `board.set_design_rules(...)` | — (all optional) | `copper_clearance`, `min_track_width`, `min_via_drill`, `min_via_annular`, `board_outline_clearance`, `package_assembly_clearance` |
| `board.set_rectangular_outline(...)` | `origin: Point`, `size: Point` | — |
| `board.set_polygon_outline(vertices)` | `vertices: Iterable[Point]` | — |
| `board.add(volt.Hole(...))` | — | `center`, `diameter`, `plated=False`, `role=""`, `label=""`, `finished_diameter=None` |
| `board.set_capability_profile(profile)` | `profile: CapabilityProfile` | — |

All lengths in millimeters. `Point` = `(x: float, y: float)`.

---

## Canonical Example: 555 LED Blinker Board Structure

The following is the board structure block from `examples/timer_555_led_blinker/board.py` (verbatim, lines 13–29). This is the authoritative, provenance-verified pattern.

```python
board = design.add_board("555 LED Blinker")
front = board.add_layer("F.Cu", role="copper", side="top")
back = board.add_layer("B.Cu", role="copper", side="bottom")
silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
board.set_layer_stack((front, back), thickness=1.6)
board.set_design_rules(
    copper_clearance=0.20,
    min_track_width=0.20,
    min_via_drill=0.30,
    min_via_annular=0.70,
    board_outline_clearance=0.25,
)
board.set_rectangular_outline(origin=(0.0, 0.0), size=(48.0, 32.0))
board.add(volt.Hole(center=(4.0, 4.0), diameter=2.2, role="mounting"))
board.add(volt.Hole(center=(44.0, 4.0), diameter=2.2, role="mounting"))
board.add(volt.Hole(center=(4.0, 28.0), diameter=2.2, role="mounting"))
board.add(volt.Hole(center=(44.0, 28.0), diameter=2.2, role="mounting"))
```

Key observations:

- `add_layer` is called once per physical layer; the return value is the kernel layer index.
- `set_layer_stack` receives a tuple of layer indices, not names. Call it **after** all `add_layer` calls.
- `set_design_rules` uses `min_via_annular` (not `min_via_annular_ring` or `via_annular`). All six keyword args are optional — omit to preserve the current kernel default. The sixth, `package_assembly_clearance`, sets the minimum package-body-to-package-body spacing checked during DRC (the 555 example above omits it).
- `set_rectangular_outline` requires `origin=` and `size=` as keyword arguments.
- `volt.Hole` `center=` is a `(float, float)` tuple. `role="mounting"` is the standard tag for mechanical mounting holes.

---

## First-Board LED Example Structure

From `examples/pcb_led_board/main.py` `build_board()` (lines 167–174):

```python
board = design.add_board("First Board LED")
front = board.add_layer("F.Cu", role="copper", side="top")
back = board.add_layer("B.Cu", role="copper", side="bottom")
board.set_layer_stack((front, back), thickness=1.6)
board.set_rectangular_outline(origin=(0.0, 0.0), size=(32.0, 18.0))
board.add(volt.Hole(center=(3.0, 3.0), diameter=2.7, role="mounting", label="MH1"))
board.add(volt.Hole(center=(29.0, 15.0), diameter=2.7, role="mounting", label="MH2"))
```

This minimal two-layer board omits `set_design_rules` (kernel defaults apply) and uses `label=` on `volt.Hole` to assign human-readable hole references (`"MH1"`, `"MH2"`).

---

## Method Details

### `design.add_board(name)`

Creates and returns a direct bound `Board` owner over kernel-owned PCB projection state.
Source: `python/volt/design.py`. The exact non-empty `name` identifies one complete physical
alternative within the Design and appears in manufacturing package manifests as
`board.name`; duplicate names are rejected. Use `design.board(name)` for exact lookup and
`design.boards()` for deterministic enumeration.

### `board.add_layer(name, *, role, side, thickness=0.0, enabled=True, copper_weight=None)`

Source: `python/volt/pcb.py` `Board.add_layer`. Returns `int` (kernel layer index). Always capture the return value.

**`role` values:**
- `"copper"` — routable copper layer
- `"silkscreen"` — component references and markings
- `"soldermask"` — solder mask openings
- `"paste"` — solder paste for SMD assembly
- `"courtyard"` — keepout boundary
- `"fabrication"` — fab notes and assembly drawings

**`side` values:** `"top"`, `"bottom"`, `"inner"`

`copper_weight` is in oz/ft² (e.g. `1.0` for 1oz). Omit to let the manufacturer apply defaults.

### `board.set_layer_stack(layers, *, thickness, dielectrics=None)`

Source: `python/volt/pcb.py` `Board.set_layer_stack`. `layers` is an `Iterable[int]` of the kernel layer indices in stack order. `thickness` is the total board thickness in mm (standard: `1.6`). `dielectrics` is an optional iterable of `(thickness_mm: float, permittivity: float)` tuples for prepreg/core layers between copper layers.

### `board.set_design_rules(...)`

Source: `python/volt/pcb.py` `Board.set_design_rules`. All arguments are keyword-only and optional. Unspecified rules preserve the current kernel value.

| Keyword arg | Meaning |
|---|---|
| `copper_clearance` | Minimum copper-to-copper clearance (mm) |
| `min_track_width` | Minimum routed track width (mm) |
| `min_via_drill` | Minimum via drill diameter (mm) |
| `min_via_annular` | Minimum via annular ring outer diameter (mm) |
| `board_outline_clearance` | Minimum copper-to-outline clearance (mm) |
| `package_assembly_clearance` | Minimum package-body-to-package-body assembly clearance (mm) |

Returns `self` for chaining.

### `board.set_rectangular_outline(*, origin, size)`

Source: `python/volt/pcb.py` `Board.set_rectangular_outline`. Both `origin` and `size` are `(x, y)` tuples in mm. `origin` is the bottom-left corner; `size` is `(width, height)`. Returns `self`.

### `board.add(volt.Hole(...))`

Source: `python/volt/pcb.py` `Hole` dataclass and `Board.add`. `volt.Hole` fields:

| Field | Type | Default | Notes |
|---|---|---|---|
| `center` | `(float, float)` | required | Board-space position in mm |
| `diameter` | `float` | required | Drill diameter in mm |
| `plated` | `bool` | `False` | PTH if `True`, NPTH if `False` |
| `role` | `str` | `""` | `"mounting"` for mechanical holes |
| `label` | `str` | `""` | Human-readable reference (e.g. `"MH1"`) |
| `finished_diameter` | `float \| None` | `None` | After-plating inner diameter |

`board.add(...)` returns the kernel primitive index (`int`).

Other primitives accepted by `board.add(...)`: `volt.Slot(start, end, width, plated=False, role="", label="")`, `volt.Cutout(outline, role="", label="")`.

### `board.set_capability_profile(profile)`

Source: `python/volt/pcb.py` `Board.set_capability_profile`. Attaches a `CapabilityProfile` to the board. Required for `write_manufacturing_package` to succeed (raises `ManufacturingPackageError` with `status="missing-board-capability-profile"` if absent).

```python
profile = volt.CapabilityProfile(
    name="JLCPCB Standard",
    source="https://jlcpcb.com/capabilities",
    as_of="2026-01-01",
    minimum_track_width=0.127,
    minimum_via_drill=0.2,
    minimum_via_annular=0.4,
    minimum_clearances=(("copper", "copper", 0.127),),
    supported_copper_layer_counts=(2, 4, 6),
    board_thickness_range=(0.8, 3.2),
    available_copper_weights=(1.0, 2.0),
    drill_diameter_range=(0.2, 6.3),
)
board.set_capability_profile(profile)
```

`CapabilityProfile` is a frozen dataclass (source: `python/volt/pcb.py`). All fields:

| Field | Type | Default |
|---|---|---|
| `name` | `str` | required |
| `source` | `str` | required |
| `as_of` | `str` | required |
| `minimum_track_width` | `float` | required |
| `minimum_via_drill` | `float` | required |
| `minimum_via_annular` | `float` | required |
| `minimum_clearances` | `tuple[tuple[str,str,float],...]` | `()` |
| `copper_weight_refinements` | `tuple[tuple[float,float,float],...]` | `()` |
| `supported_copper_layer_counts` | `tuple[int,...]` | `()` |
| `board_thickness_range` | `tuple[float,float] \| None` | `None` |
| `available_copper_weights` | `tuple[float,...]` | `()` |
| `drill_diameter_range` | `tuple[float,float] \| None` | `None` |

`CapabilityProfile.from_file(path)` loads from a standalone Volt capability profile document.

---

## Layer Index Variables

Always capture `add_layer` return values so you can reference layers by variable rather than by magic integer:

```python
front = board.add_layer("F.Cu", role="copper", side="top")   # e.g. → 0
back  = board.add_layer("B.Cu", role="copper", side="bottom") # e.g. → 1
silk  = board.add_layer("F.SilkS", role="silkscreen", side="top")

board.set_layer_stack((front, back), thickness=1.6)
board.add_text("Rev A", at=(2.0, 16.0), layer=silk, size=0.8)
```

The layer index is also needed by routing methods in `volt-pcb-layout`: `board.add_track(net, layer=front, ...)`, `board.add_via(net, start_layer=front, end_layer=back, ...)`.

---

## Text Primitives

`board.add_text(text, *, at, layer, rotation=0.0, size=1.0, locked=False)` is a convenience wrapper over `board.add(volt.Text(...))`. The `layer` argument takes a kernel layer index. From `examples/timer_555_led_blinker/board.py` line 88:

```python
board.add_text("555 SMD", at=(28.0, 29.5), layer=silk, size=1.0)
```

---

## Common Mistakes

- **Passing layer names instead of indices.** `set_layer_stack` requires the integers returned by `add_layer`, not strings like `"F.Cu"`.
- **Calling `set_layer_stack` before `add_layer`.** Add all layers first, then commit the stack.
- **Using `min_via_annular_ring` or `via_annular`.** The correct keyword is `min_via_annular`.
- **Omitting `origin=` or `size=` on `set_rectangular_outline`.** Both are keyword-only.
- **Omitting the capability profile.** The manufacturing package export will raise `ManufacturingPackageError(status="missing-board-capability-profile")`.
