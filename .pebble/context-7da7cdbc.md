# Code Context — Multi-Gate Support for `schematic place` (Pebble 7da7cdbc)

## Files Retrieved

1. `eda/crates/volt-cli/src/commands/schematic.rs` (lines 199–257) — `place_symbol()` function, **the hard block at line 219**
2. `eda/crates/volt-cli/src/commands/schematic.rs` (lines 505–581) — `parse_endpoint()` and `resolve_pin_position()` — both hardcode `gates.first()`
3. `eda/crates/volt-cli/src/commands/schematic.rs` (lines 377–404) — `reset_symbol_field()` — uses `sym.lib_gate` correctly
4. `eda/crates/volt-core/src/library/mod.rs` (lines 148–175) — `ComponentVariant` and `Gate` structs
5. `eda/crates/volt-core/src/project/mod.rs` (lines 128–145) — `SchematicSymbol` struct
6. `eda/crates/volt-cli/src/commands/autoplace/mod.rs` (lines 85–106) — autoplace builds symbols, hardcodes `gates.first()`
7. `eda/crates/volt-cli/src/commands/autoplace/placement.rs` (lines 23–32) — `lookup_symbol_for_instance()` hardcodes `gates.first()`
8. `eda/crates/volt-cli/src/commands/autoplace/wiring.rs` (lines 73–81) — wiring resolves gate via `sym.lib_gate` (correct)

## Key Code

### THE HARD BLOCK — `schematic.rs:219` (lines 217–220)

```rust
// Check if already placed
if schematic.symbols.iter().any(|s| s.component == comp_instance.uuid) {
    return Err(format!("Component '{}' is already placed on schematic '{}'", comp_name, sch_name).into());
}
```

This rejects placing **any** second symbol for the same `ComponentInstance`. For a 4-gate LM324, this means only gate A can ever be placed — gates B/C/D are impossible.

### Gate always hardcoded to first — `schematic.rs:233`

```rust
let gate = variant.gates.first()
    .ok_or_else(|| format!("No gates defined for component '{}'", comp_name))?;
```

No `--gate` argument exists on the `Place` command, so there's no way to select which gate.

### `ComponentVariant` and `Gate` — `library/mod.rs:148–175`

```rust
pub struct ComponentVariant {
    pub uuid: Uuid,
    pub norm: String,
    pub name: String,
    pub description: String,
    pub gates: Vec<Gate>,          // ← Multi-gate: LM324 would have 4 entries
}

pub struct Gate {
    pub uuid: Uuid,
    pub symbol: Uuid,              // ← Each gate can reference a different symbol
    pub position: Position,
    pub rotation: Angle,
    pub required: bool,
    pub suffix: String,            // ← e.g. "A", "B", "C", "D" for LM324
    pub pin_mappings: Vec<PinMapping>,  // ← Maps symbol pins → component signals
}
```

The data model **already supports** multi-gate components. `Gate.suffix` is the per-gate identifier (A/B/C/D), `Gate.uuid` is stable, and each gate maps a subset of component signals to a specific symbol's pins.

### `SchematicSymbol` — `project/mod.rs:128–145`

```rust
pub struct SchematicSymbol {
    pub uuid: Uuid,
    pub component: Uuid,     // ← FK to ComponentInstance
    pub lib_gate: Uuid,      // ← FK to Gate.uuid — ALREADY per-gate!
    pub position: Position,
    pub rotation: Angle,
    pub mirror: bool,
    pub texts: Vec<SchematicText>,
}
```

Key insight: `SchematicSymbol` already stores `lib_gate` as a specific gate UUID. Multiple `SchematicSymbol`s with the same `component` but different `lib_gate` values is the correct representation.

### `parse_endpoint()` and `resolve_pin_position()` hardcode `gates.first()` — `schematic.rs:529,572`

```rust
// In parse_endpoint():
let gate = variant.gates.first().ok_or("No gate")?;

// In resolve_pin_position():
let gate = variant.gates.first().ok_or("No gate")?;
```

These should resolve the gate from the placed symbol's `lib_gate`, not blindly take the first. For multi-gate, pin "1" on gate A is a different physical pin than pin "1" on gate B.

### Autoplace hardcodes `gates.first()` — `autoplace/mod.rs:90`

```rust
let Some(gate) = variant.gates.first() else { continue };
```

