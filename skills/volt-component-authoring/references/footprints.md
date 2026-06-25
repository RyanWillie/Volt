# Footprint Cookbook

Source: `examples/timer_555_led_blinker/components.py`

---

## Key types

| Name | Where defined | Role |
|---|---|---|
| `volt.FootprintDefinition` | `python/volt/pcb.py` (alias of `Footprint`) | Holds library-qualified ref + pad list |
| `volt.FootprintPad` | `python/volt/pcb.py` | One pad geometry entry |
| `FootprintDrill` | `python/volt/pcb.py` | Drill metadata for through-hole pads |
| `volt.FootprintMarking` | `python/volt/_footprint.py` | Semantic non-pad mark (silkscreen / polarity / pin-1) |

`FootprintDefinition` is a direct alias: `FootprintDefinition = Footprint` in `pcb.py`.

---

## Surface-mount pads

**Signature** (`python/volt/pcb.py` line 287):

```python
FootprintPad.surface_mount(
    label: str,
    *,
    at: tuple[float, float],
    size: tuple[float, float],
    shape: str = "rounded_rectangle",
    layers: str = "front_smd",
    mechanical_role: str | None = None,
) -> FootprintPad
```

All coordinates are in millimetres. `at` is the pad centre relative to the footprint origin. `label` is the pad label string that `pin_pads` values reference.

The 555 blinker example wraps `surface_mount` in a local helper to avoid repeating the layer default. Quoted verbatim from `examples/timer_555_led_blinker/components.py`:

```python
def _front_smd_pad(
    label: str,
    *,
    at: tuple[float, float],
    size: tuple[float, float],
    shape: str = "rounded_rectangle",
    mechanical_role: str | None = None,
) -> volt.FootprintPad:
    return volt.FootprintPad.surface_mount(
        label,
        at=at,
        size=size,
        shape=shape,
        mechanical_role=mechanical_role,
    )
```

This helper is a local convenience. `volt.FootprintPad.surface_mount(...)` is the real API call.

---

## Through-hole pads

**Signature** (`python/volt/pcb.py` line 309):

```python
FootprintPad.through_hole(
    label: str,
    *,
    at: tuple[float, float],
    size: tuple[float, float],
    drill: FootprintDrill,
    shape: str = "circle",
    layers: str = "through_hole",
    mechanical_role: str | None = None,
) -> FootprintPad
```

`FootprintDrill` takes `diameter: float` and `plating: str = "plated"`.

---

## FootprintDefinition constructor

```python
volt.FootprintDefinition(
    (library: str, name: str),
    pads=(...),
    courtyard=None,             # Iterable[(x, y)] | None
    body=None,                  # Iterable[(x, y)] | None
    fabrication_outline=None,   # Iterable[(x, y)] | None
    assembly_outline=None,      # Iterable[(x, y)] | None
    markings=(),                # Iterable[volt.FootprintMarking]
)
```

The first positional argument is a `(library, name)` tuple — a library-qualified KiCad footprint reference. `pads` is any iterable of `FootprintPad` objects. The remaining keyword arguments are optional non-pad geometry (all in millimetres). Omitting a geometry field means no such geometry was declared — not an empty extent.

Each outline/polygon is an iterable of `(x, y)` boundary vertices in canonical order. **Do not repeat the first vertex at the end** — the boundary is implicitly closed.

---

## Footprint outlines and markings

A footprint may carry semantic non-pad geometry beyond its pads. These feed the board's
visual bounds, clearance diagnostics (see `volt-pcb-authoring`), and the `volt.part`
artifact (format `volt.part`, version 4).

| Field | Meaning |
|---|---|
| `courtyard` | Keep-out boundary used for component-to-component spacing |
| `body` | Physical package body extent |
| `fabrication_outline` | Fab-layer outline |
| `assembly_outline` | Assembly-drawing outline |
| `markings` | Semantic marks (silkscreen / polarity / pin-1) as `volt.FootprintMarking` |

`volt.FootprintMarking` (`python/volt/_footprint.py`) is a frozen value with `kind` and
`polygon`. `kind` must be `"silkscreen"`, `"polarity"`, or `"pin_1"` — any other value
raises `ValueError`. Use the classmethods rather than the raw constructor:

```python
volt.FootprintMarking.silkscreen([(x0, y0), (x1, y1), ...])
volt.FootprintMarking.polarity([(x0, y0), (x1, y1), ...])
volt.FootprintMarking.pin_1([(x0, y0), (x1, y1), ...])
```

Example — a polarized LED footprint that declares a pin-1 mark and a body outline:

```python
volt.FootprintDefinition(
    ("LED_SMD", "LED_0805_2012Metric"),
    pads=(
        _front_smd_pad("1", at=(-0.9375, 0.0), size=(0.975, 1.4)),
        _front_smd_pad("2", at=( 0.9375, 0.0), size=(0.975, 1.4)),
    ),
    body=((-1.0, -0.7), (1.0, -0.7), (1.0, 0.7), (-1.0, 0.7)),
    markings=(
        volt.FootprintMarking.pin_1([(-1.4, -0.7), (-1.2, -0.7), (-1.2, 0.7), (-1.4, 0.7)]),
    ),
)
```

Polarity and pin-1 marks are how the board can render and check orientation, so declare
them for polarized parts (LEDs, diodes, electrolytics, connectors) when you author custom
geometry.

---

## The FOOTPRINTS dict from the 555 example

Quoted verbatim from `examples/timer_555_led_blinker/components.py`:

```python
FOOTPRINTS = {
    "jst_ph_smd_1x02": volt.FootprintDefinition(
        ("Connector_JST", "JST_PH_S2B-PH-SM4-TB_1x02-1MP_P2.00mm_Horizontal"),
        pads=(
            _front_smd_pad("1", at=(-1.0, -2.85), size=(1.0, 3.5)),
            _front_smd_pad("2", at=(1.0, -2.85), size=(1.0, 3.5)),
            _front_smd_pad(
                "MP1",
                at=(-3.35, 2.9),
                size=(1.5, 3.4),
                mechanical_role="mechanical_support",
            ),
            _front_smd_pad(
                "MP2",
                at=(3.35, 2.9),
                size=(1.5, 3.4),
                mechanical_role="mechanical_support",
            ),
        ),
    ),
    "timer_soic_8": volt.FootprintDefinition(
        ("KiCad_Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"),
        pads=(
            _front_smd_pad("1", at=(-2.475, -1.905), size=(1.95, 0.6)),
            _front_smd_pad("2", at=(-2.475, -0.635), size=(1.95, 0.6)),
            _front_smd_pad("3", at=(-2.475,  0.635), size=(1.95, 0.6)),
            _front_smd_pad("4", at=(-2.475,  1.905), size=(1.95, 0.6)),
            _front_smd_pad("5", at=( 2.475,  1.905), size=(1.95, 0.6)),
            _front_smd_pad("6", at=( 2.475,  0.635), size=(1.95, 0.6)),
            _front_smd_pad("7", at=( 2.475, -0.635), size=(1.95, 0.6)),
            _front_smd_pad("8", at=( 2.475, -1.905), size=(1.95, 0.6)),
        ),
    ),
    "resistor_0805": volt.FootprintDefinition(
        ("Resistor_SMD", "R_0805_2012Metric"),
        pads=(
            _front_smd_pad("1", at=(-0.9125, 0.0), size=(1.025, 1.4)),
            _front_smd_pad("2", at=( 0.9125, 0.0), size=(1.025, 1.4)),
        ),
    ),
    "capacitor_0805": volt.FootprintDefinition(
        ("Capacitor_SMD", "C_0805_2012Metric"),
        pads=(
            _front_smd_pad("1", at=(-0.95, 0.0), size=(1.0, 1.45)),
            _front_smd_pad("2", at=( 0.95, 0.0), size=(1.0, 1.45)),
        ),
    ),
    "led_0805": volt.FootprintDefinition(
        ("LED_SMD", "LED_0805_2012Metric"),
        pads=(
            _front_smd_pad("1", at=(-0.9375, 0.0), size=(0.975, 1.4)),
            _front_smd_pad("2", at=( 0.9375, 0.0), size=(0.975, 1.4)),
        ),
    ),
}
```

---

## Mechanical pads

Use `mechanical_role="mechanical_support"` for mounting pads that are not electrically connected to any logical pin. Do not include mechanical pads in `pin_pads` — the kernel treats pad labels absent from `pin_pads` as unconnected physical pads. The JST connector example above has `"MP1"` and `"MP2"` as mechanical support pads.

---

## Footprint reference tuple (no geometry)

When you do not need custom geometry and the part matches a known KiCad footprint exactly, pass a `(library, name)` tuple directly to `select_part(footprint=...)`:

```python
component.select_part(
    footprint=("Resistor_SMD", "R_0603_1608Metric"),
    ...
)
```

This is treated as a provenance string only — not board-ready geometry. Use a `FootprintDefinition` with explicit `pads` when the board layer needs geometry for DRC, placement, and CPL export.
