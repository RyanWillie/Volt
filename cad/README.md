# Volt CAD Engine

MIT-licensed Rust **agent-friendly CAD CLI** (`volt-cad`) that drives FreeCAD headlessly via `freecadcmd`.

## Requirements

- Rust toolchain (2021 edition)
- FreeCAD installed with a headless launcher on `PATH`, typically `freecadcmd` or `FreeCADCmd`
- Optional: set `VOLT_CAD_FREECAD_CMD` to the exact executable if it is not discoverable

## Build

```bash
cd cad && cargo build
```

## Run

```bash
cd cad && cargo run --bin volt-cad -- --help
```

## Capabilities (v0.1)

The CLI mirrors the EDA engine style: **structured commands** and **JSON on stdout** for automation.

- **Documents:** create, open, save `.FCStd`
- **Solids:** box, cylinder, sphere, cone
- **Transforms:** translate, rotate, uniform scale
- **Booleans:** union, cut, common
- **Import:** STEP, STL (tessellated to solid where applicable)
- **Export:** STEP, STL (mesh from solid tessellation)
- **Mesh:** tessellate a solid, export mesh STL
- **Checks:** minimum distance between solids, bounding-box intersection
- **Advanced:** `run --job job.json` merges a `CadJob` operation list after optional global document flags

Global flags (before the subcommand):

- `--document <path>` — open an existing document first
- `--new-doc-name <name>` — name for a newly created document (default `VoltCAD`)
- `--save-document <path>` — save the active document after the operation batch

See `crates/volt-cad-core` for the JSON job schema consumed by the embedded FreeCAD driver.
