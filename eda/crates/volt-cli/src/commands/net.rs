//! `volt-eda net` subcommands.

use std::path::PathBuf;

use clap::Subcommand;
use uuid::Uuid;

use volt_core::common::*;
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

    // Find the component instance
    let comp_instance = circuit
        .components
        .iter_mut()
        .find(|c| c.name == component_name)
        .ok_or_else(|| format!("Component '{component_name}' not found"))?;

    // Look up the library component to resolve pin name → signal UUID
    let lib_comp: volt_core::library::Component =
        project_io::read_library_element(project, "components", &comp_instance.lib_component)?;

    let signal = lib_comp
        .signals
        .iter()
        .find(|s| s.name == pin_name)
        .ok_or_else(|| {
            let available: Vec<&str> = lib_comp.signals.iter().map(|s| s.name.as_str()).collect();
            format!(
                "Pin '{pin_name}' not found on component '{component_name}'. Available: {}",
                available.join(", ")
            )
        })?;

    // Find or create the signal connection
    let mut found = false;
    for conn in &mut comp_instance.signal_connections {
        if conn.signal == signal.uuid {
            conn.net = Some(net_uuid);
            found = true;
            break;
        }
    }
    if !found {
        comp_instance.signal_connections.push(SignalConnection {
            signal: signal.uuid,
            net: Some(net_uuid),
        });
    }

    project_io::write_circuit(project, &circuit)?;

    let result = serde_json::json!({
        "status": "ok",
        "connection": {
            "component": component_name,
            "pin": pin_name,
            "signal_uuid": signal.uuid.to_string(),
            "net": net_name,
            "net_uuid": net_uuid.to_string(),
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
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
