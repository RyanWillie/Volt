//! `volt-eda schematic` subcommands.

use std::path::PathBuf;

use clap::Subcommand;
use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Component, Symbol};
use volt_core::project::*;

use super::project_io::{self, Result};

#[derive(Subcommand)]
pub enum SchematicCommands {
    /// Place a component symbol on the schematic
    Place {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Schematic name (without .json)
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Component designator (e.g. "R1")
        #[arg(long)]
        component: String,
        /// Position as grid coordinates "x,y" (grid = 2.54mm)
        #[arg(long)]
        grid: String,
        /// Rotation in degrees (0, 90, 180, 270)
        #[arg(long, default_value = "0")]
        rotation: f64,
        /// Mirror the symbol
        #[arg(long, default_value = "false")]
        mirror: bool,
    },
    /// Move a placed symbol to a new position
    Move {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Component designator to move
        #[arg(long)]
        component: String,
        /// New position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
    },
    /// Add a wire between two endpoints (pin or junction)
    Wire {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Net name
        #[arg(long)]
        net: String,
        /// From endpoint: "component:pin" (e.g. "R1:1") or "x,y" for junction
        #[arg(long)]
        from: String,
        /// To endpoint: "component:pin" or "x,y" for junction
        #[arg(long)]
        to: String,
    },
    /// Add a net label at a position
    Label {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Net name
        #[arg(long)]
        net: String,
        /// Position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
        /// Rotation in degrees
        #[arg(long, default_value = "0")]
        rotation: f64,
    },
    /// Render the schematic to SVG
    Render {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Output SVG file path
        #[arg(long)]
        output: PathBuf,
    },
}

pub fn schematic_command(cmd: SchematicCommands) -> Result<()> {
    match cmd {
        SchematicCommands::Place {
            project, schematic, component, grid, rotation, mirror,
        } => place_symbol(&project, &schematic, &component, &grid, rotation, mirror),
        SchematicCommands::Move {
            project, schematic, component, grid,
        } => move_symbol(&project, &schematic, &component, &grid),
        SchematicCommands::Wire {
            project, schematic, net, from, to,
        } => add_wire(&project, &schematic, &net, &from, &to),
        SchematicCommands::Label {
            project, schematic, net, grid, rotation,
        } => add_label(&project, &schematic, &net, &grid, rotation),
        SchematicCommands::Render {
            project, schematic, output,
        } => super::render::render_schematic(&project, &schematic, &output),
    }
}

fn parse_grid(s: &str) -> Result<Position> {
    let parts: Vec<&str> = s.split(',').collect();
    if parts.len() != 2 {
        return Err(format!("Invalid grid position '{}': expected 'x,y'", s).into());
    }
    let gx: f64 = parts[0].trim().parse()
        .map_err(|_| format!("Invalid grid x: '{}'", parts[0]))?;
    let gy: f64 = parts[1].trim().parse()
        .map_err(|_| format!("Invalid grid y: '{}'", parts[1]))?;
    // Grid units: 1 grid = 2.54mm
    Ok(Position::new(gx * 2.54, gy * 2.54))
}

