---
name: volt-pcb-authoring
description: Author or review the Volt PCB board structure and manufacturing handoff. Use when defining stackup, layers, design rules, board outline, mounting holes, capability profiles, or producing Gerbers/Excellon, BOM, CPL, native fabrication report, manifest, inspection HTML, and manufacturing package output. Triggers on: board structure, stackup, design rules, capability profile, manufacturing package, Gerbers, BOM, CPL, native fabrication, KiCad export, PCB_KICAD_FAB_EXPORT_LOSS, volt export manufacturing.
---

# Volt PCB Authoring — Structure and Manufacturing Handoff

First read `../shared-volt-architecture.md`.

This skill covers board structure definition and the manufacturing handoff. For placement and routing craft (placing footprints, routing copper, vias, zones, DRC), use `volt-pcb-layout`.

---

## Scope

| This skill | `volt-pcb-layout` |
|---|---|
| `design.add_board(name)` | `board.place(component, at=, ...)` |
| `add_layer`, `set_layer_stack` | `board.add_track`, `board.add_via` |
| `set_design_rules`, `set_capability_profile` | `board.layout(...)` session |
| `set_rectangular_outline`, `board.add(volt.Hole(...))` | `board.escape(...)`, `board.assisted_connect(...)` |
| `result.write_manufacturing_package(...)` | `board.add_zone(...)`, `board.add_room(...)` |
| `volt export manufacturing`, KiCad export review | `board.resolve_pads()`, routing DRC |

---

## 1. Board-Readiness Gate

Before defining board structure, confirm the logical design is PCB-ready:

```python
for diagnostic in design.validate_for_pcb():
    print(diagnostic.severity, diagnostic.code, diagnostic.message)
```

`design.validate_for_pcb()` (source: `python/volt/design.py`) adds PCB-readiness checks on top of `design.validate()`: selected physical parts, footprint geometry, and pin-pad mappings must be present for all placed components. Do not select parts or create nets in the PCB layer; consume what the logical circuit owns.

Once components are placed, board DRC also checks package geometry against the design rules and the board edge. Watch for `PCB_COMPONENT_ASSEMBLY_CLEARANCE_WARNING` (two package bodies closer than `package_assembly_clearance`) and `PCB_COMPONENT_BOARD_EDGE_CLEARANCE_VIOLATION` (a package body too close to the outline). These depend on footprints declaring `body`/outline geometry (see `volt-component-authoring`); fix them by spacing parts or pulling them off the edge in `volt-pcb-layout`, not by loosening the rule.

Run the project result check after authoring:

```python
result = project.run()
assert result.ok, [d.code for d in result.diagnostics]
```

---

## 2. Board Structure

The full structure sequence: get the board handle → add layers → commit the stack → set design rules → set outline → add holes. See `references/board-structure.md` for the cookbook with quoted source.

### 2.1 Board handle

```python
board = design.add_board("Main")
```

`design.add_board(name)` creates a complete named physical alternative and returns its
direct bound `Board` owner. Empty or duplicate names are rejected. Use
`design.board(name)` for exact lookup, `design.board()` only when exactly one Board exists,
and `design.boards()` for deterministic BoardName order. Boards over the same Design have
independent physical state; they share only the read-only logical Circuit.

### 2.2 Layers

```python
front = board.add_layer("F.Cu", role="copper", side="top")
back  = board.add_layer("B.Cu", role="copper", side="bottom")
silk  = board.add_layer("F.SilkS", role="silkscreen", side="top")
```

`board.add_layer(name, *, role, side, thickness=0.0, enabled=True, copper_weight=None)` returns a kernel layer index (`int`). Keep the returned index — you need it for `set_layer_stack` and routing.

Common `role` values: `"copper"`, `"silkscreen"`, `"soldermask"`, `"paste"`, `"courtyard"`, `"fabrication"`. Common `side` values: `"top"`, `"bottom"`, `"inner"`.

### 2.3 Stackup

```python
board.set_layer_stack((front, back), thickness=1.6)
```

`board.set_layer_stack(layers, *, thickness, dielectrics=None)` commits the stack order and board thickness in mm. Optional `dielectrics` is an iterable of `(thickness_mm, permittivity)` tuples for inner prepreg/core layers.

### 2.4 Design rules

```python
board.set_design_rules(
    copper_clearance=0.20,
    min_track_width=0.20,
    min_via_drill=0.30,
    min_via_annular=0.70,
    board_outline_clearance=0.25,
)
```

All keyword arguments are optional — unspecified rules preserve their current kernel values. All units are mm.

### 2.5 Outline

```python
board.set_rectangular_outline(origin=(0.0, 0.0), size=(32.0, 18.0))
```

`board.set_rectangular_outline(*, origin, size)` — both are `(x, y)` tuples in mm. For non-rectangular outlines use `board.set_polygon_outline(vertices)`.

### 2.6 Mounting holes

```python
board.add(volt.Hole(center=(3.0, 3.0), diameter=2.7, role="mounting", label="MH1"))
```

`volt.Hole(center, diameter, plated=False, role="", label="", finished_diameter=None)` — `center` is an `(x, y)` tuple. Pass to `board.add(...)`. `role="mounting"` is the standard value for mechanical mounting holes.

Other board primitives: `volt.Slot(start, end, width, ...)`, `volt.Cutout(outline, ...)`.

### 2.7 Capability profile

Attach a pinned manufacturer capability snapshot to the board before running manufacturing export. It is required by `write_manufacturing_package`.

