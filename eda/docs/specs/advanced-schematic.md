# Advanced Schematic Features

- **Status:** draft
- **Pebble:** `f92928d1`, `e16de244`, `5eaee7aa`
- **Owner:** `pi`
- **Last updated:** `2026-04-17`

## Summary

Three interconnected advanced schematic/board features implemented as a single schema version bump (v1→v2):

1. **Hierarchical schematics** — sheet references, hierarchical ports, power ports/flags, net scoping
2. **Bus support** — bus segments, bus entries, bus aliases, bus labels
3. **Differential pairs** — pair definitions, length measurement, DRC integration

## Implementation order

1. Data model additions (schema v2) with migration
2. Hierarchical schematic types + CLI commands
3. Bus types + CLI commands + expansion logic
4. Differential pair model + length analysis + DRC check

## Schema v2 data model changes

### `Net` — add scope

```rust
pub enum NetScope { Local, Global }

pub struct Net {
    // existing fields...
    #[serde(default = "default_net_scope")]
    pub scope: NetScope,            // Local or Global
    #[serde(default)]
    pub owner_sheet: Option<Uuid>,  // for Local scoped nets
    #[serde(default)]
    pub is_power: bool,
}
fn default_net_scope() -> NetScope { NetScope::Global } // backward compat
```

### `Schematic` — add hierarchy + bus objects

```rust
pub struct Schematic {
    // existing fields...
    #[serde(default)]
    pub sheet_refs: Vec<SheetRef>,
    #[serde(default)]
    pub hierarchical_ports: Vec<HierarchicalPort>,
    #[serde(default)]
    pub power_ports: Vec<PowerPort>,
    #[serde(default)]
    pub power_flags: Vec<PowerFlag>,
    #[serde(default)]
    pub bus_segments: Vec<BusSegment>,
    #[serde(default)]
    pub bus_entries: Vec<BusEntry>,
    #[serde(default)]
    pub bus_aliases: Vec<BusAlias>,
}
```

### `LineEndpoint` — extend for hierarchy

```rust
pub enum LineEndpoint {
    Symbol { symbol: Uuid, pin: Uuid },
    Junction { junction: Uuid },
    SheetPin { sheet_ref: Uuid, pin: Uuid },
    HierPort { port: Uuid },
}
```

### `Circuit` — add differential pairs

```rust
pub struct Circuit {
    // existing fields...
    #[serde(default)]
    pub differential_pairs: Vec<DifferentialPair>,
}
```

### New types

```rust
pub struct SheetRef {
    pub uuid: Uuid,
    pub name: String,
    pub target_schematic: String,
    pub position: Position,
    pub width: f64,
    pub height: f64,
    #[serde(default)]
    pub pins: Vec<SheetRefPin>,
}

pub struct SheetRefPin {
    pub uuid: Uuid,
    pub name: String,
    pub port_ref: Uuid,
    pub side: SheetSide,
    pub offset: f64,
    #[serde(default)]
    pub net: Option<Uuid>,
}

pub struct HierarchicalPort {
    pub uuid: Uuid,
    pub name: String,
    pub position: Position,
    pub side: SheetSide,
    pub net: Uuid,
}

pub struct PowerPort {
    pub uuid: Uuid,
    pub net: Uuid,
    pub position: Position,
    #[serde(default)]
    pub rotation: Angle,
    pub style: String,
}

pub struct PowerFlag {
    pub uuid: Uuid,
    pub net: Uuid,
    pub position: Position,
}

pub enum SheetSide { Left, Right, Top, Bottom }

pub struct BusSegment {
    pub uuid: Uuid,
    pub label: String,          // e.g. "D[0..7]"
    pub junctions: Vec<Junction>,
    pub lines: Vec<SchematicLine>,
}

pub struct BusEntry {
    pub uuid: Uuid,
    pub position: Position,
    pub bus_segment: Uuid,
    pub net: Uuid,
    pub member_name: String,    // e.g. "D[3]"
}

pub struct BusAlias {
    pub uuid: Uuid,
    pub name: String,
    pub members: Vec<String>,
}

pub struct DifferentialPair {
    pub uuid: Uuid,
    pub name: String,
    pub positive_net: Uuid,
    pub negative_net: Uuid,
    #[serde(default)]
    pub max_length_delta: Option<f64>,
    #[serde(default)]
    pub target_impedance: Option<f64>,
}
```

### `NetClass` — add diff-pair defaults

```rust
pub struct NetClass {
    // existing fields...
    #[serde(default)]
    pub diff_pair_gap: Option<f64>,
    #[serde(default)]
    pub diff_pair_max_length_delta: Option<f64>,
}
```

## Migration v1→v2

- Existing nets get `scope: Global` (preserves flat-netlist behavior)
- All new fields use `#[serde(default)]` so existing files load cleanly
- `schema_version` bumps to 2
- `CURRENT_SCHEMA_VERSION` const updates

## Provenance

| Source | License | Used for |
|---|---|---|
| Volt source | MIT | Implementation base |
| General EDA concepts | Public domain | Hierarchical schematics, buses, diff pairs are standard EDA concepts |

No GPL source consulted during implementation.
