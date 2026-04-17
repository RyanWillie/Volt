# Device assignment workflow for component instances

- **Status:** implemented
- **Pebble:** `f25933ed`
- **Owner:** `pi`
- **Last updated:** `2026-04-17`

## Summary

Volt already models physical device assignment in `ComponentInstance.device_assignments`, but the current CLI never populates it during `component add`. Board workflows currently compensate by scanning the library for the first `Device` referencing the component, which is ambiguous, not persisted, and inconsistent with ERC/BOM behavior. This spec makes device assignment explicit and validated at the circuit layer.

## Motivation

Current behavior is inconsistent and blocks the intended workflow:

- `component add` always writes `device_assignments: []`
- `create_simple_passive()` creates a `Device` but does not assign it to the new instance
- `board init` and `board place` silently fall back to library scanning when assignments are missing
- ERC W004 warns that missing assignment means the part cannot be placed on a board, but board commands may still place it
- BOM resolution uses assignments, so a board may exist without a clean assigned physical part in the circuit

The acceptance workflow for this issue is:

`library search → component add --device → board init → board place`

with no manual JSON editing.

## Goals

- Add `--device <uuid>` to `component add`
- Add `component assign-device` for post-hoc binding
- Auto-assign the created device for `--simple-passive`
- Validate that an assigned device is compatible with the component
- Make `board init` fail cleanly when components lack valid device assignments
- Make `board place` require explicit assignment when placing a component not yet on the board
- Keep the existing project schema unchanged

## Non-goals

- Full multi-assembly-variant selection throughout all commands
- Footprint selection when a package has multiple footprints
- Automatic board resync when a placed component's assignment changes
- DRC changes beyond using explicit assignment for board population
- Library database redesign

## Current state

### Relevant code paths

- `crates/volt-cli/src/commands/component.rs`
  - `Add` has no `--device`
  - no `assign-device` subcommand exists
  - `ComponentInstance.device_assignments` is always empty
  - `create_simple_passive()` returns only `(component_uuid, variant_uuid)`
- `crates/volt-cli/src/commands/board.rs`
  - `board init` uses `comp.device_assignments.first()` or falls back to `find_device_for_component()`
  - `board place` uses the same fallback for a not-yet-placed device
- `crates/volt-cli/src/commands/new.rs`
  - new projects always create a single default assembly variant named `Std`
- `crates/volt-erc/src/lib.rs`
  - W004 warns on missing `device_assignments`

### Current inconsistencies

- Circuit layer says assignment is explicit
- Board layer treats assignment as optional and guesses one
- BOM relies on assignment while board init does not
- Multiple compatible devices are currently ambiguous

## Proposed design

### Data model changes

None.

Use the existing:

- `ComponentInstance.device_assignments: Vec<DeviceAssignment>`
- `DeviceAssignment { device, variant, part }`

### CLI / API changes

#### `component add`

Add:

- `--device <uuid>` — optional for library-backed components, implicit for `--simple-passive`

Behavior:

- If `--lib-component` / `--lib-variant` is used and `--device` is provided:
  - validate the device against the library component
  - create a `DeviceAssignment` for the default assembly variant
- If `--simple-passive` is used:
  - the created device is automatically assigned to the instance
- If no device is provided for a non-simple-passive add:
  - allow the add, but the component remains unassigned and later board init/place will fail cleanly

#### `component assign-device`

Add a new subcommand:

```bash
volt-eda component assign-device \
  --project . \
  --component R1 \
  --device <uuid>
```

Behavior:

- Resolve component instance by designator name
- Resolve default assembly variant from `circuit.assembly_variants`
- Validate device compatibility
- Upsert exactly one assignment for that variant
- If `lock_assembly == true`, error
- If the component is already present on any board, error for now

### Library command improvements

Extend `library search` and/or `library info` so device UUIDs are discoverable by an agent.

Minimum viable change:

- `library search` includes `devices: [{device_uuid, package_uuid, package_name, device_name}]`

This enables `component add --device` immediately after a search result.

## Validation rules

A device assignment is valid iff:

1. the `Device` exists in `library/devices`
2. `device.component == component.lib_component`
3. the referenced `Package` exists in `library/packages`
4. the package has at least one footprint
5. every `DevicePadMapping.pad` exists in `Package.pads`
6. every `DevicePadMapping.signal` exists in the component's signals
7. every required component signal is covered by at least one non-optional `DevicePadMapping`

For now, assignment resolution uses the project's first assembly variant.

If the project has zero assembly variants, error.

## Board workflow behavior

### `board init`

`board init` must stop silently discovering devices from the library.

New behavior:

- for each circuit component, resolve its explicit assignment for the default assembly variant
- if any component lacks a valid assignment, fail the command with an actionable error listing affected designators
- do not write the new board file on failure
- when assignment is valid:
  - create `BoardDevice` using assigned `Device`
  - use the package's first footprint as before

### `board place`

If the component is already on the board:

- update position/rotation/flip/lock only

If the component is not on the board yet:

- require explicit device assignment
- do not fall back to library scanning
- create the `BoardDevice` from the assigned device/package

### Existing helper policy

`find_device_for_component()` becomes legacy behavior and should no longer drive board population for this workflow.

It may remain temporarily only if other commands still use it, but `board init` and `board place` must not silently rely on it.

## Reassignment safety

Changing the assigned device for a component that is already placed on a board is unsafe because board traces reference footprint-pad UUIDs. For this issue, the safe behavior is:

