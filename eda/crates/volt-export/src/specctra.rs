//! Specctra DSN export for external autorouting (e.g. FreeRouting).
//!
//! Generates a minimal Specctra Design Session (.dsn) file containing:
//! - PCB boundary
//! - Layer stack
//! - Component placements
//! - Padstacks (via definitions)
//! - Network (nets with pin references)
//!
//! The DSN format is a public S-expression format originally from Cadence.

use std::collections::HashMap;
use std::fmt::Write;

use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Device, Package};
use volt_core::project::*;

use crate::gerber::{BoardLibrary, transform_point};

/// Export a Specctra DSN file.
pub fn export_dsn(
    board: &Board,
    circuit: &Circuit,
    library: &dyn BoardLibrary,
) -> String {
    let mut out = String::with_capacity(8192);

    writeln!(out, "(pcb \"{}\"", board.name).unwrap();
    writeln!(out, "  (parser").unwrap();
    writeln!(out, "    (string_quote \")").unwrap();
    writeln!(out, "    (host_cad \"Volt EDA\")").unwrap();
    writeln!(out, "    (host_version \"0.1\")").unwrap();
    writeln!(out, "  )").unwrap();

    // Resolution
    writeln!(out, "  (resolution mm 1000)").unwrap();

    // Layer stack
    write_layer_stack(&mut out, board);

    // Board boundary
    write_boundary(&mut out, board);

    // Padstack for default via
    write_via_padstack(&mut out, board);

    // Component placements
    let comp_names = build_comp_names(circuit);
    write_placements(&mut out, board, circuit, library, &comp_names);

    // Network
    write_network(&mut out, board, circuit, library, &comp_names);

    // Wiring (existing traces, if any)
    write_wiring(&mut out, board, circuit);

    writeln!(out, ")").unwrap();
    out
}

fn write_layer_stack(out: &mut String, board: &Board) {
    writeln!(out, "  (structure").unwrap();

    // Layers
    let mut layers = vec!["F.Cu".to_string()];
    for i in 1..=board.inner_layers {
        layers.push(format!("In{}.Cu", i));
    }
    layers.push("B.Cu".to_string());

    for (i, name) in layers.iter().enumerate() {
        writeln!(out, "    (layer \"{}\"", name).unwrap();
        writeln!(out, "      (type signal)").unwrap();
        writeln!(out, "      (property").unwrap();
        writeln!(out, "        (index {}))", i).unwrap();
        writeln!(out, "    )").unwrap();
    }

    // Via rule
    writeln!(out, "    (via \"Default_Via\")").unwrap();

    // Design rules
    let clearance = board.drc_settings.min_copper_copper_clearance;
    let trace_width = board.design_rules.default_trace_width;
    writeln!(out, "    (rule").unwrap();
    writeln!(out, "      (width {:.4})", trace_width).unwrap();
    writeln!(out, "      (clearance {:.4})", clearance).unwrap();
    writeln!(out, "    )").unwrap();

    writeln!(out, "  )").unwrap();
}

fn write_boundary(out: &mut String, board: &Board) {
    let outline = board
        .polygons
        .iter()
        .find(|p| p.layer == Layer::BoardOutlines);

    if let Some(poly) = outline {
        write!(out, "  (boundary (path pcb 0").unwrap();
        for v in &poly.vertices {
            write!(out, " {:.4} {:.4}", v.position.x, v.position.y).unwrap();
        }
        // Close
        if let Some(first) = poly.vertices.first() {
            write!(out, " {:.4} {:.4}", first.position.x, first.position.y).unwrap();
        }
        writeln!(out, "))").unwrap();
    }
}

fn write_via_padstack(out: &mut String, board: &Board) {
    let drill = board.design_rules.default_via_drill_diameter;
    let outer = drill + 0.5; // annular ring
    writeln!(out, "  (library").unwrap();
    writeln!(out, "    (padstack \"Default_Via\"").unwrap();
    writeln!(out, "      (shape (circle \"F.Cu\" {:.4}))", outer).unwrap();
    writeln!(out, "      (shape (circle \"B.Cu\" {:.4}))", outer).unwrap();
    writeln!(out, "      (attach off)").unwrap();
    writeln!(out, "    )").unwrap();
    writeln!(out, "  )").unwrap();
}

