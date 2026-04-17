//! `volt-eda net` subcommands.

use std::path::PathBuf;

use clap::Subcommand;
use uuid::Uuid;

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
        /// Net scope: global or local
        #[arg(long, default_value = "global")]
        scope: String,
        /// Owning schematic name for local nets
        #[arg(long)]
        schematic: Option<String>,
        /// Mark this as a power net
        #[arg(long, default_value = "false")]
        power: bool,
    },
    /// Connect a component signal to a net
    Connect {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Component designator (e.g. "R1")
        #[arg(long)]
        component: String,
        /// Pin/signal name on the component (e.g. "1", "2", or gate-qualified "A:OUT")
        #[arg(long)]
        pin: String,
        /// Net name to connect to
        #[arg(long)]
        net: String,
        /// Schematic context for local nets
        #[arg(long)]
        schematic: Option<String>,
    },
    /// List all nets in the circuit
    List {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
    },
    /// Define a differential pair between two nets
    DiffPair {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Pair name (e.g. "USB_D")
        #[arg(long)]
        name: String,
        /// Positive net name
        #[arg(long)]
        positive: String,
        /// Negative net name
        #[arg(long)]
        negative: String,
        /// Maximum allowed length delta in mm
        #[arg(long)]
        max_delta: Option<f64>,
        /// Target differential impedance in ohms
        #[arg(long)]
        impedance: Option<f64>,
    },
}

pub fn net_command(cmd: NetCommands) -> Result<()> {
    match cmd {
        NetCommands::Add {
            project,
            name,
            scope,
            schematic,
            power,
        } => net_add(&project, &name, &scope, schematic.as_deref(), power),
        NetCommands::Connect {
            project,
            component,
            pin,
            net,
            schematic,
        } => net_connect(&project, &component, &pin, &net, schematic.as_deref()),
        NetCommands::List { project } => net_list(&project),
        NetCommands::DiffPair {
            project,
            name,
            positive,
            negative,
            max_delta,
            impedance,
        } => net_diff_pair(&project, &name, &positive, &negative, max_delta, impedance),
    }
}

pub(crate) fn resolve_schematic_uuid(project: &std::path::Path, schematic: &str) -> Result<Uuid> {
    Ok(project_io::read_schematic(project, schematic)?.uuid)
}

pub(crate) fn resolve_net_in_context<'a>(
    circuit: &'a Circuit,
    name: &str,
    owner_sheet: Option<Uuid>,
) -> Result<&'a Net> {
    let matches: Vec<&Net> = circuit.nets.iter().filter(|net| net.name == name).collect();
    if matches.is_empty() {
        return Err(format!("Net '{name}' not found").into());
    }

    if let Some(sheet_uuid) = owner_sheet {
        if let Some(local) = matches
            .iter()
            .copied()
            .find(|net| net.scope == NetScope::Local && net.owner_sheet == Some(sheet_uuid))
        {
            return Ok(local);
        }

        let globals: Vec<&Net> = matches
            .iter()
            .copied()
            .filter(|net| net.scope == NetScope::Global)
            .collect();
        if globals.len() == 1 {
            return Ok(globals[0]);
        }

        return Err(format!(
            "Net '{name}' is not defined on this schematic. Use --scope local with the owning schematic, or reference an existing global net"
        )
        .into());
    }

    if matches.len() == 1 {
        return Ok(matches[0]);
    }

    let scoped = matches
        .iter()
        .map(|net| match net.scope {
            NetScope::Global => format!("global ({})", net.uuid),
            NetScope::Local => format!(
                "local:{} ({})",
                net.owner_sheet
                    .map(|uuid| uuid.to_string())
                    .unwrap_or_else(|| "unknown".into()),
                net.uuid
            ),
        })
        .collect::<Vec<_>>()
        .join(", ");
    Err(format!(
        "Net '{name}' is ambiguous across scopes: {scoped}. Re-run with --schematic to choose a local net"
    )
    .into())
}

pub(crate) fn resolve_net_uuid_in_context(
    circuit: &Circuit,
    name: &str,
    owner_sheet: Option<Uuid>,
) -> Result<Uuid> {
    Ok(resolve_net_in_context(circuit, name, owner_sheet)?.uuid)
}

