# Manufacturing Package Reference

**Sources:** `python/volt/project.py` (`ProjectResult.write_manufacturing_package`, `ManufacturingPackageError`, `ManufacturingPackageResult`); `python/volt/manufacturing.py` (`write_project_manufacturing_package`, produced file set); `docs/python-api.md` (manufacturing package section, quoted below).

---

## Overview

`result.write_manufacturing_package(...)` writes a complete, deterministic manufacturing handoff for one project board. It is the Python entry point to the same logic that `volt export manufacturing` invokes. The output is a directory containing native Gerber/Excellon fabrication files, BOM, CPL, diagnostics, manufacturer profile metadata, a coverage report, a full manifest, and a browsable inspection HTML page. An optional deterministic zip archive can be produced alongside.

---

## Method Signature

Source: `python/volt/project.py` `ProjectResult.write_manufacturing_package` (line 930):

```python
result.write_manufacturing_package(
    path,            # str | Path — output directory
    *,
    board=None,      # str | None — board name selector
    manufacturing_profile=None,  # Mapping[str, str] | None
    archive=False,   # bool — produce deterministic zip
) -> ManufacturingPackageResult
```

**`path`** — Destination directory. Created if absent. Replaced atomically using a staging directory if it already exists. The parent directory must exist.

**`board`** — Name (or output-name) of the board to export. Required when the project result contains multiple boards; raises `LookupError` if missing or ambiguous. Omit for single-board projects.

**`manufacturing_profile`** — A mapping (dict) with at minimum:
- `"path"` (str) — relative path to the profile document
- `"resolved_path"` (str) — absolute or fully resolved path

Additional keys are preserved in `manufacturing/profile.json`. Omitting this argument raises `ManufacturingPackageError(status="missing-manufacturing-profile")`.

**`archive`** — If `True`, writes a deterministic `.zip` archive alongside the output directory (timestamps normalized to 1980-01-01, sorted entry order for reproducibility). The archive path is returned as `ManufacturingPackageResult.archive`.

---

## Canonical Call (from `docs/python-api.md`)

```python
result = project.run()
package = result.write_manufacturing_package(
    "dist/status-led-manufacturing",
    board="Control",
    manufacturing_profile={
        "path": "profiles/generic.volt.json",
        "resolved_path": "profiles/generic.volt.json",
    },
    archive=True,
)
print(package.archive)
```

---

## Return Value: ManufacturingPackageResult

Source: `python/volt/project.py` `ManufacturingPackageResult` (line 160):

| Field | Type | Notes |
|---|---|---|
| `output` | `Path` | Output directory path |
| `board` | `dict[str, str]` | Board metadata: `design`, `name`, `output_name` |
| `status` | `str` | Project result status string |
| `archive` | `Path \| None` | Zip archive path if `archive=True`, else `None` |
| `native_fabrication` | `dict[str, object]` | Coverage report payload |

---

## Produced File Set

Source: `python/volt/manufacturing.py` `_write_manufacturing_contents`. For a project result written to `dist/my-board-manufacturing/`:

```
dist/my-board-manufacturing/
  manifest.volt.json              ← project result manifest
  diagnostics/
    diagnostics.json              ← full project diagnostic report
  bom/
    *.volt.bom.json               ← BOM in Volt JSON format
    *.volt.bom.csv                ← BOM as CSV
  cpl/
    *.volt.pcb.cpl.json           ← CPL in Volt JSON format
    *.volt.pcb.cpl.csv            ← CPL as JLCPCB-shaped CSV
  manufacturing/
    fabrication/
      gerber/                     ← RS-274X Gerber files, one per copper/mask/silk/paste layer
      drill/                      ← Excellon drill files
    profile.json                  ← manufacturer profile config + board capability snapshot
    native-fabrication.json       ← coverage, loss warnings, exporter metadata
    manifest.json                 ← full manufacturing package manifest
    inspection.html               ← linked index for opening Gerbers in a viewer
  (dist/my-board-manufacturing.zip)  ← if archive=True
```

### File descriptions

**`manufacturing/fabrication/gerber/`** — RS-274X Gerber files. One file per enabled copper layer (front copper, back copper, inner layers), soldermask (front/back), silkscreen (front/back), and paste (front/back). Media type: `application/x-gerber`.

**`manufacturing/fabrication/drill/`** — Excellon drill files. Media type: `application/x-excellon`.

