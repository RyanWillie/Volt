//! `volt-eda net` subcommands.

use std::path::PathBuf;

use clap::Subcommand;

use volt_core::common::*;
use volt_core::library::{Component, Symbol, SymbolPin};
use volt_core::project::*;

use super::project_io::{self, Result};

#[derive(Subcommand)]
pub enum NetCommands {
    /// Create a new net
    Add {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Net name (e.g. "VCC", "GND", "SDA")
        #[arg(long)]
        name: String,
    },
    /// Connect a component signal to a net
    Connect {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Component designator (e.g. "R1")
        #[arg(long)]
        component: String,
        /// Pin/signal name on the component (e.g. "1", "2")
        #[arg(long)]
        pin: String,
        /// Net name to connect to
        #[arg(long)]
        net: String,
    },
    /// List all nets in the circuit
    List {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
    },
}

pub fn net_command(cmd: NetCommands) -> Result<()> {
    match cmd {
        NetCommands::Add { project, name } => net_add(&project, &name),
        NetCommands::Connect {
            project,
            component,
            pin,
            net,
        } => net_connect(&project, &component, &pin, &net),
        NetCommands::List { project } => net_list(&project),
    }
}

fn net_add(project: &std::path::Path, name: &str) -> Result<()> {
    project_io::ensure_project(project)?;
    let mut circuit = project_io::read_circuit(project)?;

    // Check for duplicate name
    if circuit.nets.iter().any(|n| n.name == name) {
        return Err(format!("Net '{name}' already exists").into());
    }

    // Use the default net class
    let default_nc = circuit
        .net_classes
        .first()
        .ok_or("No net classes found — project may be corrupted")?;

    let net_uuid = new_uuid();
    let net = Net {
        uuid: net_uuid,
        name: name.to_string(),
        auto_name: false,
        net_class: default_nc.uuid,
    };

    circuit.nets.push(net);
    project_io::write_circuit(project, &circuit)?;

    let result = serde_json::json!({
        "status": "ok",
        "net": {
            "uuid": net_uuid.to_string(),
            "name": name,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn net_connect(
    project: &std::path::Path,
    component_name: &str,
    pin_name: &str,
    net_name: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let mut circuit = project_io::read_circuit(project)?;

    // Find the net
    let net_uuid = circuit
        .nets
        .iter()
        .find(|n| n.name == net_name)
        .map(|n| n.uuid)
        .ok_or_else(|| format!("Net '{net_name}' not found"))?;

    // Find the component instance index so we can resolve aliases before mutably borrowing it
    let comp_index = circuit
        .components
        .iter()
        .position(|c| c.name == component_name)
        .ok_or_else(|| format!("Component '{component_name}' not found"))?;

    let lib_component_uuid = circuit.components[comp_index].lib_component;
    let lib_variant_uuid = circuit.components[comp_index].lib_variant;
    let lib_comp: Component =
        project_io::read_library_element(project, "components", &lib_component_uuid)?;

    let (signal_uuid, resolved_pin) = resolve_signal_selector(
        project,
        &lib_comp,
        lib_variant_uuid,
        pin_name,
        component_name,
    )?;

    let comp_instance = &mut circuit.components[comp_index];

    // Find or create the signal connection
    let mut found = false;
    for conn in &mut comp_instance.signal_connections {
        if conn.signal == signal_uuid {
            conn.net = Some(net_uuid);
            found = true;
            break;
        }
    }
    if !found {
        comp_instance.signal_connections.push(SignalConnection {
            signal: signal_uuid,
            net: Some(net_uuid),
        });
    }

    project_io::write_circuit(project, &circuit)?;

    let result = serde_json::json!({
        "status": "ok",
        "connection": {
            "component": component_name,
            "pin": pin_name,
            "resolved_pin": resolved_pin,
            "signal_uuid": signal_uuid.to_string(),
            "net": net_name,
            "net_uuid": net_uuid.to_string(),
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn resolve_signal_selector(
    project: &std::path::Path,
    lib_comp: &Component,
    lib_variant_uuid: uuid::Uuid,
    selector: &str,
    component_name: &str,
) -> Result<(uuid::Uuid, String)> {
    if let Some(signal) = lib_comp.signals.iter().find(|s| s.name == selector) {
        return Ok((signal.uuid, signal.name.clone()));
    }

    let variant = lib_comp
        .variants
        .iter()
        .find(|v| v.uuid == lib_variant_uuid)
        .ok_or_else(|| format!("Variant not found for component '{component_name}'"))?;
    let gate = variant
        .gates
        .first()
        .ok_or_else(|| format!("No gates defined for component '{component_name}'"))?;
    let lib_sym: Symbol = project_io::read_library_element(project, "symbols", &gate.symbol)?;
    let pin = find_symbol_pin(&lib_sym, selector).map_err(|e| format!("{e} on component '{component_name}'"))?;
    let mapping = gate
        .pin_mappings
        .iter()
        .find(|m| m.pin == pin.uuid)
        .ok_or_else(|| format!("No signal mapping found for pin '{}' on component '{component_name}'", pin.name))?;
    let signal = lib_comp
        .signals
        .iter()
        .find(|s| s.uuid == mapping.signal)
        .ok_or_else(|| format!("Mapped signal missing for component '{component_name}'"))?;
    Ok((signal.uuid, format_pin_display(pin)))
}

fn find_symbol_pin<'a>(lib_sym: &'a Symbol, selector: &str) -> std::result::Result<&'a SymbolPin, String> {
    if let Some(pin) = lib_sym.pins.iter().find(|p| p.name == selector) {
        return Ok(pin);
    }

    let alias_matches: Vec<&SymbolPin> = lib_sym
        .pins
        .iter()
        .filter(|p| !p.pin_name.is_empty() && p.pin_name.eq_ignore_ascii_case(selector))
        .collect();

    match alias_matches.len() {
        1 => Ok(alias_matches[0]),
        0 => Err(format!(
            "Pin '{selector}' not found. Available: {}",
            lib_sym
                .pins
                .iter()
                .map(format_pin_display)
                .collect::<Vec<_>>()
                .join(", ")
        )),
        _ => Err(format!(
            "Pin name '{selector}' is ambiguous. Matches: {}",
            alias_matches
                .iter()
                .map(|p| format_pin_display(p))
                .collect::<Vec<_>>()
                .join(", ")
        )),
    }
}

fn format_pin_display(pin: &SymbolPin) -> String {
    if pin.pin_name.is_empty() {
        pin.name.clone()
    } else {
        format!("{} ({})", pin.name, pin.pin_name)
    }
}

fn net_list(project: &std::path::Path) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;

    let nets: Vec<serde_json::Value> = circuit
        .nets
        .iter()
        .map(|n| {
            // Count connections to this net
            let connection_count: usize = circuit
                .components
                .iter()
                .flat_map(|c| &c.signal_connections)
                .filter(|sc| sc.net == Some(n.uuid))
                .count();

            serde_json::json!({
                "uuid": n.uuid.to_string(),
                "name": n.name,
                "auto_name": n.auto_name,
                "connections": connection_count,
            })
        })
        .collect();

    let result = serde_json::json!({
        "status": "ok",
        "nets": nets,
        "count": nets.len(),
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}