fn net_add(
    project: &std::path::Path,
    name: &str,
    scope_str: &str,
    schematic: Option<&str>,
    power: bool,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let mut circuit = project_io::read_circuit(project)?;

    let (scope, owner_sheet) = match scope_str.trim().to_ascii_lowercase().as_str() {
        "global" => {
            if schematic.is_some() {
                return Err("Global nets do not take --schematic".into());
            }
            (NetScope::Global, None)
        }
        "local" => {
            let schematic = schematic
                .ok_or("Local nets require --schematic <sheet-name> to define ownership")?;
            (
                NetScope::Local,
                Some(resolve_schematic_uuid(project, schematic)?),
            )
        }
        _ => return Err(format!("Invalid scope '{}'. Use 'global' or 'local'", scope_str).into()),
    };

    if circuit.nets.iter().any(|net| {
        net.name == name
            && net.scope == scope
            && match scope {
                NetScope::Global => true,
                NetScope::Local => net.owner_sheet == owner_sheet,
            }
    }) {
        return Err(format!("Net '{name}' already exists in this scope").into());
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
        scope,
        owner_sheet,
        is_power: power,
    };

    circuit.nets.push(net);
    project_io::write_circuit(project, &circuit)?;

    let result = serde_json::json!({
        "status": "ok",
        "net": {
            "uuid": net_uuid.to_string(),
            "name": name,
            "scope": match scope {
                NetScope::Global => "global",
                NetScope::Local => "local",
            },
            "owner_sheet": owner_sheet.map(|uuid| uuid.to_string()),
            "is_power": power,
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
    schematic: Option<&str>,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let mut circuit = project_io::read_circuit(project)?;
    let schematic_uuid = match schematic {
        Some(name) => Some(resolve_schematic_uuid(project, name)?),
        None => None,
    };

    // Find the net
    let net_uuid = resolve_net_uuid_in_context(&circuit, net_name, schematic_uuid)?;

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
            "schematic": schematic,
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

    fn split_gate_pin_selector(selector: &str) -> (Option<&str>, &str) {
        if let Some((gate, pin)) = selector.split_once(':') {
            if !gate.is_empty() && !pin.is_empty() {
                return (Some(gate), pin);
            }
        }
        (None, selector)
    }

    fn gate_matches_selector(gate_uuid: uuid::Uuid, gate_suffix: &str, selector: &str) -> bool {
        selector
            .parse::<uuid::Uuid>()
            .map(|uuid| uuid == gate_uuid)
            .unwrap_or_else(|_| gate_suffix.eq_ignore_ascii_case(selector))
    }

    fn gate_display(component_name: &str, gate_suffix: &str, gate_uuid: uuid::Uuid) -> String {
        if gate_suffix.is_empty() {
            format!("{component_name}:{}", gate_uuid)
        } else {
            format!("{component_name}:{}", gate_suffix)
        }
    }

    let variant = lib_comp
        .variants
        .iter()
        .find(|v| v.uuid == lib_variant_uuid)
        .ok_or_else(|| format!("Variant not found for component '{component_name}'"))?;
    let (gate_selector, pin_selector) = split_gate_pin_selector(selector);

    let mut matches = Vec::new();
    let mut gate_count = 0usize;
    for gate in &variant.gates {
        if let Some(selector) = gate_selector {
            if !gate_matches_selector(gate.uuid, &gate.suffix, selector) {
                continue;
            }
        }
        gate_count += 1;

        let lib_sym: Symbol = project_io::read_library_element(project, "symbols", &gate.symbol)?;
        let pin = match find_symbol_pin(&lib_sym, pin_selector) {
            Ok(pin) => pin,
            Err(_) => continue,
        };
        let Some(mapping) = gate.pin_mappings.iter().find(|m| m.pin == pin.uuid) else {
            continue;
        };
        let Some(signal) = lib_comp.signals.iter().find(|s| s.uuid == mapping.signal) else {
            continue;
        };
        matches.push((
            signal.uuid,
            format_pin_display(pin),
            gate.suffix.clone(),
            gate.uuid,
        ));
    }

    if matches.len() == 1 {
        return Ok((matches[0].0, matches[0].1.clone()));
    }

    if matches.is_empty() {
        if let Some(selector) = gate_selector {
            if gate_count == 0 {
                return Err(format!(
                    "Gate '{}' not found on component '{}'",
                    selector, component_name
                )
                .into());
            }
            return Err(format!(
                "Pin '{}' not found on '{}:{}'",
                pin_selector, component_name, selector
            )
            .into());
        }
        return Err(format!(
            "Pin '{}' not found on component '{}'",
            pin_selector, component_name
        )
        .into());
    }

    let gate_list = matches
        .iter()
        .map(|(_, _, suffix, uuid)| gate_display(component_name, suffix, *uuid))
        .collect::<Vec<_>>()
        .join(", ");
    Err(
        format!(
            "Pin '{}' is ambiguous on component '{}'. Matches: {}. Use a gate-qualified selector like '{}:{}'",
            pin_selector,
            component_name,
            gate_list,
            gate_display(component_name, &matches[0].2, matches[0].3),
            pin_selector
        )
        .into(),
    )
}

fn find_symbol_pin<'a>(
    lib_sym: &'a Symbol,
    selector: &str,
) -> std::result::Result<&'a SymbolPin, String> {
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
                "scope": match n.scope {
                    NetScope::Global => "global",
                    NetScope::Local => "local",
                },
                "owner_sheet": n.owner_sheet.map(|uuid| uuid.to_string()),
                "is_power": n.is_power,
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

fn net_diff_pair(
    project: &std::path::Path,
    name: &str,
    positive_name: &str,
    negative_name: &str,
    max_delta: Option<f64>,
    impedance: Option<f64>,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let mut circuit = project_io::read_circuit(project)?;

    let pos_net = circuit
        .nets
        .iter()
        .find(|n| n.name == positive_name)
        .ok_or_else(|| format!("Positive net '{}' not found", positive_name))?;
    let neg_net = circuit
        .nets
        .iter()
        .find(|n| n.name == negative_name)
        .ok_or_else(|| format!("Negative net '{}' not found", negative_name))?;

    let pos_uuid = pos_net.uuid;
    let neg_uuid = neg_net.uuid;

    // Check for existing pair
    if circuit.differential_pairs.iter().any(|dp| dp.name == name) {
        return Err(format!("Differential pair '{}' already exists", name).into());
    }

    let pair = DifferentialPair {
        uuid: new_uuid(),
        name: name.to_string(),
        positive_net: pos_uuid,
        negative_net: neg_uuid,
        max_length_delta: max_delta,
        target_impedance: impedance,
    };

    let uuid_str = pair.uuid.to_string();
    circuit.differential_pairs.push(pair);
    project_io::write_circuit(project, &circuit)?;

    let result = serde_json::json!({
        "status": "ok",
        "differential_pair": {
            "uuid": uuid_str,
            "name": name,
            "positive_net": positive_name,
            "negative_net": negative_name,
            "max_length_delta": max_delta,
            "target_impedance": impedance,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}
