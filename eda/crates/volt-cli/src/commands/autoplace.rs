//! Schematic auto-placement and tidy algorithms.
//!
//! `autoplace` — generates an initial layout from a connected circuit.
//! `tidy` — cleans up an existing schematic layout.

use std::collections::{HashMap, HashSet};
use std::path::Path;

use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Component, Symbol, SymbolText};
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

    // Load all library data we need
    let mut lib_comps: HashMap<Uuid, Component> = HashMap::new();
    let mut lib_syms: HashMap<Uuid, Symbol> = HashMap::new();

    for inst in &circuit.components {
        if !lib_comps.contains_key(&inst.lib_component) {
            if let Ok(c) = project_io::read_library_element::<Component>(
                project, "components", &inst.lib_component,
            ) {
                let variant = c.variants.iter().find(|v| v.uuid == inst.lib_variant);
                if let Some(variant) = variant {
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
    }

    // Build connectivity: net_uuid → [component_uuid]
    let mut net_members: HashMap<Uuid, Vec<Uuid>> = HashMap::new();
    for inst in &circuit.components {
        for conn in &inst.signal_connections {
            if let Some(net_uuid) = conn.net {
                net_members.entry(net_uuid).or_default().push(inst.uuid);
            }
        }
    }

    // Score components by connection degree
    let mut comp_degree: HashMap<Uuid, usize> = HashMap::new();
    for inst in &circuit.components {
        let degree: usize = inst.signal_connections.iter()
            .filter_map(|c| c.net)
            .flat_map(|n| net_members.get(&n).into_iter().flatten())
            .filter(|&&c| c != inst.uuid)
            .collect::<HashSet<_>>()
            .len();
        comp_degree.insert(inst.uuid, degree);
    }

    // Sort: highest degree first (ICs before passives)
    let mut placement_order: Vec<&ComponentInstance> = circuit.components.iter().collect();
    placement_order.sort_by(|a, b| {
        comp_degree.get(&b.uuid).unwrap_or(&0)
            .cmp(comp_degree.get(&a.uuid).unwrap_or(&0))
    });

    // Compute bounding boxes for each symbol
    let mut sym_bounds: HashMap<Uuid, (f64, f64)> = HashMap::new(); // symbol_uuid → (half_w, half_h)
    for (uuid, sym) in &lib_syms {
        let (hw, hh) = symbol_half_extents(sym);
        sym_bounds.insert(*uuid, (hw, hh));
    }

    // Occupied grid cells: position → component_uuid
    let mut occupied: HashMap<(i32, i32), Uuid> = HashMap::new();
    let mut placements: HashMap<Uuid, (f64, f64, f64)> = HashMap::new(); // comp_uuid → (gx, gy, rotation)

    // Detect power net names
    let power_nets: HashSet<Uuid> = circuit.nets.iter()
        .filter(|n| is_power_net_name(&n.name))
        .map(|n| n.uuid)
        .collect();

    // Place components
    let center_x = 16.0_f64;
    let center_y = 14.0_f64;

    for inst in &placement_order {
        let lib_comp = match lib_comps.get(&inst.lib_component) {
            Some(c) => c,
            None => continue,
        };
        let variant = match lib_comp.variants.iter().find(|v| v.uuid == inst.lib_variant) {
            Some(v) => v,
            None => continue,
        };
        let gate = match variant.gates.first() {
            Some(g) => g,
            None => continue,
        };
        let sym = match lib_syms.get(&gate.symbol) {
            Some(s) => s,
            None => continue,
        };
        let (hw, hh) = sym_bounds.get(&gate.symbol).copied().unwrap_or((4.0, 4.0));

        if placements.is_empty() {
            // First component (highest degree) goes at center
            let gx = center_x;
            let gy = center_y;
            occupy_cells(&mut occupied, gx, gy, hw, hh, inst.uuid);
            placements.insert(inst.uuid, (gx, gy, 0.0));
            continue;
        }

        // Find the best anchor: placed component sharing the most non-power nets
        let mut best_anchor: Option<(Uuid, usize, Uuid)> = None; // (anchor_uuid, shared_count, connecting_net)
        for conn in &inst.signal_connections {
            let Some(net_uuid) = conn.net else { continue };
            if power_nets.contains(&net_uuid) { continue; }
            if let Some(members) = net_members.get(&net_uuid) {
                for &member in members {
                    if member == inst.uuid { continue; }
                    if !placements.contains_key(&member) { continue; }
                    let count = best_anchor.map(|(_, c, _)| c).unwrap_or(0);
                    // Count shared nets between this inst and this anchor
                    let shared = count_shared_nets(inst, member, &net_members, &power_nets);
                    if shared > count {
                        best_anchor = Some((member, shared, net_uuid));
                    }
                }
            }
        }

        let (target_gx, target_gy) = if let Some((anchor_uuid, _, connecting_net)) = best_anchor {
            let (ax, ay, _arot) = placements[&anchor_uuid];
            let anchor_inst = circuit.components.iter().find(|c| c.uuid == anchor_uuid).unwrap();
            let anchor_comp = lib_comps.get(&anchor_inst.lib_component).unwrap();
            let anchor_variant = anchor_comp.variants.iter().find(|v| v.uuid == anchor_inst.lib_variant).unwrap();
            let anchor_gate = anchor_variant.gates.first().unwrap();
            let anchor_sym = lib_syms.get(&anchor_gate.symbol).unwrap();

            // Find the pin on the anchor that connects via this net
            let direction = find_pin_direction(
                anchor_inst, anchor_comp, anchor_sym, anchor_gate, connecting_net,
            );

            let (ahw, ahh) = sym_bounds.get(&anchor_gate.symbol).copied().unwrap_or((4.0, 4.0));
            let spacing_x = ahw + hw + 3.0;
            let spacing_y = ahh + hh + 3.0;

            match direction {
                Direction::Right => (ax + spacing_x, ay),
                Direction::Left  => (ax - spacing_x, ay),
                Direction::Down  => (ax, ay + spacing_y),
                Direction::Up    => (ax, ay - spacing_y),
            }
        } else {
            // No anchor found, place below the last placed component
            let max_y = placements.values().map(|(_, y, _)| *y).fold(f64::NEG_INFINITY, f64::max);
            (center_x, max_y + hh * 2.0 + 4.0)
        };

        // Find nearest free position
        let (gx, gy) = find_free_position(&occupied, target_gx, target_gy, hw, hh);
        occupy_cells(&mut occupied, gx, gy, hw, hh, inst.uuid);
        placements.insert(inst.uuid, (gx, gy, 0.0));
    }

    // Clear existing symbols and net segments
    schematic.symbols.clear();
    schematic.net_segments.clear();

    // Create schematic symbols
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

    // Build a lookup from component_uuid → schematic symbol
    let sym_by_comp: HashMap<Uuid, &SchematicSymbol> = schematic.symbols.iter()
        .map(|s| (s.component, s))
        .collect();

    // Generate wires for each net
    let net_name_map: HashMap<Uuid, &str> = circuit.nets.iter().map(|n| (n.uuid, n.name.as_str())).collect();

    for net in &circuit.nets {
        // Collect all pin endpoints for this net
        let mut pin_endpoints: Vec<(Uuid, Uuid, Position)> = Vec::new(); // (sch_sym_uuid, pin_uuid, world_pos)

        for inst in &circuit.components {
            for conn in &inst.signal_connections {
                if conn.net != Some(net.uuid) { continue; }
                let Some(sch_sym) = sym_by_comp.get(&inst.uuid) else { continue };
                let lib_comp = match lib_comps.get(&inst.lib_component) { Some(c) => c, None => continue };
                let variant = match lib_comp.variants.iter().find(|v| v.uuid == inst.lib_variant) { Some(v) => v, None => continue };
                let gate = match variant.gates.first() { Some(g) => g, None => continue };
                let lib_sym = match lib_syms.get(&gate.symbol) { Some(s) => s, None => continue };

                // Find which pin maps to this signal
                let Some(mapping) = gate.pin_mappings.iter().find(|m| m.signal == conn.signal) else { continue };
                let Some(pin) = lib_sym.pins.iter().find(|p| p.uuid == mapping.pin) else { continue };

                let rot_rad = sch_sym.rotation.0.to_radians();
                let cos_r = rot_rad.cos();
                let sin_r = rot_rad.sin();
                let world_pos = Position::new(
                    sch_sym.position.x + pin.position.x * cos_r - pin.position.y * sin_r,
                    sch_sym.position.y + pin.position.x * sin_r + pin.position.y * cos_r,
                );

                pin_endpoints.push((sch_sym.uuid, pin.uuid, world_pos));
            }
        }

        if pin_endpoints.is_empty() { continue; }

        let mut seg = SchematicNetSegment {
            uuid: new_uuid(),
            net: net.uuid,
            junctions: vec![],
            lines: vec![],
            labels: vec![],
        };

        let is_power = power_nets.contains(&net.uuid);

        if is_power || pin_endpoints.len() > 4 {
            // High-fanout net: use labels at each pin instead of wiring everything
            for (_, _, pos) in &pin_endpoints {
                // Add a short stub + label
                let label_pos = Position::new(
                    (pos.x / GRID).round() * GRID,
                    (pos.y / GRID - 1.0).round() * GRID,
                );
                seg.labels.push(NetLabel {
                    uuid: new_uuid(),
                    position: label_pos,
                    rotation: Angle(0.0),
                    mirror: false,
                });
            }
        } else {
            // Sort by x then y for a consistent chain
            let mut sorted = pin_endpoints.clone();
            sorted.sort_by(|a, b| {
                a.2.x.partial_cmp(&b.2.x).unwrap()
                    .then(a.2.y.partial_cmp(&b.2.y).unwrap())
            });

            // Chain: wire pin[0]→pin[1]→pin[2]→...
            for window in sorted.windows(2) {
                let (sym_a, pin_a, pos_a) = &window[0];
                let (sym_b, pin_b, pos_b) = &window[1];

                let from = LineEndpoint::Symbol { symbol: *sym_a, pin: *pin_a };
                let to = LineEndpoint::Symbol { symbol: *sym_b, pin: *pin_b };

                let dx = (pos_b.x - pos_a.x).abs();
                let dy = (pos_b.y - pos_a.y).abs();

                if dx < 0.01 || dy < 0.01 {
                    // Already aligned — direct wire
                    seg.lines.push(SchematicLine {
                        uuid: new_uuid(),
                        width: 0.15875,
                        from,
                        to,
                    });
                } else {
                    // Manhattan: create a bend junction
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
                        uuid: new_uuid(),
                        width: 0.15875,
                        from,
                        to: bend_ep.clone(),
                    });
                    seg.lines.push(SchematicLine {
                        uuid: new_uuid(),
                        width: 0.15875,
                        from: bend_ep,
                        to,
                    });
                }
            }

            // Add a label for nets with ≥ 3 connections
            if sorted.len() >= 3 {
                let mid = &sorted[sorted.len() / 2].2;
                seg.labels.push(NetLabel {
                    uuid: new_uuid(),
                    position: Position::new(
                        (mid.x / GRID).round() * GRID,
                        (mid.y / GRID - 1.5).round() * GRID,
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

    // 1. Deduplicate junctions at the same position
    let deduped = dedup_junctions(&mut schematic);
    if deduped > 0 {
        changes.push(format!("Deduplicated {deduped} junctions"));
    }

    // 2. Remove zero-length wire segments
    let removed = remove_zero_length_wires(&mut schematic);
    if removed > 0 {
        changes.push(format!("Removed {removed} zero-length wire segments"));
    }

    // 3. Reset all field positions to library defaults
    let reset = reset_all_fields(project, &circuit, &mut schematic)?;
    if reset > 0 {
        changes.push(format!("Reset {reset} text field positions"));
    }

    // 4. Spread overlapping labels
    let spread = spread_overlapping_labels(&mut schematic);
    if spread > 0 {
        changes.push(format!("Spread {spread} overlapping labels"));
    }

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "tidy": {
            "changes": changes,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// Autoplace helpers
// ===========================================================================

#[derive(Debug, Clone, Copy)]
enum Direction { Left, Right, Up, Down }

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

fn is_power_net_name(name: &str) -> bool {
    let upper = name.to_uppercase();
    matches!(upper.as_str(),
        "VCC" | "VDD" | "V+" | "VBUS" | "VBAT" | "VIN" | "VOUT" |
        "GND" | "VSS" | "V-" | "AGND" | "DGND" | "PGND" | "GNDA" |
        "3V3" | "+3V3" | "+3.3V" | "5V" | "+5V" | "+12V" | "+24V" |
        "BAT_RAW"
    )
}

fn occupy_cells(occupied: &mut HashMap<(i32, i32), Uuid>, gx: f64, gy: f64, hw: f64, hh: f64, uuid: Uuid) {
    let x0 = (gx - hw).floor() as i32;
    let x1 = (gx + hw).ceil() as i32;
    let y0 = (gy - hh).floor() as i32;
    let y1 = (gy + hh).ceil() as i32;
    for x in x0..=x1 {
        for y in y0..=y1 {
            occupied.insert((x, y), uuid);
        }
    }
}

fn is_position_free(occupied: &HashMap<(i32, i32), Uuid>, gx: f64, gy: f64, hw: f64, hh: f64) -> bool {
    let x0 = (gx - hw).floor() as i32;
    let x1 = (gx + hw).ceil() as i32;
    let y0 = (gy - hh).floor() as i32;
    let y1 = (gy + hh).ceil() as i32;
    for x in x0..=x1 {
        for y in y0..=y1 {
            if occupied.contains_key(&(x, y)) {
                return false;
            }
        }
    }
    true
}

fn find_free_position(
    occupied: &HashMap<(i32, i32), Uuid>,
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
    // Spiral outward
    for radius in 1..30 {
        let r = radius as f64;
        for &(dx, dy) in &[
            (r, 0.0), (-r, 0.0), (0.0, r), (0.0, -r),
            (r, r), (r, -r), (-r, r), (-r, -r),
            (r * 2.0, 0.0), (-r * 2.0, 0.0), (0.0, r * 2.0), (0.0, -r * 2.0),
        ] {
            let cx = tx + dx;
            let cy = ty + dy;
            if is_position_free(occupied, cx, cy, hw, hh) {
                return (cx, cy);
            }
        }
    }
    // Fallback: just offset far enough
    (tx + 20.0, ty)
}

fn count_shared_nets(
    a: &ComponentInstance,
    b_uuid: Uuid,
    net_members: &HashMap<Uuid, Vec<Uuid>>,
    power_nets: &HashSet<Uuid>,
) -> usize {
    let mut count = 0;
    for conn in &a.signal_connections {
        let Some(net) = conn.net else { continue };
        if power_nets.contains(&net) { continue; }
        if let Some(members) = net_members.get(&net) {
            if members.contains(&b_uuid) {
                count += 1;
            }
        }
    }
    count
}

fn find_pin_direction(
    inst: &ComponentInstance,
    _lib_comp: &Component,
    lib_sym: &Symbol,
    gate: &volt_core::library::Gate,
    net_uuid: Uuid,
) -> Direction {
    // Find the signal connected to this net
    let signal_uuid = inst.signal_connections.iter()
        .find(|c| c.net == Some(net_uuid))
        .map(|c| c.signal);

    if let Some(signal_uuid) = signal_uuid {
        // Find the pin mapped to this signal
        if let Some(mapping) = gate.pin_mappings.iter().find(|m| m.signal == signal_uuid) {
            if let Some(pin) = lib_sym.pins.iter().find(|p| p.uuid == mapping.pin) {
                // Direction based on pin position relative to center
                let px = pin.position.x;
                let py = pin.position.y;
                if px.abs() > py.abs() {
                    if px > 0.0 { return Direction::Right; }
                    else { return Direction::Left; }
                } else {
                    if py > 0.0 { return Direction::Down; }
                    else { return Direction::Up; }
                }
            }
        }
    }
    Direction::Right // default
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

        // Remove duplicated junctions
        seg.junctions.retain(|j| !remap.contains_key(&j.uuid));

        // Remap line endpoints
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

fn reset_all_fields(
    project: &Path,
    circuit: &Circuit,
    schematic: &mut Schematic,
) -> Result<usize> {
    let mut count = 0;
    for sym in &mut schematic.symbols {
        let Some(inst) = circuit.components.iter().find(|c| c.uuid == sym.component) else { continue };
        let Ok(lib_comp) = project_io::read_library_element::<Component>(project, "components", &inst.lib_component) else { continue };
        let Some(variant) = lib_comp.variants.iter().find(|v| v.uuid == inst.lib_variant) else { continue };
        let Some(gate) = variant.gates.iter().find(|g| g.uuid == sym.lib_gate).or_else(|| variant.gates.first()) else { continue };
        let Ok(lib_sym) = project_io::read_library_element::<Symbol>(project, "symbols", &gate.symbol) else { continue };

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

fn spread_overlapping_labels(schematic: &mut Schematic) -> usize {
    // Collect all label positions, nudge overlapping ones
    let mut all_label_positions: Vec<(usize, usize, f64, f64)> = Vec::new(); // (seg_idx, label_idx, x, y)

    for (si, seg) in schematic.net_segments.iter().enumerate() {
        for (li, label) in seg.labels.iter().enumerate() {
            all_label_positions.push((si, li, label.position.x, label.position.y));
        }
    }

    let mut nudged = 0;
    let threshold = GRID * 1.5;

    for i in 0..all_label_positions.len() {
        for j in (i + 1)..all_label_positions.len() {
            let (_, _, x1, y1) = all_label_positions[i];
            let (si, li, x2, y2) = all_label_positions[j];
            let dist = ((x2 - x1).powi(2) + (y2 - y1).powi(2)).sqrt();
            if dist < threshold {
                // Nudge the second label down
                let new_y = y2 + GRID * 2.0;
                all_label_positions[j].3 = new_y;
                schematic.net_segments[si].labels[li].position.y = new_y;
                nudged += 1;
            }
        }
    }
    nudged
}