**`manufacturing/profile.json`** — Two-key JSON object: `config` (the `manufacturing_profile` mapping) and `board` (the board's capability profile snapshot in Volt canonical form).

**`manufacturing/native-fabrication.json`** — Native fabrication coverage report. Key fields:
- `coverage.classification`: `"complete"` or `"fab-critical-loss"`
- `coverage.fab_critical_loss`: `true` / `false`
- `warnings`: list of loss warnings with `kind`, `construct`, `message`, `severity`, `fabrication_impact`
- `exporter`: kernel exporter metadata (name, version)
- `files`: list of produced fabrication file records with `filename`, `function`, `path`, `media_type`

**`manufacturing/manifest.json`** — Full package manifest. Schema: `volt.manufacturing_package` version 1. Contains `project`, `command`, `board`, `profile`, `exporter`, `diagnostics`, `native_fabrication`, and `artifacts` sections.

**`manufacturing/inspection.html`** — Browsable HTML page listing all fabrication files with hyperlinks relative to the package root. Intended for quick smoke inspection without a dedicated Gerber viewer app.

---

## Gate: ManufacturingPackageError

Source: `python/volt/project.py` `ManufacturingPackageError` (line 170) and `python/volt/manufacturing.py` `write_project_manufacturing_package`.

The method raises `ManufacturingPackageError` (a `RuntimeError` subclass) and does not write any output when any of the following conditions are true:

| Condition | `error.status` |
|---|---|
| `result.ok` is `False` | `"<project result status>"` (e.g. `"error"`) |
| Native fabrication reports `fab_critical_loss` | `"native-fabrication-loss"` |
| `manufacturing_profile` is `None` or missing required fields | `"missing-manufacturing-profile"` |
| Board has no capability profile attached | `"missing-board-capability-profile"` |
| `board=` selector is ambiguous or not found | `LookupError` (not `ManufacturingPackageError`) |

`ManufacturingPackageError` attributes:

| Attribute | Type | Notes |
|---|---|---|
| `status` | `str` | Machine-readable failure reason |
| `output` | `Path` | The path that was not written |
| `board` | `dict \| None` | Board metadata if available |
| `diagnostics` | `dict \| None` | Project diagnostics payload |
| `native_fabrication` | `dict \| None` | Native fabrication payload if available |

Example defensive call:

```python
try:
    package = result.write_manufacturing_package(
        "dist/my-board-manufacturing",
        manufacturing_profile={"path": "p.json", "resolved_path": "/abs/p.json"},
    )
except volt.ManufacturingPackageError as error:
    print("Export blocked:", error.status, str(error))
except LookupError as error:
    print("Board selector error:", error)
```

---

## Prerequisite: Capability Profile

The board must have a capability profile attached (via `board.set_capability_profile(profile)`) before calling `write_manufacturing_package`. See `references/board-structure.md` for `CapabilityProfile` construction.

---

## CLI Equivalent

```bash
volt export manufacturing \
    --board Main \
    --profile profiles/jlcpcb.volt.json \
    --archive \
    dist/my-board-manufacturing
```

The CLI and Python API use the same `write_project_manufacturing_package` function from `python/volt/manufacturing.py`. The `--board` flag corresponds to the `board=` keyword argument; `--profile` resolves to the `manufacturing_profile` dict's `"resolved_path"` field.

---

## Native Fabrication Without the Full Package

For lower-level access to native Gerber/Excellon output without writing the full package:

```python
export = board.to_fabrication_files()
# export.files: tuple[PcbFabricationFile, ...]
# export.warnings: tuple[PcbFabricationLossWarning, ...]
for file in export.files:
    print(file.filename, file.function)

board.write_fabrication_files("dist/gerbers/")
```

`PcbFabricationFile` has `filename`, `function`, and `text` fields. `PcbFabricationLossWarning` has `kind`, `construct`, `message`, `severity`, and `fabrication_impact`. These types are defined in `python/volt/pcb.py`.

---

## KiCad Export

For KiCad PCB adapter output (separate from native fabrication):

```python
kicad_export = board.to_kicad_pcb()
# kicad_export.text: str (.kicad_pcb file content)
# kicad_export.warnings: tuple[KiCadLossWarning, ...]
board.write_kicad_pcb("dist/board.kicad_pcb")
```

Warnings with `fabrication_impact == "fab-critical"` are surfaced in the project run as `PCB_KICAD_FAB_EXPORT_LOSS` diagnostics under the `pcb.kicad_export` source (source: `docs/design/kicad-pcb-export-handoff.html`). These block `result.ok` unless whitelisted via `project.expect_diagnostic`. A clean routed board should have zero fab-critical losses.