fn write_placements(
    out: &mut String,
    board: &Board,
    circuit: &Circuit,
    library: &dyn BoardLibrary,
    comp_names: &HashMap<Uuid, String>,
) {
    writeln!(out, "  (placement").unwrap();
    for dev in &board.devices {
        let name = comp_names.get(&dev.component).map(|s| s.as_str()).unwrap_or("?");
        let side = if dev.flip { "back" } else { "front" };
        writeln!(
            out,
            "    (component \"{}\" (place \"{}\" {:.4} {:.4} {} {:.1}))",
            name, name, dev.position.x, dev.position.y, side, dev.rotation.0
        ).unwrap();
    }
    writeln!(out, "  )").unwrap();
}

fn write_network(
    out: &mut String,
    board: &Board,
    circuit: &Circuit,
    library: &dyn BoardLibrary,
    comp_names: &HashMap<Uuid, String>,
) {
    writeln!(out, "  (network").unwrap();

    // Build pad→net map
    let pad_net_map = build_pad_net_map(board, circuit, library);
    let net_names: HashMap<Uuid, String> = circuit
        .nets
        .iter()
        .map(|n| (n.uuid, n.name.clone()))
        .collect();

    // Group pads by net
    let mut net_pins: HashMap<Uuid, Vec<String>> = HashMap::new();

    for dev in &board.devices {
        let comp_name = comp_names.get(&dev.component).map(|s| s.as_str()).unwrap_or("?");
        let Some(lib_dev) = library.get_device(&dev.lib_device) else { continue };
        let Some(pkg) = library.get_package(&lib_dev.package) else { continue };
        let Some(fp) = pkg.footprints.iter().find(|f| f.uuid == dev.lib_footprint)
            .or_else(|| pkg.footprints.first()) else { continue };

        for fp_pad in &fp.pads {
            if let Some(&net_uuid) = pad_net_map.get(&(dev.component, fp_pad.uuid)) {
                let pad_name = pkg.pads.iter()
                    .find(|p| p.uuid == fp_pad.package_pad)
                    .map(|p| p.name.as_str())
                    .unwrap_or("?");
                net_pins.entry(net_uuid).or_default().push(
                    format!("\"{}\"-\"{}\"", comp_name, pad_name)
                );
            }
        }
    }

    for (net_uuid, pins) in &net_pins {
        let net_name = net_names.get(net_uuid).map(|s| s.as_str()).unwrap_or("N/C");
        writeln!(out, "    (net \"{}\"", net_name).unwrap();
        write!(out, "      (pins").unwrap();
        for pin in pins {
            write!(out, " {}", pin).unwrap();
        }
        writeln!(out, ")").unwrap();
        writeln!(out, "    )").unwrap();
    }

    writeln!(out, "  )").unwrap();
}

fn write_wiring(out: &mut String, board: &Board, circuit: &Circuit) {
    let net_names: HashMap<Uuid, String> = circuit
        .nets
        .iter()
        .map(|n| (n.uuid, n.name.clone()))
        .collect();

    writeln!(out, "  (wiring").unwrap();
    // Existing traces could be exported here for partial routing
    // For now, export empty wiring section
    writeln!(out, "  )").unwrap();
}

fn build_comp_names(circuit: &Circuit) -> HashMap<Uuid, String> {
    circuit
        .components
        .iter()
        .map(|c| (c.uuid, c.name.clone()))
        .collect()
}

fn build_pad_net_map(
    board: &Board,
    circuit: &Circuit,
    library: &dyn BoardLibrary,
) -> HashMap<(Uuid, Uuid), Uuid> {
    let mut map = HashMap::new();
    for dev in &board.devices {
        let Some(device_lib) = library.get_device(&dev.lib_device) else { continue };
        let Some(package) = library.get_package(&device_lib.package) else { continue };
        let comp = circuit.components.iter().find(|c| c.uuid == dev.component);
        let Some(comp) = comp else { continue };
        let footprint = package.footprints.iter()
            .find(|f| f.uuid == dev.lib_footprint)
            .or_else(|| package.footprints.first());
        let Some(footprint) = footprint else { continue };
        for fp_pad in &footprint.pads {
            let signal = device_lib.pad_mappings.iter()
                .find(|pm| pm.pad == fp_pad.package_pad)
                .map(|pm| pm.signal);
            if let Some(sig) = signal {
                let net = comp.signal_connections.iter()
                    .find(|sc| sc.signal == sig)
                    .and_then(|sc| sc.net);
                if let Some(net_uuid) = net {
                    map.insert((dev.component, fp_pad.uuid), net_uuid);
                }
            }
        }
    }
    map
}
