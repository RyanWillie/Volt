//! Schematic auto-placement and tidy algorithms.
//!
//! `autoplace` — generates an initial layout from a connected circuit.
//! `tidy` — cleans up an existing schematic layout.

use std::collections::{HashMap, HashSet};
use std::path::Path;

use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Component, Symbol};
use volt_core::project::*;

use super::project_io::{self, Result};

const GRID: f64 = 2.54;

// ===========================================================================
// Autoplace
// ===========================================================================

pub fn autoplace_schematic(project: &Path, sch_name: &str) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    let (lib_comps, lib_syms) = load_library_data(project, &circuit)?;
    let net_members = build_net_members(&circuit);
    let power_nets = detect_power_nets(&circuit);

    // --- Step 1: Find anchor IC (highest signal-degree component) ---
    let comp_signal_degree = compute_signal_degrees(&circuit, &net_members, &power_nets);
    let anchor_uuid = circuit.components.iter()
        .max_by_key(|c| comp_signal_degree.get(&c.uuid).unwrap_or(&0))
        .map(|c| c.uuid)
        .ok_or("No components to place")?;

    let anchor_inst = circuit.components.iter().find(|c| c.uuid == anchor_uuid).unwrap();

    // --- Step 2: Detect signal chains FIRST (before zone classification) ---
    // Follow signal nets outward from anchor to find chains.
    // A chain is: anchor-pin → comp_A → comp_B → comp_C (each via a signal net).
    // Components in chains get locked to their chain and won't be placed as power-only.
    let mut chain_member: HashSet<Uuid> = HashSet::new(); // all components assigned to a chain
    chain_member.insert(anchor_uuid);

    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    enum Zone { Left, Right, Top, Bottom }

    struct ZoneChain {
        zone: Zone,
        sort_key: f64,
        root: Uuid,
        chain: Vec<Uuid>, // components after root, in order
    }

    let mut zone_chains: Vec<ZoneChain> = Vec::new();

    // For each signal connection from anchor, follow the chain outward
    for anchor_conn in &anchor_inst.signal_connections {
        let Some(net_uuid) = anchor_conn.net else { continue };
        if power_nets.contains(&net_uuid) { continue; }
        let Some(members) = net_members.get(&net_uuid) else { continue };

        // Get the anchor pin direction for this signal
        let pin_pos = get_pin_position_for_signal(
            anchor_inst, anchor_conn.signal, &lib_comps, &lib_syms,
        );
        let (px, py) = pin_pos.unwrap_or((0.0, 0.0));
        let zone = if px.abs() > py.abs() {
            if px < 0.0 { Zone::Left } else { Zone::Right }
        } else {
            if py < 0.0 { Zone::Top } else { Zone::Bottom }
        };
        let sort_key = match zone {
            Zone::Left | Zone::Right => py,
            Zone::Top | Zone::Bottom => px,
        };

        // Find direct neighbors on this net that aren't already claimed
        for &member in members {
            if member == anchor_uuid || chain_member.contains(&member) { continue; }

            // Start a chain from this member
            chain_member.insert(member);
            let mut chain = Vec::new();
            let mut current = member;

            // Follow signal connections outward
            loop {
                let inst = circuit.components.iter().find(|c| c.uuid == current).unwrap();
                let mut next_comp = None;
                for conn in &inst.signal_connections {
                    let Some(n) = conn.net else { continue };
                    if power_nets.contains(&n) { continue; }
                    if let Some(ms) = net_members.get(&n) {
                        for &m in ms {
                            if m == current || chain_member.contains(&m) { continue; }
                            next_comp = Some(m);
                            break;
                        }
                    }
                    if next_comp.is_some() { break; }
                }
                if let Some(next) = next_comp {
                    chain.push(next);
                    chain_member.insert(next);
                    current = next;
                } else {
                    break;
                }
            }

            zone_chains.push(ZoneChain { zone, sort_key, root: member, chain });
        }
    }

    // Sort chains within each zone by pin order
    zone_chains.sort_by(|a, b| {
        a.zone.cmp_order().cmp(&b.zone.cmp_order())
            .then(a.sort_key.partial_cmp(&b.sort_key).unwrap())
    });

    // Remaining components (power-only) that weren't claimed by any signal chain
    let mut power_only_comps: Vec<Uuid> = circuit.components.iter()
        .filter(|c| !chain_member.contains(&c.uuid))
        .map(|c| c.uuid)
        .collect();

    // Chain power-only components together (battery→switch→regulator)
    let mut power_chain_order = Vec::new();
    {
        let mut remaining: HashSet<Uuid> = power_only_comps.iter().copied().collect();
        let mut sorted: Vec<Uuid> = power_only_comps.clone();
        sorted.sort_by_key(|&uuid| {
            circuit.components.iter().find(|c| c.uuid == uuid)
                .map(|inst| inst.signal_connections.iter()
                    .filter_map(|c| c.net)
                    .flat_map(|n| net_members.get(&n).into_iter().flatten())
                    .filter(|&&m| m != uuid)
                    .collect::<HashSet<_>>().len())
                .unwrap_or(0)
        });
        while let Some(&start) = sorted.iter().find(|u| remaining.contains(u)) {
            remaining.remove(&start);
            power_chain_order.push(start);
            let mut current = start;
            loop {
                let inst = circuit.components.iter().find(|c| c.uuid == current).unwrap();
                let mut next = None;
                for conn in &inst.signal_connections {
                    let Some(n) = conn.net else { continue };
                    if let Some(ms) = net_members.get(&n) {
                        for &m in ms {
                            if remaining.contains(&m) { next = Some(m); break; }
                        }
                    }
                    if next.is_some() { break; }
                }
                if let Some(n) = next {
                    remaining.remove(&n);
                    power_chain_order.push(n);
                    current = n;
                } else { break; }
            }
        }
    }

    // --- Step 3: Compute positions ---
    let sym_extents = compute_all_extents(&circuit, &lib_comps, &lib_syms);
    let anchor_ext = sym_extents.get(&anchor_uuid).copied().unwrap_or((4.0, 4.0));

    let anchor_gx = 20.0_f64;
    let anchor_gy = 16.0_f64;
    let mut placements: HashMap<Uuid, (f64, f64, f64)> = HashMap::new();
    placements.insert(anchor_uuid, (anchor_gx, anchor_gy, 0.0));

    // Place zone chains
    impl Zone {
        fn cmp_order(&self) -> u8 {
            match self { Zone::Left => 0, Zone::Right => 1, Zone::Top => 2, Zone::Bottom => 3 }
        }
    }

    // Group chains by zone
    let mut left_chains: Vec<&ZoneChain> = Vec::new();
    let mut right_chains: Vec<&ZoneChain> = Vec::new();
    let mut top_chains: Vec<&ZoneChain> = Vec::new();
    let mut bottom_chains: Vec<&ZoneChain> = Vec::new();

    for zc in &zone_chains {
        match zc.zone {
            Zone::Left => left_chains.push(zc),
            Zone::Right => right_chains.push(zc),
            Zone::Top => top_chains.push(zc),
            Zone::Bottom => bottom_chains.push(zc),
        }
    }

    // Place left zone chains: roots in a column, chains extend further left
    {
        let col_x = anchor_gx - anchor_ext.0 - 4.0;
        let n = left_chains.len();
        let start_y = anchor_gy - ((n as f64 - 1.0) * 3.5) / 2.0;
        for (i, zc) in left_chains.iter().enumerate() {
            let gy = start_y + i as f64 * 3.5;
            placements.insert(zc.root, (col_x, gy, 0.0));
            let mut cx = col_x - 4.0;
            for &chain_uuid in &zc.chain {
                placements.insert(chain_uuid, (cx, gy, 0.0));
                cx -= 4.0;
            }
        }
    }

    // Place right zone chains: roots in a column, chains extend further right
    {
        let col_x = anchor_gx + anchor_ext.0 + 4.0;
        let n = right_chains.len();
        let start_y = anchor_gy - ((n as f64 - 1.0) * 3.5) / 2.0;
        for (i, zc) in right_chains.iter().enumerate() {
            let gy = start_y + i as f64 * 3.5;
            placements.insert(zc.root, (col_x, gy, 0.0));
            let mut cx = col_x + 4.0;
            for &chain_uuid in &zc.chain {
                placements.insert(chain_uuid, (cx, gy, 0.0));
                cx += 4.0;
            }
        }
    }

    // Place top zone chains: roots in a row, chains extend further up
    {
        let row_y = anchor_gy - anchor_ext.1 - 4.0;
        let n = top_chains.len();
        let start_x = anchor_gx - ((n as f64 - 1.0) * 5.0) / 2.0;
        for (i, zc) in top_chains.iter().enumerate() {
            let gx = start_x + i as f64 * 5.0;
            placements.insert(zc.root, (gx, row_y, 0.0));
            let mut cy = row_y - 4.0;
            for &chain_uuid in &zc.chain {
                placements.insert(chain_uuid, (gx, cy, 0.0));
                cy -= 4.0;
            }
        }
    }

    // Place bottom zone chains
    {
        let row_y = anchor_gy + anchor_ext.1 + 4.0;
        let n = bottom_chains.len();
        let start_x = anchor_gx - ((n as f64 - 1.0) * 5.0) / 2.0;
        for (i, zc) in bottom_chains.iter().enumerate() {
            let gx = start_x + i as f64 * 5.0;
            placements.insert(zc.root, (gx, row_y, 0.0));
            let mut cy = row_y + 4.0;
            for &chain_uuid in &zc.chain {
                placements.insert(chain_uuid, (gx, cy, 0.0));
                cy += 4.0;
            }
        }
    }

    // Place power chain above everything
    {
        let min_y = placements.values().map(|(_, y, _)| *y).fold(f64::INFINITY, f64::min);
        let power_y = (min_y - 5.0).min(anchor_gy - anchor_ext.1 - 8.0);
        let n = power_chain_order.len();
        let start_x = anchor_gx - ((n as f64 - 1.0) * 5.0) / 2.0;
        for (i, &uuid) in power_chain_order.iter().enumerate() {
            if !placements.contains_key(&uuid) {
                placements.insert(uuid, (start_x + i as f64 * 5.0, power_y, 0.0));
            }
        }
    }

    // --- Step 5: Build schematic ---
    schematic.symbols.clear();
    schematic.net_segments.clear();

    for inst in &circuit.components {
        let Some(&(gx, gy, rot)) = placements.get(&inst.uuid) else { continue };
        let Some(lib_comp) = lib_comps.get(&inst.lib_component) else { continue };
        let Some(variant) = lib_comp.variants.iter().find(|v| v.uuid == inst.lib_variant) else { continue };
        let Some(gate) = variant.gates.first() else { continue };
        let Some(lib_sym) = lib_syms.get(&gate.symbol) else { continue };

        let px = gx * GRID;
        let py = gy * GRID;

        let texts: Vec<SchematicText> = lib_sym.texts.iter().map(|t| {
            SchematicText {
                uuid: new_uuid(),
                layer: t.layer,
                value: t.value.clone(),
                position: Position::new(px + t.position.x, py + t.position.y),
                rotation: Angle(t.rotation.0 + rot),
                height: t.height,
                align: t.align,
                lock: t.lock,
            }
        }).collect();

        schematic.symbols.push(SchematicSymbol {
            uuid: new_uuid(),
            component: inst.uuid,
            lib_gate: gate.uuid,
            position: Position::new(px, py),
            rotation: Angle(rot),
            mirror: false,
            texts,
        });
    }

    // --- Step 6: Wire nets ---
    let sym_by_comp: HashMap<Uuid, &SchematicSymbol> = schematic.symbols.iter()
        .map(|s| (s.component, s)).collect();

    for net in &circuit.nets {
        let endpoints = collect_pin_endpoints(&circuit, net, &sym_by_comp, &lib_comps, &lib_syms);
        if endpoints.is_empty() { continue; }

        let mut seg = SchematicNetSegment {
            uuid: new_uuid(),
            net: net.uuid,
            junctions: vec![],
            lines: vec![],
            labels: vec![],
        };

        let is_power = power_nets.contains(&net.uuid);

        if is_power || endpoints.len() > 4 {
            // High-fanout / power nets: label at each pin, no wires
            for (_, _, pos) in &endpoints {
                let label_y = pos.y - GRID * 2.0;
                seg.labels.push(NetLabel {
                    uuid: new_uuid(),
                    position: Position::new(
                        (pos.x / GRID).round() * GRID,
                        (label_y / GRID).round() * GRID,
                    ),
                    rotation: Angle(0.0),
                    mirror: false,
                });
            }
        } else {
            // Signal nets: chain wire sorted by X then Y
            let mut sorted = endpoints.clone();
            sorted.sort_by(|a, b| a.2.x.partial_cmp(&b.2.x).unwrap()
                .then(a.2.y.partial_cmp(&b.2.y).unwrap()));

            for window in sorted.windows(2) {
                let (sym_a, pin_a, pos_a) = &window[0];
                let (sym_b, pin_b, pos_b) = &window[1];

                let from = LineEndpoint::Symbol { symbol: *sym_a, pin: *pin_a };
                let to = LineEndpoint::Symbol { symbol: *sym_b, pin: *pin_b };

                let dx = (pos_b.x - pos_a.x).abs();
                let dy = (pos_b.y - pos_a.y).abs();

                if dx < 0.01 || dy < 0.01 {
                    seg.lines.push(SchematicLine {
                        uuid: new_uuid(), width: 0.15875, from, to,
                    });
                } else {
                    // Manhattan bend
                    let bend = if dx >= dy {
                        Position::new(pos_b.x, pos_a.y)
                    } else {
                        Position::new(pos_a.x, pos_b.y)
                    };
                    let bend_snapped = Position::new(
                        (bend.x / GRID).round() * GRID,
                        (bend.y / GRID).round() * GRID,
                    );
                    let junc_uuid = new_uuid();
                    seg.junctions.push(Junction { uuid: junc_uuid, position: bend_snapped });
                    let bend_ep = LineEndpoint::Junction { junction: junc_uuid };
                    seg.lines.push(SchematicLine {
                        uuid: new_uuid(), width: 0.15875, from, to: bend_ep.clone(),
                    });
                    seg.lines.push(SchematicLine {
                        uuid: new_uuid(), width: 0.15875, from: bend_ep, to,
                    });
                }
            }

            if sorted.len() >= 3 {
                let mid = &sorted[sorted.len() / 2].2;
                seg.labels.push(NetLabel {
                    uuid: new_uuid(),
                    position: Position::new(
                        (mid.x / GRID).round() * GRID,
                        ((mid.y - GRID * 2.0) / GRID).round() * GRID,
                    ),
                    rotation: Angle(0.0), mirror: false,
                });
            }
        }

        schematic.net_segments.push(seg);
    }

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "autoplace": {
            "components_placed": placements.len(),
            "nets_wired": schematic.net_segments.len(),
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// Tidy
// ===========================================================================

pub fn tidy_schematic(project: &Path, sch_name: &str) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let (lib_comps, lib_syms) = load_library_data(project, &circuit)?;

    let mut changes = Vec::new();

    // 1. Dedup junctions
    let deduped = dedup_junctions(&mut schematic);
    if deduped > 0 { changes.push(format!("Deduplicated {deduped} junctions")); }

    // 2. Remove zero-length wires
    let removed = remove_zero_length_wires(&mut schematic);
    if removed > 0 { changes.push(format!("Removed {removed} zero-length wires")); }

    // 3. Reset all field positions
    let reset = reset_all_fields(project, &circuit, &mut schematic)?;
    if reset > 0 { changes.push(format!("Reset {reset} text fields")); }

    // 4. Move labels away from component bodies
    let comp_boxes = compute_component_boxes(&schematic, &circuit, &lib_comps, &lib_syms);
    let label_fixes = fix_label_overlaps(&mut schematic, &comp_boxes);
    if label_fixes > 0 { changes.push(format!("Repositioned {label_fixes} overlapping labels")); }

    // 5. Spread overlapping labels
    let spread = spread_overlapping_labels(&mut schematic);
    if spread > 0 { changes.push(format!("Spread {spread} overlapping labels")); }

    // 6. Compact layout
    let compacted = compact_layout(&mut schematic);
    if compacted { changes.push("Compacted layout towards origin".into()); }

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "tidy": { "changes": changes }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// Shared helpers
// ===========================================================================

fn load_library_data(
    project: &Path, circuit: &Circuit,
) -> Result<(HashMap<Uuid, Component>, HashMap<Uuid, Symbol>)> {
    let mut lib_comps: HashMap<Uuid, Component> = HashMap::new();
    let mut lib_syms: HashMap<Uuid, Symbol> = HashMap::new();
    for inst in &circuit.components {
        if lib_comps.contains_key(&inst.lib_component) { continue; }
        if let Ok(c) = project_io::read_library_element::<Component>(project, "components", &inst.lib_component) {
            if let Some(variant) = c.variants.iter().find(|v| v.uuid == inst.lib_variant) {
                for gate in &variant.gates {
                    if !lib_syms.contains_key(&gate.symbol) {
                        if let Ok(s) = project_io::read_library_element::<Symbol>(project, "symbols", &gate.symbol) {
                            lib_syms.insert(gate.symbol, s);
                        }
                    }
                }
            }
            lib_comps.insert(inst.lib_component, c);
        }
    }
    Ok((lib_comps, lib_syms))
}

fn build_net_members(circuit: &Circuit) -> HashMap<Uuid, Vec<Uuid>> {
    let mut m: HashMap<Uuid, Vec<Uuid>> = HashMap::new();
    for inst in &circuit.components {
        for conn in &inst.signal_connections {
            if let Some(net) = conn.net { m.entry(net).or_default().push(inst.uuid); }
        }
    }
    m
}

fn detect_power_nets(circuit: &Circuit) -> HashSet<Uuid> {
    circuit.nets.iter().filter(|n| is_power_net_name(&n.name)).map(|n| n.uuid).collect()
}

fn is_power_net_name(name: &str) -> bool {
    let u = name.to_uppercase();
    matches!(u.as_str(),
        "VCC"|"VDD"|"V+"|"VBUS"|"VBAT"|"VIN"|"VOUT"|
        "GND"|"VSS"|"V-"|"AGND"|"DGND"|"PGND"|"GNDA"|
        "3V3"|"+3V3"|"+3.3V"|"5V"|"+5V"|"+12V"|"+24V"|
        "BAT_RAW"
    )
}

fn compute_signal_degrees(
    circuit: &Circuit, net_members: &HashMap<Uuid, Vec<Uuid>>, power_nets: &HashSet<Uuid>,
) -> HashMap<Uuid, usize> {
    let mut degrees = HashMap::new();
    for inst in &circuit.components {
        let d = inst.signal_connections.iter()
            .filter_map(|c| c.net)
            .filter(|n| !power_nets.contains(n))
            .flat_map(|n| net_members.get(&n).into_iter().flatten())
            .filter(|&&c| c != inst.uuid)
            .collect::<HashSet<_>>().len();
        degrees.insert(inst.uuid, d);
    }
    degrees
}

fn compute_all_extents(
    circuit: &Circuit, lib_comps: &HashMap<Uuid, Component>, lib_syms: &HashMap<Uuid, Symbol>,
) -> HashMap<Uuid, (f64, f64)> {
    let mut extents = HashMap::new();
    for inst in &circuit.components {
        if let Some(c) = lib_comps.get(&inst.lib_component) {
            if let Some(v) = c.variants.iter().find(|v| v.uuid == inst.lib_variant) {
                if let Some(g) = v.gates.first() {
                    if let Some(s) = lib_syms.get(&g.symbol) {
                        extents.insert(inst.uuid, symbol_half_extents(s));
                    }
                }
            }
        }
    }
    extents
}

fn symbol_half_extents(sym: &Symbol) -> (f64, f64) {
    let mut mx = 0.0_f64;
    let mut my = 0.0_f64;
    for pin in &sym.pins {
        mx = mx.max(pin.position.x.abs() / GRID);
        my = my.max(pin.position.y.abs() / GRID);
    }
    for poly in &sym.polygons {
        for v in &poly.vertices {
            mx = mx.max(v.position.x.abs() / GRID);
            my = my.max(v.position.y.abs() / GRID);
        }
    }
    (mx.ceil().max(2.0), my.ceil().max(2.0))
}

fn get_pin_position_for_signal(
    inst: &ComponentInstance, signal_uuid: Uuid,
    lib_comps: &HashMap<Uuid, Component>, lib_syms: &HashMap<Uuid, Symbol>,
) -> Option<(f64, f64)> {
    let c = lib_comps.get(&inst.lib_component)?;
    let v = c.variants.iter().find(|v| v.uuid == inst.lib_variant)?;
    let g = v.gates.first()?;
    let s = lib_syms.get(&g.symbol)?;
    let m = g.pin_mappings.iter().find(|m| m.signal == signal_uuid)?;
    let pin = s.pins.iter().find(|p| p.uuid == m.pin)?;
    Some((pin.position.x, pin.position.y))
}

fn collect_pin_endpoints(
    circuit: &Circuit, net: &Net, sym_by_comp: &HashMap<Uuid, &SchematicSymbol>,
    lib_comps: &HashMap<Uuid, Component>, lib_syms: &HashMap<Uuid, Symbol>,
) -> Vec<(Uuid, Uuid, Position)> {
    let mut eps = Vec::new();
    for inst in &circuit.components {
        for conn in &inst.signal_connections {
            if conn.net != Some(net.uuid) { continue; }
            let Some(ss) = sym_by_comp.get(&inst.uuid) else { continue };
            let Some(lc) = lib_comps.get(&inst.lib_component) else { continue };
            let Some(v) = lc.variants.iter().find(|v| v.uuid == inst.lib_variant) else { continue };
            let Some(g) = v.gates.first() else { continue };
            let Some(ls) = lib_syms.get(&g.symbol) else { continue };
            let Some(m) = g.pin_mappings.iter().find(|m| m.signal == conn.signal) else { continue };
            let Some(pin) = ls.pins.iter().find(|p| p.uuid == m.pin) else { continue };
            let rot = ss.rotation.0.to_radians();
            let wp = Position::new(
                ss.position.x + pin.position.x * rot.cos() - pin.position.y * rot.sin(),
                ss.position.y + pin.position.x * rot.sin() + pin.position.y * rot.cos(),
            );
            eps.push((ss.uuid, pin.uuid, wp));
        }
    }
    eps
}

// ===========================================================================
// Tidy helpers
// ===========================================================================

/// Bounding box in world coordinates (mm)
#[derive(Debug, Clone, Copy)]
struct BBox { x0: f64, y0: f64, x1: f64, y1: f64 }

impl BBox {
    fn overlaps(&self, other: &BBox) -> bool {
        self.x0 < other.x1 && self.x1 > other.x0 &&
        self.y0 < other.y1 && self.y1 > other.y0
    }
    fn contains(&self, x: f64, y: f64) -> bool {
        x >= self.x0 && x <= self.x1 && y >= self.y0 && y <= self.y1
    }
}

fn compute_component_boxes(
    schematic: &Schematic, circuit: &Circuit,
    lib_comps: &HashMap<Uuid, Component>, lib_syms: &HashMap<Uuid, Symbol>,
) -> Vec<BBox> {
    let comp_by_uuid: HashMap<Uuid, &ComponentInstance> = circuit.components.iter().map(|c| (c.uuid, c)).collect();
    let mut boxes = Vec::new();

    for sym in &schematic.symbols {
        let Some(inst) = comp_by_uuid.get(&sym.component) else { continue };
        let Some(lc) = lib_comps.get(&inst.lib_component) else { continue };
        let Some(v) = lc.variants.iter().find(|v| v.uuid == inst.lib_variant) else { continue };
        let Some(g) = v.gates.iter().find(|g| g.uuid == sym.lib_gate).or(v.gates.first()) else { continue };
        let Some(ls) = lib_syms.get(&g.symbol) else { continue };

        let rot = sym.rotation.0.to_radians();
        let mut xs = Vec::new();
        let mut ys = Vec::new();

        for pin in &ls.pins {
            let wx = sym.position.x + pin.position.x * rot.cos() - pin.position.y * rot.sin();
            let wy = sym.position.y + pin.position.x * rot.sin() + pin.position.y * rot.cos();
            xs.push(wx); ys.push(wy);
        }
        for poly in &ls.polygons {
            for v in &poly.vertices {
                let wx = sym.position.x + v.position.x * rot.cos() - v.position.y * rot.sin();
                let wy = sym.position.y + v.position.x * rot.sin() + v.position.y * rot.cos();
                xs.push(wx); ys.push(wy);
            }
        }

        if let (Some(&min_x), Some(&max_x), Some(&min_y), Some(&max_y)) = (
            xs.iter().reduce(|a, b| if a < b { a } else { b }),
            xs.iter().reduce(|a, b| if a > b { a } else { b }),
            ys.iter().reduce(|a, b| if a < b { a } else { b }),
            ys.iter().reduce(|a, b| if a > b { a } else { b }),
        ) {
            boxes.push(BBox {
                x0: min_x - 1.0, y0: min_y - 1.0,
                x1: max_x + 1.0, y1: max_y + 1.0,
            });
        }
    }
    boxes
}

fn fix_label_overlaps(schematic: &mut Schematic, comp_boxes: &[BBox]) -> usize {
    let mut fixed = 0;
    for seg in &mut schematic.net_segments {
        for label in &mut seg.labels {
            let lx = label.position.x;
            let ly = label.position.y;

            // Check if label is inside any component bounding box
            for bbox in comp_boxes {
                if bbox.contains(lx, ly) {
                    // Move label above the component
                    label.position.y = bbox.y0 - GRID;
                    fixed += 1;
                    break;
                }
            }
        }
    }
    fixed
}

fn dedup_junctions(schematic: &mut Schematic) -> usize {
    let mut total = 0;
    for seg in &mut schematic.net_segments {
        let mut seen: HashMap<(i64, i64), Uuid> = HashMap::new();
        let mut remap: HashMap<Uuid, Uuid> = HashMap::new();
        for junc in &seg.junctions {
            let key = ((junc.position.x * 1000.0).round() as i64, (junc.position.y * 1000.0).round() as i64);
            if let Some(&existing) = seen.get(&key) {
                remap.insert(junc.uuid, existing);
                total += 1;
            } else { seen.insert(key, junc.uuid); }
        }
        if remap.is_empty() { continue; }
        seg.junctions.retain(|j| !remap.contains_key(&j.uuid));
        for line in &mut seg.lines {
            remap_endpoint(&mut line.from, &remap);
            remap_endpoint(&mut line.to, &remap);
        }
    }
    total
}

fn remap_endpoint(ep: &mut LineEndpoint, remap: &HashMap<Uuid, Uuid>) {
    if let LineEndpoint::Junction { junction } = ep {
        if let Some(&new) = remap.get(junction) { *junction = new; }
    }
}

fn remove_zero_length_wires(schematic: &mut Schematic) -> usize {
    let mut total = 0;
    for seg in &mut schematic.net_segments {
        let before = seg.lines.len();
        seg.lines.retain(|l| l.from != l.to);
        total += before - seg.lines.len();
    }
    total
}

fn reset_all_fields(project: &Path, circuit: &Circuit, schematic: &mut Schematic) -> Result<usize> {
    let (lib_comps, lib_syms) = load_library_data(project, circuit)?;
    let mut count = 0;
    for sym in &mut schematic.symbols {
        let Some(inst) = circuit.components.iter().find(|c| c.uuid == sym.component) else { continue };
        let Some(lc) = lib_comps.get(&inst.lib_component) else { continue };
        let Some(v) = lc.variants.iter().find(|v| v.uuid == inst.lib_variant) else { continue };
        let Some(g) = v.gates.iter().find(|g| g.uuid == sym.lib_gate).or(v.gates.first()) else { continue };
        let Some(ls) = lib_syms.get(&g.symbol) else { continue };
        for template in &ls.texts {
            let rebuilt = SchematicText {
                uuid: sym.texts.iter().find(|t| t.layer == template.layer).map(|t| t.uuid).unwrap_or_else(new_uuid),
                layer: template.layer,
                value: template.value.clone(),
                position: Position::new(sym.position.x + template.position.x, sym.position.y + template.position.y),
                rotation: Angle(template.rotation.0 + sym.rotation.0),
                height: template.height,
                align: template.align,
                lock: template.lock,
            };
            if let Some(existing) = sym.texts.iter_mut().find(|t| t.layer == template.layer) {
                *existing = rebuilt;
            } else { sym.texts.push(rebuilt); }
            count += 1;
        }
    }
    Ok(count)
}

fn spread_overlapping_labels(schematic: &mut Schematic) -> usize {
    let mut positions: Vec<(usize, usize, f64, f64)> = Vec::new();
    for (si, seg) in schematic.net_segments.iter().enumerate() {
        for (li, label) in seg.labels.iter().enumerate() {
            positions.push((si, li, label.position.x, label.position.y));
        }
    }
    let mut nudged = 0;
    let threshold = GRID * 3.0;
    for i in 0..positions.len() {
        for j in (i+1)..positions.len() {
            let (_, _, x1, y1) = positions[i];
            let (si, li, x2, y2) = positions[j];
            let dist = ((x2-x1).powi(2) + (y2-y1).powi(2)).sqrt();
            if dist < threshold {
                let new_y = y2 + GRID * 2.5;
                positions[j].3 = new_y;
                schematic.net_segments[si].labels[li].position.y = new_y;
                nudged += 1;
            }
        }
    }
    nudged
}

fn compact_layout(schematic: &mut Schematic) -> bool {
    if schematic.symbols.is_empty() { return false; }
    let mut min_x = f64::INFINITY;
    let mut min_y = f64::INFINITY;
    for sym in &schematic.symbols {
        min_x = min_x.min(sym.position.x);
        min_y = min_y.min(sym.position.y);
    }
    for seg in &schematic.net_segments {
        for junc in &seg.junctions { min_x = min_x.min(junc.position.x); min_y = min_y.min(junc.position.y); }
        for label in &seg.labels { min_x = min_x.min(label.position.x); min_y = min_y.min(label.position.y); }
    }
    let target = 8.0 * GRID;
    let dx = target - min_x;
    let dy = target - min_y;
    if dx.abs() < 0.01 && dy.abs() < 0.01 { return false; }
    for sym in &mut schematic.symbols {
        sym.position.x += dx; sym.position.y += dy;
        for t in &mut sym.texts { t.position.x += dx; t.position.y += dy; }
    }
    for seg in &mut schematic.net_segments {
        for j in &mut seg.junctions { j.position.x += dx; j.position.y += dy; }
        for l in &mut seg.labels { l.position.x += dx; l.position.y += dy; }
    }
    true
}
