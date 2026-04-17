# Code Context

## Files Retrieved
1. `eda/crates/volt-cli/src/commands/autoplace/analysis.rs` (lines 1-220; focus 117-159) - `PinInfo` definition and `PinInfo` construction in `classify_one` where stray schema fields were inserted.
2. `eda/crates/volt-cli/src/commands/export.rs` (lines 540-628; focus 574-595) - failing test `gerber_export_refills_planes_before_writing` with two outdated `Net` literals.
3. `eda/crates/volt-core/src/split.rs` (lines 420-635; focus 432-455, 474-497, 522-631) - schematic test constructors, the failing `Board` test constructor, and the nearby correct `Board` constructor for comparison.
4. `eda/crates/volt-core/src/project/mod.rs` (lines 67-139) - authoritative definitions of `Circuit`, `NetClass`, `Net`, and `NetScope` after the schema upgrade.
5. `eda/crates/volt-core/src/project/mod.rs` (lines 206-303) - authoritative definitions of `Schematic` and `LineEndpoint` after the schema upgrade.
6. `eda/crates/volt-cli/src/commands/net.rs` (lines 74-83) - canonical current `Net` constructor showing the right new fields.
7. `eda/crates/volt-core/tests/roundtrip.rs` (lines 394-406) - follow-up test coverage note: `LineEndpoint` roundtrip test still only exercises the old two variants.

## Key Code

### Compiler error sites from `cargo test 2>&1`
Error-related ` --> ` lines in the current run:

- `crates/volt-cli/src/commands/autoplace/analysis.rs:124:19`
- `crates/volt-cli/src/commands/autoplace/analysis.rs:122:16`
- `crates/volt-cli/src/commands/autoplace/analysis.rs:123:22`
- `crates/volt-cli/src/commands/autoplace/analysis.rs:156:16`
- `crates/volt-cli/src/commands/export.rs:582:13`
- `crates/volt-cli/src/commands/export.rs:588:13`
- `crates/volt-core/src/split.rs:563:9`
- `crates/volt-core/src/split.rs:564:9`
- `crates/volt-core/src/split.rs:565:9`
- `crates/volt-core/src/split.rs:566:9`
- `crates/volt-core/src/split.rs:567:9`
- `crates/volt-core/src/split.rs:568:9`
- `crates/volt-core/src/split.rs:569:9`

Other ` --> ` lines in the log were warnings, not compile errors.

### Authoritative schema after the upgrade
`eda/crates/volt-core/src/project/mod.rs`:

```rust
pub struct Circuit {
    pub assembly_variants: Vec<AssemblyVariant>,
    pub net_classes: Vec<NetClass>,
    pub nets: Vec<Net>,
    pub components: Vec<ComponentInstance>,
    pub differential_pairs: Vec<DifferentialPair>,
}

pub struct NetClass {
    pub uuid: Uuid,
    pub name: String,
    pub default_trace_width: TraceWidthConfig,
    pub default_via_drill_diameter: TraceWidthConfig,
    pub min_copper_copper_clearance: f64,
    pub min_copper_width: f64,
    pub min_via_drill_diameter: f64,
    pub diff_pair_gap: Option<f64>,
    pub diff_pair_max_length_delta: Option<f64>,
}

pub struct Net {
    pub uuid: Uuid,
    pub name: String,
    pub auto_name: bool,
    pub net_class: Uuid,
    pub scope: NetScope,
    pub owner_sheet: Option<Uuid>,
    pub is_power: bool,
}
```

And for schematic data:

```rust
pub struct Schematic {
    pub uuid: Uuid,
    pub name: String,
    pub grid: Grid,
    pub symbols: Vec<SchematicSymbol>,
    pub net_segments: Vec<SchematicNetSegment>,
    pub sheet_refs: Vec<SheetRef>,
    pub hierarchical_ports: Vec<HierarchicalPort>,
    pub power_ports: Vec<PowerPort>,
    pub power_flags: Vec<PowerFlag>,
    pub bus_segments: Vec<BusSegment>,
    pub bus_entries: Vec<BusEntry>,
    pub bus_aliases: Vec<BusAlias>,
}

pub enum LineEndpoint {
    Symbol { symbol: Uuid, pin: Uuid },
    Junction { junction: Uuid },
    SheetPin { sheet_ref: Uuid, pin: Uuid },
    HierPort { port: Uuid },
}
```

### File-by-file exact fixes

#### 1. `eda/crates/volt-cli/src/commands/autoplace/analysis.rs`
Problem area:

```rust
struct PinInfo {
    _signal: Uuid,
    role: SignalRole,
    net: Uuid,
    net_class: NetClass,
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: false,
}
```

and later:

```rust
Some(PinInfo {
    _signal: sc.signal,
    role,
    net: nid,
    net_class: nc,
    scope: NetScope::Global,
    owner_sheet: None,
    is_power: false,
})
```