fn place_symbol(
    project: &std::path::Path,
    sch_name: &str,
    comp_name: &str,
    grid: &str,
    rotation: f64,
    mirror: bool,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    let position = parse_grid(grid)?;

    // Find the component instance
    let comp_instance = circuit.components.iter()
        .find(|c| c.name == comp_name)
        .ok_or_else(|| format!("Component '{}' not found in circuit", comp_name))?;

    // Check if already placed
    if schematic.symbols.iter().any(|s| s.component == comp_instance.uuid) {
        return Err(format!("Component '{}' is already placed on schematic '{}'", comp_name, sch_name).into());
    }

    // Look up the library component to get the gate UUID
    let lib_comp: Component = project_io::read_library_element(
        project, "components", &comp_instance.lib_component,
    )?;

    let variant = lib_comp.variants.iter()
        .find(|v| v.uuid == comp_instance.lib_variant)
        .ok_or_else(|| format!("Variant not found for component '{}'", comp_name))?;

    let gate = variant.gates.first()
        .ok_or_else(|| format!("No gates defined for component '{}'", comp_name))?;

    // Look up symbol to get text templates
    let lib_sym: Symbol = project_io::read_library_element(
        project, "symbols", &gate.symbol,
    )?;

    // Create schematic texts from the symbol's text templates, offset by position
    let texts: Vec<SchematicText> = lib_sym.texts.iter().map(|t| {
        SchematicText {
            uuid: new_uuid(),
            layer: t.layer,
            value: t.value.clone(),
            position: Position::new(
                position.x + t.position.x,
                position.y + t.position.y,
            ),
            rotation: Angle(t.rotation.0 + rotation),
            height: t.height,
            align: t.align,
            lock: t.lock,
        }
    }).collect();

    let sym_uuid = new_uuid();
    let symbol = SchematicSymbol {
        uuid: sym_uuid,
        component: comp_instance.uuid,
        lib_gate: gate.uuid,
        position,
        rotation: Angle(rotation),
        mirror,
        texts,
    };

    schematic.symbols.push(symbol);
    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "symbol": {
            "uuid": sym_uuid.to_string(),
            "component": comp_name,
            "position": { "x": position.x, "y": position.y },
            "grid": grid,
            "rotation": rotation,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn move_symbol(
    project: &std::path::Path,
    sch_name: &str,
    comp_name: &str,
    grid: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    let new_pos = parse_grid(grid)?;

    let comp_instance = circuit.components.iter()
        .find(|c| c.name == comp_name)
        .ok_or_else(|| format!("Component '{}' not found in circuit", comp_name))?;

    let sym = schematic.symbols.iter_mut()
        .find(|s| s.component == comp_instance.uuid)
        .ok_or_else(|| format!("Component '{}' not placed on schematic '{}'", comp_name, sch_name))?;

    let old_pos = sym.position;
    let dx = new_pos.x - old_pos.x;
    let dy = new_pos.y - old_pos.y;

    sym.position = new_pos;

    // Move texts too
    for text in &mut sym.texts {
        text.position.x += dx;
        text.position.y += dy;
    }

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "moved": {
            "component": comp_name,
            "from": { "x": old_pos.x, "y": old_pos.y },
            "to": { "x": new_pos.x, "y": new_pos.y },
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

/// Parse an endpoint string: either "Component:Pin" or "x,y" (grid coords for junction)
fn parse_endpoint(
    s: &str,
    circuit: &Circuit,
    schematic: &Schematic,
    project: &std::path::Path,
) -> Result<(LineEndpoint, Option<Position>)> {
    if s.contains(':') {
        // Symbol pin: "R1:1"
        let parts: Vec<&str> = s.splitn(2, ':').collect();
        let comp_name = parts[0];
        let pin_name = parts[1];

        let comp_instance = circuit.components.iter()
            .find(|c| c.name == comp_name)
            .ok_or_else(|| format!("Component '{}' not found", comp_name))?;

        let sch_sym = schematic.symbols.iter()
            .find(|s| s.component == comp_instance.uuid)
            .ok_or_else(|| format!("Component '{}' not placed on schematic", comp_name))?;

        // Look up library component and symbol to find pin UUID
        let lib_comp: Component = project_io::read_library_element(
            project, "components", &comp_instance.lib_component,
        )?;
        let variant = lib_comp.variants.iter()
            .find(|v| v.uuid == comp_instance.lib_variant)
            .ok_or("Variant not found")?;
        let gate = variant.gates.first().ok_or("No gate")?;

        let lib_sym: Symbol = project_io::read_library_element(
            project, "symbols", &gate.symbol,
        )?;

        let pin = lib_sym.pins.iter()
            .find(|p| p.name == pin_name)
            .ok_or_else(|| {
                let avail: Vec<&str> = lib_sym.pins.iter().map(|p| p.name.as_str()).collect();
                format!("Pin '{}' not found on '{}'. Available: {}", pin_name, comp_name, avail.join(", "))
            })?;

        Ok((
            LineEndpoint::Symbol { symbol: sch_sym.uuid, pin: pin.uuid },
            None,
        ))
    } else {
        // Junction position: "x,y"
        let pos = parse_grid(s)?;
        Ok((
            LineEndpoint::Junction { junction: Uuid::nil() }, // placeholder, will be created
            Some(pos),
        ))
    }
}

fn add_wire(
    project: &std::path::Path,
    sch_name: &str,
    net_name: &str,
    from_str: &str,
    to_str: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    // Find the net
    let net = circuit.nets.iter()
        .find(|n| n.name == net_name)
        .ok_or_else(|| format!("Net '{}' not found", net_name))?;

    let (mut from_ep, from_pos) = parse_endpoint(from_str, &circuit, &schematic, project)?;
    let (mut to_ep, to_pos) = parse_endpoint(to_str, &circuit, &schematic, project)?;

    // Find or create the net segment for this net
    let seg = if let Some(seg) = schematic.net_segments.iter_mut().find(|s| s.net == net.uuid) {
        seg
    } else {
        schematic.net_segments.push(SchematicNetSegment {
            uuid: new_uuid(),
            net: net.uuid,
            junctions: vec![],
            lines: vec![],
            labels: vec![],
        });
        schematic.net_segments.last_mut().unwrap()
    };

    // Create junctions if endpoints are positions
    if let Some(pos) = from_pos {
        let junc_uuid = new_uuid();
        seg.junctions.push(Junction { uuid: junc_uuid, position: pos });
        from_ep = LineEndpoint::Junction { junction: junc_uuid };
    }
    if let Some(pos) = to_pos {
        let junc_uuid = new_uuid();
        seg.junctions.push(Junction { uuid: junc_uuid, position: pos });
        to_ep = LineEndpoint::Junction { junction: junc_uuid };
    }

    let line_uuid = new_uuid();
    seg.lines.push(SchematicLine {
        uuid: line_uuid,
        width: 0.15875,
        from: from_ep,
        to: to_ep,
    });

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "wire": {
            "uuid": line_uuid.to_string(),
            "net": net_name,
            "from": from_str,
            "to": to_str,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn add_label(
    project: &std::path::Path,
    sch_name: &str,
    net_name: &str,
    grid: &str,
    rotation: f64,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    let position = parse_grid(grid)?;

    let net = circuit.nets.iter()
        .find(|n| n.name == net_name)
        .ok_or_else(|| format!("Net '{}' not found", net_name))?;

    // Find or create net segment
    let seg = if let Some(seg) = schematic.net_segments.iter_mut().find(|s| s.net == net.uuid) {
        seg
    } else {
        schematic.net_segments.push(SchematicNetSegment {
            uuid: new_uuid(),
            net: net.uuid,
            junctions: vec![],
            lines: vec![],
            labels: vec![],
        });
        schematic.net_segments.last_mut().unwrap()
    };

    let label_uuid = new_uuid();
    seg.labels.push(NetLabel {
        uuid: label_uuid,
        position,
        rotation: Angle(rotation),
        mirror: false,
    });

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "label": {
            "uuid": label_uuid.to_string(),
            "net": net_name,
            "position": { "x": position.x, "y": position.y },
            "grid": grid,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}