- `component assign-device` errors if the component appears on any board
- follow-up work can add a forced board-resync flow later

## Algorithm / behavior

### Resolve default assembly variant

1. Read `circuit.assembly_variants`
2. If empty, error
3. Use the first variant UUID

### Validate assignment

1. Read the target `Device`
2. Verify `device.component == comp.lib_component`
3. Read the referenced `Package`
4. Ensure the package has at least one footprint
5. Build sets of component signal UUIDs and required signal UUIDs
6. Build set of package pad UUIDs
7. For each `DevicePadMapping`:
   - mapped signal must exist in the component
   - mapped pad must exist in the package
8. Ensure all required signals are mapped by at least one non-optional mapping
9. Return a `ValidatedAssignment { device_uuid, variant_uuid }`

### Upsert assignment

1. Remove any existing assignment for the chosen variant UUID
2. Push the new `DeviceAssignment { device, variant, part: default }`

### Guard against placed-board reassignment

1. Scan `boards/*.json`
2. If any `BoardDevice.component == comp.uuid`, error

## Errors / diagnostics

Proposed error shapes:

- `Component 'R1' already exists`
- `Component 'R1' not found in circuit`
- `Project has no assembly variants`
- `Device '<uuid>' does not belong to component '<component-name>'`
- `Device '<uuid>' references package '<uuid>' with no footprints`
- `Device '<uuid>' pad mapping references unknown package pad '<uuid>'`
- `Device '<uuid>' pad mapping references unknown component signal '<uuid>'`
- `Device '<uuid>' is missing mappings for required signals: IN, OUT`
- `Component 'R1' is already placed on board 'default'; reassignment is blocked`
- `Board init failed: components missing valid device assignment: R1, U1`
- `Component 'R1' has no valid device assignment; run 'component assign-device'`

## Test vectors

### TV1 — add component with explicit device

- Input:
  - project with one default assembly variant
  - valid component/package/device in library
  - `component add --name R1 --lib-component ... --lib-variant ... --device ...`
- Expected output:
  - `circuit.json` contains one `DeviceAssignment`
  - assignment variant equals default assembly variant UUID

### TV2 — simple passive auto-assigns created device

- Input:
  - `component add --simple-passive --name R1`
- Expected output:
  - created instance has exactly one assignment
  - assigned device exists in `library/devices`

### TV3 — reject wrong device

- Input:
  - device belongs to a different library component
- Expected output:
  - add/assign command fails
  - `circuit.json` unchanged

### TV4 — reject missing required mappings

- Input:
  - component with required signal not present in `Device.pad_mappings`
- Expected output:
  - add/assign command fails with missing-signal message

### TV5 — board init fails without assignment

- Input:
  - circuit component with empty `device_assignments`
  - `board init --name fab`
- Expected output:
  - command fails
  - `boards/fab.json` does not exist

### TV6 — happy-path board init and place

- Input:
  - valid assigned component
  - `board init --name fab`
  - `board place --board fab --component R1 --x 10 --y 15`
- Expected output:
  - board contains one `BoardDevice`
  - `lib_device` equals assigned device UUID
  - placement is updated correctly

## Acceptance criteria

- [ ] `component add --device` writes a validated `DeviceAssignment`
- [ ] `component add --simple-passive` auto-assigns the created device
- [ ] `component assign-device` exists and upserts assignment for the default assembly variant
- [ ] invalid device/component or missing required mappings are rejected before write
- [ ] `board init` fails cleanly when components lack valid assignments
- [ ] `board place` requires explicit assignment when creating a new board device
- [ ] library CLI exposes device UUIDs for agent discovery
- [ ] tests cover TV1–TV6

## Provenance / sources

| Source | License | Used for | Notes |
|---|---|---|---|
| Volt source tree (`crates/volt-cli`, `volt-core`, `volt-erc`) | MIT | current-state analysis | Primary implementation source |
| LibrePCB source tree | GPL-3.0-or-later | research only | Used earlier for gap analysis, not for implementation details here |
| KiCad source tree | GPL-3.0 / GPL-2.0+ in parts | research only | Used earlier for gap analysis, not for implementation details here |

## Dependency decision

- Exact package / library: none
- License: n/a
- Why this choice is acceptable: this feature only uses existing Volt code and standard Rust library functionality

## Open questions

- Should `component add` require `--device` for all non-schematic-only library components in a future tightening pass?
- Should `library search` return all devices or only the first matching device per component?
- Should board init eventually support selecting an active assembly variant?

## Implementation notes

Implemented in:

- `crates/volt-cli/src/commands/component.rs`
- `crates/volt-cli/src/commands/board.rs`
- `crates/volt-cli/src/commands/library.rs`
- `crates/volt-cli/Cargo.toml`

Delivered behavior:

1. added `--device` to `component add`
2. added `component assign-device`
3. auto-assigned the created device for `--simple-passive`
4. added assignment validation for device/component/package/pad mappings/required signals
5. made `board init` fail on missing or invalid assignments instead of silently discovering devices
6. made `board place` require explicit assignment when creating a new board device
7. extended `library search` and `library info` to expose matching devices
8. added temp-project tests for add/assign/init/place flows

Validation run:

- `cargo test -p volt-cli`
- `cargo test`

Clean-room attestation:

> This change was implemented from this spec and Volt source only. No GPL source was consulted during implementation of this change.