Exact fix needed:
- Remove the three stray lines `scope`, `owner_sheet`, and `is_power` from the local `PinInfo` struct.
- Remove the same three lines from the `PinInfo` initializer in `classify_one`.
- Do **not** add a `NetScope` import here; that would only mask one symptom. These fields belong to `volt_core::project::Net`, not to this local analysis helper type.

Why:
- `PinInfo` is a local helper with fields `_signal`, `role`, `net`, and `net_class` only.
- The schema upgrade for `Net` appears to have been accidentally pasted into this unrelated helper, which causes:
  - parse failure (`expected type, found keyword false`),
  - unresolved `NetScope`, and
  - `None` being interpreted where a type is expected.

#### 2. `eda/crates/volt-cli/src/commands/export.rs`
Problem area:

```rust
circuit.nets = vec![
    Net {
        uuid: plane_net,
        name: "GND".into(),
        auto_name: false,
        net_class,
    },
    Net {
        uuid: trace_net,
        name: "SIG".into(),
        auto_name: false,
        net_class,
    },
];
```

Exact fix needed:
- Add the new `Net` fields to both literals:
  - `scope: NetScope::Global`
  - `owner_sheet: None`
  - `is_power: ...`
- For semantic correctness in this test:
  - `GND` net should be `is_power: true`
  - `SIG` net should be `is_power: false`

Use the same pattern already used in `eda/crates/volt-cli/src/commands/net.rs`:

```rust
let net = Net {
    uuid: net_uuid,
    name: name.to_string(),
    auto_name: false,
    net_class: default_nc.uuid,
    scope: NetScope::Global,
    owner_sheet: None,
    is_power: false,
};
```

Why:
- `volt_core::project::Net` now requires the new schema fields.
- This failing test is constructing raw `Net` values by hand.

#### 3. `eda/crates/volt-core/src/split.rs`
Problem area in `board_split_into_two`:

```rust
let mut board = Board {
    ...
    net_segments: vec![...],
    sheet_refs: vec![],
    hierarchical_ports: vec![],
    power_ports: vec![],
    power_flags: vec![],
    bus_segments: vec![],
    bus_entries: vec![],
    bus_aliases: vec![],
    planes: vec![],
    polygons: vec![],
    holes: vec![],
};
```

Exact fix needed:
- Remove these seven fields from the `Board` literal:
  - `sheet_refs`
  - `hierarchical_ports`
  - `power_ports`
  - `power_flags`
  - `bus_segments`
  - `bus_entries`
  - `bus_aliases`
- Keep only the actual `Board` fields (`planes`, `polygons`, `holes`, etc.).
- The immediately following test `board_no_split_when_connected` already shows the correct `Board` field set and can be copied.

Why:
- Those seven fields were added to `Schematic`, not `Board`.
- In the same file, the two `Schematic` tests above are correctly updated with these new fields, so this `Board` constructor appears to be a copy/paste spillover from the schematic schema update.

## Architecture
- `eda/crates/volt-core/src/project/mod.rs` is the source of truth for serialized project schema.
- `volt-cli` test/support code and `volt-core` tests instantiate schema structs directly rather than through builders/defaults, so schema changes fan out into handwritten literals.
- Current compiler failures are all downstream of that schema source:
  - `autoplace/analysis.rs` has an accidental paste of new `Net` fields into a non-schema helper type.
  - `export.rs` test code still uses the pre-upgrade `Net` literal shape.
  - `split.rs` has new `Schematic` fields incorrectly inserted into a `Board` literal.
- I checked the schema definitions and nearby code paths: `LineEndpoint` support is already updated in the currently visible core logic (`split.rs`, `render.rs`, `autoplace/tidy.rs`). No current compiler error for `SheetPin`/`HierPort` surfaced in this `cargo test` run.

### Follow-up note not surfaced as a compiler error in this run
`eda/crates/volt-core/tests/roundtrip.rs` still has:

```rust
fn roundtrip_line_endpoint_enum() {
    let sym_ep = LineEndpoint::Symbol { ... };
    roundtrip(&sym_ep);

    let junc_ep = LineEndpoint::Junction { ... };
    roundtrip(&junc_ep);
}
```

This is not a compile failure, but the schema upgrade added two more `LineEndpoint` variants. Test coverage should likely be extended with roundtrip cases for:
- `LineEndpoint::SheetPin { sheet_ref, pin }`
- `LineEndpoint::HierPort { port }`

## Start Here
Start with `eda/crates/volt-core/src/project/mod.rs` because it is the authoritative schema that explains every failing constructor. Then inspect the three failing files in this order:
1. `eda/crates/volt-cli/src/commands/autoplace/analysis.rs` - obvious bad paste into a local helper type.
2. `eda/crates/volt-cli/src/commands/export.rs` - straightforward `Net` literal updates.
3. `eda/crates/volt-core/src/split.rs` - remove schematic-only fields from the `Board` test literal.
