//! Schematic auto-placement and tidy algorithms (v2).
//!
//! Uses a Sugiyama-style layered graph drawing algorithm adapted for schematics:
//! 1. Analysis — classify nets and components, build signal flow DAG
//! 2. Ranking — assign components to columns by signal flow depth
//! 3. Ordering — minimize wire crossings within columns
//! 4. Orientation & Positioning — choose rotation, assign coordinates
//! 5. Companion attachment — place bypass caps, pull-ups near parent ICs
//! 6. Wiring & labels — route nets, place net labels
//! 7. Text fields — collision-aware NAME/VALUE placement
//! 8. Tidy — cleanup pass

pub mod types;
pub mod analysis;
pub mod ranking;
pub mod placement;
pub mod wiring;
pub mod text_fields;
pub mod tidy;

use std::collections::HashMap;
use std::path::Path;

use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Component, Symbol};
use volt_core::project::*;

use super::project_io::{self, Result};
use types::*;

// Disambiguate: use our autoplace NetClass enum, not the project struct.
use self::types::NetClass;

// ===========================================================================
// Public API
// ===========================================================================

pub fn autoplace_schematic(project: &Path, sch_name: &str) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let (lib_comps, lib_syms) = load_library_data(project, &circuit)?;

    // Phase 1: Analysis
    let net_members = analysis::build_net_members(&circuit);
    let net_classes = analysis::classify_nets(&circuit, &net_members);
    let comp_roles = analysis::classify_components(&circuit, &net_classes, &net_members, &lib_comps, &lib_syms);
    let flow_dag = analysis::build_flow_dag(&circuit, &net_classes, &net_members, &comp_roles, &lib_comps);
    let companions = analysis::detect_companions(&circuit, &comp_roles, &net_classes, &net_members, &lib_comps);
    let companion_set: std::collections::HashSet<Uuid> = companions.iter().map(|c| c.component).collect();

    // Phase 2: Ranking
    let main_components: Vec<Uuid> = circuit.components.iter()
        .map(|c| c.uuid)
        .filter(|u| !companion_set.contains(u))
        .collect();
    let ranks = ranking::assign_ranks(&main_components, &flow_dag, &comp_roles);

    // Phase 3: Ordering
    let rank_order = ranking::order_within_ranks(&ranks, &flow_dag, &net_members, &net_classes);

    // Phase 4: Orientation & Positioning
    let sym_extents = placement::compute_all_extents(&circuit, &lib_comps, &lib_syms);
    let pin_profiles = placement::compute_all_pin_profiles(&circuit, &lib_comps, &lib_syms);
    let mut placements = placement::assign_coordinates(
        &rank_order, &sym_extents, &flow_dag, &circuit, &lib_comps, &lib_syms,
        &net_members, &net_classes, &pin_profiles,
    );

    // Phase 5: Companion attachment
    let companion_placements = placement::place_companions(
        &companions, &placements, &sym_extents, &circuit, &lib_comps, &lib_syms,
    );
    for (uuid, pl) in &companion_placements {
        placements.insert(*uuid, *pl);
    }
    placement::resolve_overlaps(&mut placements, &sym_extents);

    // Build schematic symbols
    schematic.symbols.clear();
    schematic.net_segments.clear();

    let mut comp_to_sym: HashMap<Uuid, Uuid> = HashMap::new();
    for inst in &circuit.components {
        let Some(pl) = placements.get(&inst.uuid) else { continue };
        let Some(lib_comp) = lib_comps.get(&inst.lib_component) else { continue };
        let Some(variant) = lib_comp.variants.iter().find(|v| v.uuid == inst.lib_variant) else { continue };
        let Some(gate) = variant.gates.first() else { continue };
        let Some(lib_sym) = lib_syms.get(&gate.symbol) else { continue };

        let px = pl.gx * GRID;
        let py = pl.gy * GRID;
        let sym_uuid = new_uuid();
        comp_to_sym.insert(inst.uuid, sym_uuid);

        // Phase 7: Text fields (inline during symbol construction)
        let texts = text_fields::build_symbol_texts(
            px, py, pl.rotation, lib_sym, &inst.name, &inst.value,
        );

        schematic.symbols.push(SchematicSymbol {
            uuid: sym_uuid,
            component: inst.uuid,
            lib_gate: gate.uuid,
            position: Position::new(px, py),
            rotation: Angle(pl.rotation),
            mirror: pl.mirror,
            texts,
        });
    }

    // Phase 6: Wiring & labels
    let sym_by_comp: HashMap<Uuid, &SchematicSymbol> = schematic.symbols.iter()
        .map(|s| (s.component, s)).collect();
    let comp_boxes = compute_component_boxes(&schematic, &circuit, &lib_comps, &lib_syms);

    for net in &circuit.nets {
        let net_class = net_classes.get(&net.uuid).copied().unwrap_or(NetClass::Signal);
        let segment = wiring::route_net(
            &circuit, net, net_class, &sym_by_comp, &lib_comps, &lib_syms, &comp_boxes,
        );
        if let Some(seg) = segment {
            schematic.net_segments.push(seg);
        }
    }

    // Phase 7b: Resolve text field collisions
    text_fields::resolve_collisions(&mut schematic, &comp_boxes);

    // Phase 8: Final tidy pass
    tidy::tidy_pass(&mut schematic, &circuit, &lib_comps, &lib_syms, project)?;

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

pub fn tidy_schematic(project: &Path, sch_name: &str) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let (lib_comps, lib_syms) = load_library_data(project, &circuit)?;

    let changes = tidy::tidy_pass(&mut schematic, &circuit, &lib_comps, &lib_syms, project)?;

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

pub(crate) fn load_library_data(
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
            boxes.push(BBox::new(min_x - 1.0, min_y - 1.0, max_x + 1.0, max_y + 1.0));
        }
    }
    boxes
}