Only places one symbol per component, skipping gates B/C/D entirely.

## Architecture

### Data flow for symbol placement

```
CLI --component "U1" --grid "10,5"
  → circuit.json: find ComponentInstance(name="U1") → uuid, lib_component, lib_variant
  → library/components/<lib_component>.json: find ComponentVariant(uuid=lib_variant) → gates[]
  → gates[0].symbol → library/symbols/<symbol>.json → pin/polygon/text data
  → Build SchematicSymbol { component: inst.uuid, lib_gate: gate.uuid, ... }
  → Write to schematics/<name>.json
```

### Relationship model

```
ComponentInstance (circuit.json)
  └─ 1:N ─→ SchematicSymbol (schematic.json)  [one per gate placed]
               └── lib_gate → Gate.uuid
                              └── symbol → Symbol (different visual per gate)
                              └── pin_mappings[] → maps pin UUIDs ↔ signal UUIDs
```

The 1:N relationship is what's blocked.

## Locations That Need Change (7 sites)

| # | File | Line | What | Change needed |
|---|------|------|------|---------------|
| 1 | `schematic.rs` | 30–37 | `Place` CLI args | Add `--gate <suffix>` optional arg (e.g. "A", "B") |
| 2 | `schematic.rs` | 217–220 | **Hard block** | Replace with: reject only if *this specific gate* is already placed |
| 3 | `schematic.rs` | 233 | Gate lookup | Use `--gate` arg to find gate by suffix, default to first |
| 4 | `schematic.rs` | 529 | `parse_endpoint` | Resolve gate from `sch_sym.lib_gate` instead of `gates.first()` |
| 5 | `schematic.rs` | 572 | `resolve_pin_position` | Same: use the placed symbol's `lib_gate` to pick the right gate |
| 6 | `autoplace/mod.rs` | 85–106 | Symbol construction | Loop over all gates (or at least all required gates), not just first |
| 7 | `autoplace/placement.rs` | 23–32 | `lookup_symbol_for_instance` | Accept a gate parameter or return all gate symbols |

### Secondary sites (informational, already correct or easy):

- `schematic.rs:400` (`reset_symbol_field`) — already uses `sym.lib_gate` ✓
- `autoplace/wiring.rs:75` — already uses `sym.lib_gate` ✓
- `autoplace/tidy.rs:524` — already uses `sym.lib_gate` ✓

## Minimal Plan

### Step 1: Unblock `place_symbol` (sites 1–3)

- Add `--gate` optional arg to `Place` command (default: `None`)
- Change the duplicate check (line 219) from:
  ```rust
  schematic.symbols.iter().any(|s| s.component == comp_instance.uuid)
  ```
  to:
  ```rust
  schematic.symbols.iter().any(|s| s.component == comp_instance.uuid && s.lib_gate == gate.uuid)
  ```
- Select gate by suffix when `--gate` is provided, fall back to first unplaced gate when not

### Step 2: Fix wire endpoint resolution (sites 4–5)

- In `parse_endpoint` and `resolve_pin_position`, find the `SchematicSymbol` first, then use its `lib_gate` to look up the correct gate (not `gates.first()`). This is already the pattern used in `reset_symbol_field`.
- For multi-gate components where the user says `--from "U1:3"`, we need to determine which placed symbol owns pin 3 by checking `gate.pin_mappings`.

### Step 3: Fix autoplace (sites 6–7)

- Loop over `variant.gates` and create a `SchematicSymbol` per gate
- Each gate gets its own position (offset by gate index × spacing)
- `lookup_symbol_for_instance` needs to return all symbols or accept a gate filter

### Wire command pin ambiguity

For `"U1:3"` on a multi-gate component, pin 3 might belong to gate B. The resolution should:
1. Find all `SchematicSymbol`s for U1 on the schematic
2. For each, look up `gate.pin_mappings` to see which gate's symbol contains the requested pin
3. Return the matching `(symbol_uuid, pin_uuid)` pair

## Start Here

**`eda/crates/volt-cli/src/commands/schematic.rs` line 199** — the `place_symbol()` function. It contains the hard block (line 219), the gate selection (line 233), and is the minimal change needed to unblock multi-gate placement. The data model (`Gate.suffix`, `SchematicSymbol.lib_gate`) already supports it.
