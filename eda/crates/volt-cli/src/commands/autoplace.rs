//! Schematic auto-placement and tidy algorithms.
//!
//! `autoplace` — generates an initial layout from a connected circuit.
//! `tidy` — cleans up an existing schematic layout.

use std::collections::{HashMap, HashSet, VecDeque};
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

    // Load all library data
    let (lib_comps, lib_syms) = load_library_data(project, &circuit)?;

    // Build connectivity
    let net_members = build_net_members(&circuit);
    let power_nets = detect_power_nets(&circuit);

    // Score components: signal degree (non-power connections to other components)
    let comp_signal_degree = compute_signal_degrees(&circuit, &net_members, &power_nets);

    // Find the anchor: highest signal degree (usually the IC)
    let anchor = circuit.components.iter()
        .max_by_key(|c| comp_signal_degree.get(&c.uuid).unwrap_or(&0))
        .map(|c| c.uuid);

    let Some(anchor_uuid) = anchor else {
        return Err("No components to place".into());
    };

    // Compute symbol half-extents
    let sym_extents = compute_all_extents(&circuit, &lib_comps, &lib_syms);

    // Classify each component's relationship to the anchor
    let anchor_inst = circuit.components.iter().find(|c| c.uuid == anchor_uuid).unwrap();
    let anchor_comp = lib_comps.get(&anchor_inst.lib_component);
    let anchor_sym = anchor_comp
        .and_then(|c| c.variants.iter().find(|v| v.uuid == anchor_inst.lib_variant))
        .and_then(|v| v.gates.first())
        .and_then(|g| lib_syms.get(&g.symbol));

    // --- PLACEMENT via BFS from anchor ---
    let anchor_ext = sym_extents.get(&anchor_uuid).copied().unwrap_or((4.0, 4.0));
    let anchor_gx = 20.0_f64;
    let anchor_gy = 16.0_f64;

    let mut placements: HashMap<Uuid, (f64, f64, f64)> = HashMap::new();
    placements.insert(anchor_uuid, (anchor_gx, anchor_gy, 0.0));

    // Track occupied grid cells to prevent overlap
    let mut occupied: HashSet<(i32, i32)> = HashSet::new();
    occupy_cells(&mut occupied, anchor_gx, anchor_gy, anchor_ext.0, anchor_ext.1);

    // BFS: place components outward from the anchor
    // Use two-pass: signal neighbors first (front of queue), power neighbors second (back)
    let mut queue: VecDeque<Uuid> = VecDeque::new();
    queue.push_back(anchor_uuid);
    let mut visited: HashSet<Uuid> = HashSet::new();
    visited.insert(anchor_uuid);

    while let Some(current_uuid) = queue.pop_front() {
        let current_inst = circuit.components.iter().find(|c| c.uuid == current_uuid).unwrap();
        let (cur_gx, cur_gy, _) = placements[&current_uuid];

        // Collect neighbors with signal/power distinction
        let mut signal_neighbors: Vec<(Uuid, f64, f64)> = Vec::new();
        let mut power_neighbors: Vec<(Uuid, f64, f64)> = Vec::new();

        for conn in &current_inst.signal_connections {
            let Some(net_uuid) = conn.net else { continue };
            let Some(members) = net_members.get(&net_uuid) else { continue };
            let is_power = power_nets.contains(&net_uuid);

            let pin_pos = get_pin_position_for_signal(
                current_inst, conn.signal, &lib_comps, &lib_syms,
            );

            for &member_uuid in members {
                if member_uuid == current_uuid || visited.contains(&member_uuid) { continue; }
                let (px, py) = pin_pos.unwrap_or((0.0, 0.0));
                if is_power {
                    power_neighbors.push((member_uuid, px, py));
                } else {
                    signal_neighbors.push((member_uuid, px, py));
                }
            }
        }

        // Deduplicate: if a neighbor appears in signal list, remove from power
        let signal_uuids: HashSet<Uuid> = signal_neighbors.iter().map(|(u, _, _)| *u).collect();
        power_neighbors.retain(|(u, _, _)| !signal_uuids.contains(u));

        // Dedup within each list
        let mut seen: HashSet<Uuid> = HashSet::new();
        signal_neighbors.retain(|(u, _, _)| seen.insert(*u));
        seen.clear();
        power_neighbors.retain(|(u, _, _)| seen.insert(*u));

        // Process all neighbors: signal first, then power
        let all_neighbors: Vec<(Uuid, f64, f64)> = signal_neighbors.into_iter()
            .chain(power_neighbors.into_iter())
            .collect();

        for (neighbor_uuid, pin_x, pin_y) in all_neighbors {
            if visited.contains(&neighbor_uuid) { continue; }
            visited.insert(neighbor_uuid);

            let neighbor_ext = sym_extents.get(&neighbor_uuid).copied().unwrap_or((2.0, 2.0));
            let cur_ext = sym_extents.get(&current_uuid).copied().unwrap_or((4.0, 4.0));

            // Determine placement direction from pin position
            let (dir_x, dir_y) = if pin_x.abs() > pin_y.abs() {
                if pin_x < 0.0 { (-1.0, 0.0) } else { (1.0, 0.0) }
            } else if pin_y.abs() > 0.01 {
                if pin_y < 0.0 { (0.0, -1.0) } else { (0.0, 1.0) }
            } else {
                (1.0, 0.0) // default: place to the right
            };

            let spacing_x = cur_ext.0 + neighbor_ext.0 + 2.0;
            let spacing_y = cur_ext.1 + neighbor_ext.1 + 1.5;

            let target_gx = cur_gx + dir_x * spacing_x;
            let target_gy = cur_gy + dir_y * spacing_y;

            let (gx, gy) = find_free_position(&occupied, target_gx, target_gy, neighbor_ext.0, neighbor_ext.1);
            occupy_cells(&mut occupied, gx, gy, neighbor_ext.0, neighbor_ext.1);
            placements.insert(neighbor_uuid, (gx, gy, 0.0));
            queue.push_back(neighbor_uuid);
        }
    }

    // Place any remaining unvisited components near the anchor
    for inst in &circuit.components {
        if placements.contains_key(&inst.uuid) { continue; }
        let ext = sym_extents.get(&inst.uuid).copied().unwrap_or((2.0, 2.0));
        let (gx, gy) = find_free_position(&occupied, anchor_gx, anchor_gy - 8.0, ext.0, ext.1);
        occupy_cells(&mut occupied, gx, gy, ext.0, ext.1);
        placements.insert(inst.uuid, (gx, gy, 0.0));
    }

    // --- BUILD SCHEMATIC ---
    schematic.symbols.clear();
    schematic.net_segments.clear();

    for inst in &circuit.components {
        let Some(&(gx, gy, rot)) = placements.get(&inst.uuid) else { continue };
        let lib_comp = match lib_comps.get(&inst.lib_component) { Some(c) => c, None => continue };
        let variant = match lib_comp.variants.iter().find(|v| v.uuid == inst.lib_variant) { Some(v) => v, None => continue };
        let gate = match variant.gates.first() { Some(g) => g, None => continue };
        let lib_sym = match lib_syms.get(&gate.symbol) { Some(s) => s, None => continue };

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

    // --- WIRE NETS ---
    let sym_by_comp: HashMap<Uuid, &SchematicSymbol> = schematic.symbols.iter()
        .map(|s| (s.component, s))
        .collect();

    for net in &circuit.nets {
        let pin_endpoints = collect_pin_endpoints(
            &circuit, net, &sym_by_comp, &lib_comps, &lib_syms,
        );
        if pin_endpoints.is_empty() { continue; }

        let mut seg = SchematicNetSegment {
            uuid: new_uuid(),
            net: net.uuid,
            junctions: vec![],
            lines: vec![],
            labels: vec![],
        };

        let is_power = power_nets.contains(&net.uuid);

        if is_power {
            // Power nets: label at each pin
            for (_, _, pos) in &pin_endpoints {
                seg.labels.push(NetLabel {
                    uuid: new_uuid(),
                    position: Position::new(
                        (pos.x / GRID).round() * GRID,
                        ((pos.y - GRID * 1.5) / GRID).round() * GRID,
                    ),
                    rotation: Angle(0.0),
                    mirror: false,
                });
            }
        } else {
            // Signal nets: chain wire
            let mut sorted = pin_endpoints.clone();
            sorted.sort_by(|a, b| {
                a.2.x.partial_cmp(&b.2.x).unwrap()
                    .then(a.2.y.partial_cmp(&b.2.y).unwrap())
            });

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

            // Label for multi-connection signal nets
            if sorted.len() >= 3 {
                let mid = &sorted[sorted.len() / 2].2;
                seg.labels.push(NetLabel {
                    uuid: new_uuid(),
                    position: Position::new(
                        (mid.x / GRID).round() * GRID,
                        ((mid.y - GRID * 1.5) / GRID).round() * GRID,
                    ),
                    rotation: Angle(0.0),
                    mirror: false,
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

    let mut changes = Vec::new();

    // 1. Deduplicate junctions
    let deduped = dedup_junctions(&mut schematic);
    if deduped > 0 { changes.push(format!("Deduplicated {deduped} junctions")); }

    // 2. Remove zero-length wires
    let removed = remove_zero_length_wires(&mut schematic);
    if removed > 0 { changes.push(format!("Removed {removed} zero-length wires")); }

    // 3. Reset field positions to library defaults
    let reset = reset_all_fields(project, &circuit, &mut schematic)?;
    if reset > 0 { changes.push(format!("Reset {reset} text fields")); }

    // 4. Snap nearly-aligned components to same row/column
    let snapped = snap_component_alignment(&mut schematic);
    if snapped > 0 { changes.push(format!("Snapped {snapped} components to alignment")); }

    // 5. Spread overlapping labels
    let spread = spread_overlapping_labels(&mut schematic);
    if spread > 0 { changes.push(format!("Spread {spread} overlapping labels")); }

    // 6. Compact: shift entire layout so top-left is near origin
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
    project: &Path,
    circuit: &Circuit,
) -> Result<(HashMap<Uuid, Component>, HashMap<Uuid, Symbol>)> {
    let mut lib_comps: HashMap<Uuid, Component> = HashMap::new();
    let mut lib_syms: HashMap<Uuid, Symbol> = HashMap::new();

    for inst in &circuit.components {
        if lib_comps.contains_key(&inst.lib_component) { continue; }
        if let Ok(c) = project_io::read_library_element::<Component>(
            project, "components", &inst.lib_component,
        ) {
            if let Some(variant) = c.variants.iter().find(|v| v.uuid == inst.lib_variant) {
                for gate in &variant.gates {
                    if !lib_syms.contains_key(&gate.symbol) {
                        if let Ok(s) = project_io::read_library_element::<Symbol>(
                            project, "symbols", &gate.symbol,
                        ) {
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
    let mut net_members: HashMap<Uuid, Vec<Uuid>> = HashMap::new();
    for inst in &circuit.components {
        for conn in &inst.signal_connections {
            if let Some(net_uuid) = conn.net {
                net_members.entry(net_uuid).or_default().push(inst.uuid);
            }
        }
    }
    net_members
}

fn detect_power_nets(circuit: &Circuit) -> HashSet<Uuid> {
    circuit.nets.iter()
        .filter(|n| is_power_net_name(&n.name))
        .map(|n| n.uuid)
        .collect()
}

fn is_power_net_name(name: &str) -> bool {
    let upper = name.to_uppercase();
    matches!(upper.as_str(),
        "VCC" | "VDD" | "V+" | "VBUS" | "VBAT" | "VIN" | "VOUT" |
        "GND" | "VSS" | "V-" | "AGND" | "DGND" | "PGND" | "GNDA" |
        "3V3" | "+3V3" | "+3.3V" | "5V" | "+5V" | "+12V" | "+24V" |
        "BAT_RAW"
    )
}

fn compute_signal_degrees(
    circuit: &Circuit,
    net_members: &HashMap<Uuid, Vec<Uuid>>,
    power_nets: &HashSet<Uuid>,
) -> HashMap<Uuid, usize> {
    let mut degrees = HashMap::new();
    for inst in &circuit.components {
        let degree: usize = inst.signal_connections.iter()
            .filter_map(|c| c.net)
            .filter(|n| !power_nets.contains(n))
            .flat_map(|n| net_members.get(&n).into_iter().flatten())
            .filter(|&&c| c != inst.uuid)
            .collect::<HashSet<_>>()
            .len();
        degrees.insert(inst.uuid, degree);
    }
    degrees
}

fn compute_all_extents(
    circuit: &Circuit,
    lib_comps: &HashMap<Uuid, Component>,
    lib_syms: &HashMap<Uuid, Symbol>,
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
    let mut max_x = 0.0_f64;
    let mut max_y = 0.0_f64;
    for pin in &sym.pins {
        max_x = max_x.max(pin.position.x.abs() / GRID);
        max_y = max_y.max(pin.position.y.abs() / GRID);
    }
    for poly in &sym.polygons {
        for v in &poly.vertices {
            max_x = max_x.max(v.position.x.abs() / GRID);
            max_y = max_y.max(v.position.y.abs() / GRID);
        }
    }
    (max_x.ceil().max(2.0), max_y.ceil().max(2.0))
}

fn occupy_cells(occupied: &mut HashSet<(i32, i32)>, gx: f64, gy: f64, hw: f64, hh: f64) {
    let x0 = (gx - hw - 1.0).floor() as i32;
    let x1 = (gx + hw + 1.0).ceil() as i32;
    let y0 = (gy - hh - 1.0).floor() as i32;
    let y1 = (gy + hh + 1.0).ceil() as i32;
    for x in x0..=x1 {
        for y in y0..=y1 {
            occupied.insert((x, y));
        }
    }
}

fn is_position_free(occupied: &HashSet<(i32, i32)>, gx: f64, gy: f64, hw: f64, hh: f64) -> bool {
    let x0 = (gx - hw - 1.0).floor() as i32;
    let x1 = (gx + hw + 1.0).ceil() as i32;
    let y0 = (gy - hh - 1.0).floor() as i32;
    let y1 = (gy + hh + 1.0).ceil() as i32;
    for x in x0..=x1 {
        for y in y0..=y1 {
            if occupied.contains(&(x, y)) { return false; }
        }
    }
    true
}

fn find_free_position(
    occupied: &HashSet<(i32, i32)>,
    target_x: f64,
    target_y: f64,
    hw: f64,
    hh: f64,
) -> (f64, f64) {
    let tx = target_x.round();
    let ty = target_y.round();
    if is_position_free(occupied, tx, ty, hw, hh) {
        return (tx, ty);
    }
    // Fine spiral outward
    for radius in 1..60 {
        let r = radius as f64;
        // Try 8 directions at each radius
        for angle_step in 0..8 {
            let angle = angle_step as f64 * std::f64::consts::FRAC_PI_4;
            let cx = (tx + r * angle.cos()).round();
            let cy = (ty + r * angle.sin()).round();
            if is_position_free(occupied, cx, cy, hw, hh) {
                return (cx, cy);
            }
        }
    }
    (tx + 20.0, ty)
}

fn get_pin_position_for_signal(
    inst: &ComponentInstance,
    signal_uuid: Uuid,
    lib_comps: &HashMap<Uuid, Component>,
    lib_syms: &HashMap<Uuid, Symbol>,
) -> Option<(f64, f64)> {
    let lib_comp = lib_comps.get(&inst.lib_component)?;
    let variant = lib_comp.variants.iter().find(|v| v.uuid == inst.lib_variant)?;
    let gate = variant.gates.first()?;
    let lib_sym = lib_syms.get(&gate.symbol)?;
    let mapping = gate.pin_mappings.iter().find(|m| m.signal == signal_uuid)?;
    let pin = lib_sym.pins.iter().find(|p| p.uuid == mapping.pin)?;
    Some((pin.position.x, pin.position.y))
}

fn has_signal_connection_to(
    comp_a: Uuid,
    comp_b: Uuid,
    circuit: &Circuit,
    net_members: &HashMap<Uuid, Vec<Uuid>>,
    power_nets: &HashSet<Uuid>,
) -> bool {
    let inst_a = circuit.components.iter().find(|c| c.uuid == comp_a);
    let Some(inst_a) = inst_a else { return false; };
    inst_a.signal_connections.iter()
        .filter_map(|c| c.net)
        .filter(|n| !power_nets.contains(n))
        .any(|n| {
            net_members.get(&n)
                .map(|m| m.contains(&comp_b))
                .unwrap_or(false)
        })
}

fn collect_pin_endpoints(
    circuit: &Circuit,
    net: &Net,
    sym_by_comp: &HashMap<Uuid, &SchematicSymbol>,
    lib_comps: &HashMap<Uuid, Component>,
    lib_syms: &HashMap<Uuid, Symbol>,
) -> Vec<(Uuid, Uuid, Position)> {
    let mut endpoints = Vec::new();

    for inst in &circuit.components {
        for conn in &inst.signal_connections {
            if conn.net != Some(net.uuid) { continue; }
            let Some(sch_sym) = sym_by_comp.get(&inst.uuid) else { continue };
            let Some(lib_comp) = lib_comps.get(&inst.lib_component) else { continue };
            let Some(variant) = lib_comp.variants.iter().find(|v| v.uuid == inst.lib_variant) else { continue };
            let Some(gate) = variant.gates.first() else { continue };
            let Some(lib_sym) = lib_syms.get(&gate.symbol) else { continue };
            let Some(mapping) = gate.pin_mappings.iter().find(|m| m.signal == conn.signal) else { continue };
            let Some(pin) = lib_sym.pins.iter().find(|p| p.uuid == mapping.pin) else { continue };

            let rot_rad = sch_sym.rotation.0.to_radians();
            let cos_r = rot_rad.cos();
            let sin_r = rot_rad.sin();
            let world_pos = Position::new(
                sch_sym.position.x + pin.position.x * cos_r - pin.position.y * sin_r,
                sch_sym.position.y + pin.position.x * sin_r + pin.position.y * cos_r,
            );

            endpoints.push((sch_sym.uuid, pin.uuid, world_pos));
        }
    }

    endpoints
}

// ===========================================================================
// Tidy helpers
// ===========================================================================

fn dedup_junctions(schematic: &mut Schematic) -> usize {
    let mut total = 0;
    for seg in &mut schematic.net_segments {
        let mut seen: HashMap<(i64, i64), Uuid> = HashMap::new();
        let mut remap: HashMap<Uuid, Uuid> = HashMap::new();

        for junc in &seg.junctions {
            let key = (
                (junc.position.x * 1000.0).round() as i64,
                (junc.position.y * 1000.0).round() as i64,
            );
            if let Some(&existing) = seen.get(&key) {
                remap.insert(junc.uuid, existing);
                total += 1;
            } else {
                seen.insert(key, junc.uuid);
            }
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
        if let Some(&new_uuid) = remap.get(junction) {
            *junction = new_uuid;
        }
    }
}

fn remove_zero_length_wires(schematic: &mut Schematic) -> usize {
    let mut total = 0;
    for seg in &mut schematic.net_segments {
        let before = seg.lines.len();
        seg.lines.retain(|line| line.from != line.to);
        total += before - seg.lines.len();
    }
    total
}

fn reset_all_fields(project: &Path, circuit: &Circuit, schematic: &mut Schematic) -> Result<usize> {
    let (lib_comps, lib_syms) = load_library_data(project, circuit)?;
    let mut count = 0;

    for sym in &mut schematic.symbols {
        let Some(inst) = circuit.components.iter().find(|c| c.uuid == sym.component) else { continue };
        let Some(lib_comp) = lib_comps.get(&inst.lib_component) else { continue };
        let Some(variant) = lib_comp.variants.iter().find(|v| v.uuid == inst.lib_variant) else { continue };
        let Some(gate) = variant.gates.iter().find(|g| g.uuid == sym.lib_gate).or_else(|| variant.gates.first()) else { continue };
        let Some(lib_sym) = lib_syms.get(&gate.symbol) else { continue };

        for template in &lib_sym.texts {
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
            } else {
                sym.texts.push(rebuilt);
            }
            count += 1;
        }
    }
    Ok(count)
}

fn snap_component_alignment(schematic: &mut Schematic) -> usize {
    let snap_threshold = GRID * 1.2; // ~3mm
    let mut snapped = 0;

    // Collect positions
    let positions: Vec<(usize, f64, f64)> = schematic.symbols.iter().enumerate()
        .map(|(i, s)| (i, s.position.x, s.position.y))
        .collect();

    // Snap Y: if two symbols are within threshold of same Y, align them
    for i in 0..positions.len() {
        for j in (i + 1)..positions.len() {
            let dy = (positions[i].2 - positions[j].2).abs();
            if dy > 0.01 && dy < snap_threshold {
                let avg_y = (positions[i].2 + positions[j].2) / 2.0;
                let snapped_y = (avg_y / GRID).round() * GRID;

                let idx_j = positions[j].0;
                let old_y = schematic.symbols[idx_j].position.y;
                let delta = snapped_y - old_y;
                schematic.symbols[idx_j].position.y = snapped_y;
                for text in &mut schematic.symbols[idx_j].texts {
                    text.position.y += delta;
                }
                snapped += 1;
            }
        }
    }

    snapped
}

fn spread_overlapping_labels(schematic: &mut Schematic) -> usize {
    let mut all_positions: Vec<(usize, usize, f64, f64)> = Vec::new();
    for (si, seg) in schematic.net_segments.iter().enumerate() {
        for (li, label) in seg.labels.iter().enumerate() {
            all_positions.push((si, li, label.position.x, label.position.y));
        }
    }

    let mut nudged = 0;
    let threshold = GRID * 2.0;

    for i in 0..all_positions.len() {
        for j in (i + 1)..all_positions.len() {
            let (_, _, x1, y1) = all_positions[i];
            let (si, li, x2, y2) = all_positions[j];
            let dist = ((x2 - x1).powi(2) + (y2 - y1).powi(2)).sqrt();
            if dist < threshold {
                let new_y = y2 + GRID * 2.0;
                all_positions[j].3 = new_y;
                schematic.net_segments[si].labels[li].position.y = new_y;
                nudged += 1;
            }
        }
    }
    nudged
}

fn compact_layout(schematic: &mut Schematic) -> bool {
    if schematic.symbols.is_empty() { return false; }

    // Find the bounding box of all elements
    let mut min_x = f64::INFINITY;
    let mut min_y = f64::INFINITY;

    for sym in &schematic.symbols {
        min_x = min_x.min(sym.position.x);
        min_y = min_y.min(sym.position.y);
    }
    for seg in &schematic.net_segments {
        for junc in &seg.junctions {
            min_x = min_x.min(junc.position.x);
            min_y = min_y.min(junc.position.y);
        }
    }

    // Target: top-left component at grid (6, 6) = 15.24mm
    let target = 6.0 * GRID;
    let dx = target - min_x;
    let dy = target - min_y;

    if dx.abs() < 0.01 && dy.abs() < 0.01 { return false; }

    // Shift everything
    for sym in &mut schematic.symbols {
        sym.position.x += dx;
        sym.position.y += dy;
        for text in &mut sym.texts {
            text.position.x += dx;
            text.position.y += dy;
        }
    }
    for seg in &mut schematic.net_segments {
        for junc in &mut seg.junctions {
            junc.position.x += dx;
            junc.position.y += dy;
        }
        for label in &mut seg.labels {
            label.position.x += dx;
            label.position.y += dy;
        }
    }

    true
}
