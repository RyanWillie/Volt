# Code Context — IPC-D-356 Netlist Export for Volt

## Files Retrieved
1. `eda/crates/volt-cli/src/commands/export.rs` (lines 1-290) — CLI command wiring: `ExportCommands` enum, `export_command()` dispatch, `load_project_library()`, `ProjectLibrary` struct with `BoardLibrary` + `BomLibrary` impls
2. `eda/crates/volt-export/src/lib.rs` (lines 1-6) — Module declarations; currently exposes `bom`, `excellon`, `gerber`, `pick_place`
3. `eda/crates/volt-export/src/excellon.rs` (full) — Closest analog: collects pad holes, vias, NPTH holes with geometry transforms; pattern to follow
4. `eda/crates/volt-export/src/pick_place.rs` (full) — Shows circuit→board→library resolution pattern (component designator lookup)
5. `eda/crates/volt-export/src/gerber.rs` (lines 200-270) — `BoardLibrary` trait, `MapBoardLibrary`, `transform_point()`, `effective_pad_side()`
6. `eda/crates/volt-export/src/gerber.rs` (lines 425-620) — Copper layer export: iterates `board.net_segments` for traces/vias/pads, device footprint pads
7. `eda/crates/volt-core/src/project/mod.rs` (lines 57-136) — `Circuit { nets, components }`, `Net { uuid, name }`, `ComponentInstance { uuid, name, signal_connections }`
8. `eda/crates/volt-core/src/project/mod.rs` (lines 274-310) — `Board { devices, net_segments, holes }`
9. `eda/crates/volt-core/src/project/mod.rs` (lines 477-600) — `BoardDevice`, `BoardNetSegment { uuid, net: Option<Uuid>, vias, pads }`, `Via`, `BoardPad`
10. `eda/crates/volt-core/src/project/mod.rs` (lines 417-465) — `FabricationOutputSettings` with file suffix fields
11. `eda/crates/volt-core/src/library/mod.rs` (lines 260-380) — `PackagePad { uuid, name }`, `Footprint { pads }`, `FootprintPad { uuid, package_pad, side, position, width, height, holes }`, `PadHole { diameter }`, `Device { pad_mappings }`, `DevicePadMapping { pad, signal }`
12. `eda/crates/volt-cli/src/main.rs` (lines 68-95) — CLI `Commands::Export` variant → `commands::export_command(command)`
13. `eda/crates/volt-export/Cargo.toml` — description already mentions "D-356 netlist export"

## Key Code

### CLI Wiring Pattern (export.rs lines 22-107)
```rust
#[derive(Subcommand)]
pub enum ExportCommands {
    Bom { project, format, output },
    PickPlace { project, board, output },
    Gerber { project, board, output_dir },
    Drills { project, board, output_dir },
    // NEW: Netlist { project, board, output }
}

pub fn export_command(cmd: ExportCommands) -> Result<()> {
    match cmd {
        ExportCommands::Bom { .. } => export_bom(..),
        ExportCommands::PickPlace { .. } => export_pick_place(..),
        ExportCommands::Gerber { .. } => export_gerber(..),
        ExportCommands::Drills { .. } => export_drills(..),
        // NEW: ExportCommands::Netlist { .. } => export_netlist(..),
    }
}
```

### Net Name Resolution Chain
```
Board.net_segments[i].net: Option<Uuid>  →  Circuit.nets[j].uuid  →  Circuit.nets[j].name
```

### Pad-to-Net Resolution for Device Pads
```
BoardDevice.component → ComponentInstance.signal_connections[k].signal → Uuid
Device.pad_mappings[m] maps PackagePad.uuid → signal Uuid
ComponentInstance.signal_connections[n] maps signal Uuid → net: Option<Uuid>
```

### Standalone BoardPad Net Resolution
```
BoardNetSegment.pads contains BoardPad entries
BoardNetSegment.net: Option<Uuid> gives the net for ALL pads/vias/traces in that segment
```

### BoardLibrary Trait (gerber.rs line 203)
```rust
pub trait BoardLibrary {
    fn get_device(&self, uuid: &Uuid) -> Option<&Device>;
    fn get_package(&self, uuid: &Uuid) -> Option<&Package>;
}
```

### transform_point (gerber.rs line 237)
```rust
pub fn transform_point(px: f64, py: f64, device: &BoardDevice) -> (f64, f64)
// Applies flip, rotation, translation from footprint-local → board coords
```

### FabricationOutputSettings (project/mod.rs lines 417-465)
Needs a new field: `netlist_ipc_d356_suffix: String` with default like `"_NETLIST.ipc"`.

## Architecture

```
volt-cli (commands/export.rs)
  ├── reads project: Circuit + Board + Library
  ├── calls volt-export::<format>::export_*()
  └── writes output file

volt-export
  ├── bom.rs          — BOM generation (circuit + library only)
  ├── pick_place.rs   — Placement CSV (board + circuit + library)
  ├── gerber.rs       — Gerber RS-274X (board + circuit + library), provides BoardLibrary trait
  ├── excellon.rs     — Drill files (board + library), reuses BoardLibrary + transform_point
  └── [NEW] ipc_d356.rs — IPC-D-356 netlist (board + circuit + library)

volt-core
  ├── project/mod.rs  — Board, Circuit, Net, BoardNetSegment, BoardDevice, BoardPad, Via
  └── library/mod.rs  — Package, Footprint, FootprintPad, PackagePad, Device, DevicePadMapping
```