```python
profile = volt.CapabilityProfile(
    name="JLCPCB Standard",
    source="https://jlcpcb.com/capabilities",
    as_of="2026-01-01",
    minimum_track_width=0.127,
    minimum_via_drill=0.2,
    minimum_via_annular=0.4,
    minimum_clearances=(("copper", "copper", 0.127),),
)
board.set_capability_profile(profile)
```

`CapabilityProfile` is a frozen dataclass in `python/volt/pcb.py`. All values in mm. `minimum_clearances` is a tuple of `(first, second, clearance_mm)` triples. `CapabilityProfile.from_file(path)` loads a standalone Volt capability profile document.

---

## 3. Manufacturing Handoff

Use `result.write_manufacturing_package(...)` or the CLI equivalent `volt export manufacturing`. Both invoke the same deterministic package writer.

### 3.1 Python API

```python
result = project.run()
package = result.write_manufacturing_package(
    "dist/my-board-manufacturing",
    board="Main",
    manufacturing_profile={
        "path": "profiles/jlcpcb.volt.json",
        "resolved_path": "profiles/jlcpcb.volt.json",
    },
    archive=True,
)
print(package.archive)  # Path to the deterministic zip, or None
```

Signature: `result.write_manufacturing_package(path, *, board=None, manufacturing_profile=None, archive=False)` (source: `python/volt/project.py`).

- `board`: optional board name selector. Required when the project has multiple boards.
- `manufacturing_profile`: a dict with at minimum `"path"` and `"resolved_path"` string keys.
- `archive`: if `True`, writes a deterministic zip alongside the output directory.

Returns `ManufacturingPackageResult` with `.output`, `.board`, `.status`, `.archive`, `.native_fabrication`.

See `references/manufacturing-package.md` for the complete file listing and error gate semantics.

### 3.2 CLI

```bash
volt export manufacturing --board Main --profile profiles/jlcpcb.volt.json --archive dist/my-board
```

### 3.3 Gate: ManufacturingPackageError

The method raises `volt.ManufacturingPackageError` (and does not write an orderable-looking package) when:

1. `result.ok` is `False` — project has diagnostic errors or failing stage tests.
2. Native fabrication reports `fab-critical` loss — copper or constraints cannot be faithfully exported.
3. `manufacturing_profile` is missing or lacks required fields.
4. The board's capability profile is missing.

### 3.4 Produced file set

```
dist/my-board-manufacturing/
  manufacturing/
    fabrication/
      gerber/       ← RS-274X Gerber files (one per copper/mask/silk layer)
      drill/        ← Excellon drill files
    profile.json    ← manufacturer profile + board capability snapshot
    native-fabrication.json  ← coverage report, loss warnings, exporter metadata
    manifest.json   ← full package manifest (format: volt.manufacturing_package)
    inspection.html ← linked index for opening Gerbers in a viewer
  bom/              ← BOM JSON + CSV (from the project result)
  cpl/              ← CPL JSON + CSV per board
  diagnostics/
    diagnostics.json
  manifest.volt.json
  (my-board-manufacturing.zip)  ← if archive=True
```

### 3.5 KiCad export and fab-critical loss

```python
kicad_export = board.to_kicad_pcb()
for warning in kicad_export.warnings:
    print(warning.kind, warning.fabrication_impact, warning.construct)
```

`board.to_kicad_pcb()` returns a `KiCadPcbExport` with `.text`, `.warnings`, `.diagnostics`. Warnings with `fabrication_impact == "fab-critical"` are also surfaced as `PCB_KICAD_FAB_EXPORT_LOSS` diagnostics in the project run (source: `docs/design/kicad-pcb-export-handoff.html`). A clean badge-class routed board should export with zero fab-critical losses. If known losses are intentionally accepted, gate them through `project.expect_diagnostic` policy.

---

## 4. Artifact Review Checklist

After producing the manufacturing package:

- `result.ok` is `True` before calling `write_manufacturing_package`.
- **View the rendered board SVG as an image** (`board.to_svg()` or the `*.pcb.svg` / per-layer SVGs from `write_artifacts`) and look: placement sane, silkscreen legible and clear of pads, board outline and mounting holes correct, no overlaps or off-board parts. Diagnostics and link-checks won't show you a cramped or unreadable board — see "Viewing Rendered Output" in `../shared-volt-architecture.md` for how to view or rasterize it.
- Open `manufacturing/inspection.html` and spot-check that Gerber links resolve.
- Review `manufacturing/native-fabrication.json` → `coverage.fab_critical_loss` must be `false`.
- Verify `manufacturing/manifest.json` has the expected board name, profile, and artifact list.
- Open one copper Gerber and one drill file in a standards-based viewer (KiCad Gerber viewer, gerbv, or equivalent).
- Review BOM CSV for correct manufacturer/part-number/quantity, and CPL CSV for correct reference/x/y/rotation/side.
- Run `board.validate()` and confirm the diagnostic report has zero errors.

---

## References

- `references/board-structure.md` — layers, stackup, design rules, outline, holes cookbook with quoted source.
- `references/manufacturing-package.md` — `write_manufacturing_package` arguments, produced files, error gate.
- `references/walkthrough-pcb-led-board.md` — narrated read of `examples/pcb_led_board/main.py`.
- `../shared-volt-architecture.md` — non-negotiables and canonical doc index.
- `docs/python-api.md` — Project Framework and manufacturing package sections.
- `docs/design/pcb-json-format.html` — PCB JSON structure, viewer diagnostics, capability lint.
- `docs/design/kicad-pcb-export-handoff.html` — KiCad adapter review and `PCB_KICAD_FAB_EXPORT_LOSS`.
- `examples/timer_555_led_blinker/board.py` — full structure + routing example.
- `examples/pcb_led_board/main.py` — first-board LED PCB end-to-end.