### Data Flow for D-356

For each test point record (317 record), you need:
- **Net name**: `circuit.nets.find(|n| n.uuid == seg.net).name` (max 14 chars in IPC-D-356)
- **Component ref designator**: `circuit.components.find(|c| c.uuid == board_dev.component).name`
- **Pad name**: resolved via `package.pads.find(|p| p.uuid == fp_pad.package_pad).name`
- **Mid-point (board coords)**: `transform_point(fp_pad.position, board_dev)` → (x, y) in mm → convert to mils (÷ 0.0254) or keep mm depending on units header
- **Pad size**: `fp_pad.width`, `fp_pad.height`
- **Hole diameter**: from `fp_pad.holes[0].diameter` (0 for SMD)
- **Access side**: from `effective_pad_side(fp_pad.side, board_dev.flip)` → 1=top, 2=bottom
- **Vias**: from `seg.vias[]` — position, drill, net name; treated as mid-point access (side=3 for via)
- **Standalone BoardPads**: from `seg.pads[]` — position, size, holes, net from segment

## IPC-D-356 Record Format (317 Records)

Fixed 80-column format. Key record types:

```
Column  Width  Field
1-3     3      Record type: "317" for through-hole test, "327" for SMD test
4       1      Space
5-17    14     Net name (left-justified, padded with spaces)  
18-20   3      Blank/Reserved
21-26   6      Component ref designator (left-justified)
27-30   4      Pad name (left-justified, e.g. "1", "A1")
31      1      Mid-point access: 1=top, 2=bottom, 3=via/both, blank=unknown
32-37   6      X center (±NNNNNN in 0.0001" or ten-thousandths of inch)
38-43   6      Y center (±NNNNNN same)
44-47   4      X pad size (in 0.0001")
48-51   4      Y pad size (in 0.0001")
52      1      Rotation (optional)
53-57   5      Pad/hole shape
58-62   5      Hole diameter (in 0.0001", 0 for SMD)
63      1      Plating: P=plated, U=unplated
64-66   3      Connection type
67-80   14     Reserved/comment

Special records:
P  C  A  — Units line (e.g., "P  C  A  IPC-D-356" header)
999      — End of file
```

## Minimal Implementation Plan

### 1. Add suffix field to `FabricationOutputSettings` (volt-core)
- File: `eda/crates/volt-core/src/project/mod.rs` (~line 445)
- Add: `pub netlist_d356_suffix: String` with `#[serde(default = "fab_netlist_d356")]`
- Add: `fn fab_netlist_d356() -> String { "_NETLIST.ipc".into() }`

### 2. Create `eda/crates/volt-export/src/ipc_d356.rs`
- Re-use `BoardLibrary` trait from `gerber.rs`
- Need `Circuit` for net name + component designator resolution
- Core function: `pub fn export_ipc_d356(board: &Board, circuit: &Circuit, library: &dyn BoardLibrary) -> Result<String, String>`
- Build lookup: `HashMap<Uuid, &str>` for net UUID → net name
- Build lookup: `HashMap<Uuid, &ComponentInstance>` for component UUID → instance
- Iterate `board.devices` → resolve footprint pads → emit 317/327 records
- Iterate `board.net_segments` → emit via records (317 with access=3) and standalone pad records
- Helper: `fn format_d356_coord(mm: f64) -> String` — convert mm to 0.0001" (×39370.0787, rounded to int, sign-padded to 6 chars)
- Helper: `fn format_d356_size(mm: f64) -> String` — convert mm to 0.0001" (4 chars)

### 3. Register module in `eda/crates/volt-export/src/lib.rs`
- Add: `pub mod ipc_d356;`

### 4. Add CLI variant in `eda/crates/volt-cli/src/commands/export.rs`
- Add `Netlist` variant to `ExportCommands` (project, board, output args)
- Add `ExportCommands::Netlist { .. } => export_netlist(..)` to dispatch
- Add `fn export_netlist(..)` — follows exact pattern of `export_drills`: load circuit+board+library, call `ipc_d356::export_ipc_d356()`, write to file
- Import `volt_export::ipc_d356` at top

### 5. Tests in `ipc_d356.rs`
- Follow excellon.rs test pattern: build in-memory board + library + circuit
- Verify 317 records for THT pads, 327 for SMD pads, via records
- Verify coordinate formatting, net name field width, file header/footer

## Start Here

**`eda/crates/volt-export/src/excellon.rs`** — This is the closest sibling. It demonstrates:
1. How to iterate board devices + footprint pads + holes with `BoardLibrary` (lines 153-195)
2. How to use `transform_point` from gerber.rs (line 189)
3. The collect → build → format pattern
4. The `export_all` orchestration + `DrillSummary` return type
5. Complete test patterns with `MapBoardLibrary`

Then look at **`eda/crates/volt-cli/src/commands/export.rs`** (lines 22-107) to understand the CLI wiring.
