//! `volt-eda schematic` subcommands.

use std::path::PathBuf;

use clap::Subcommand;
use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Component, Symbol, SymbolPin, SymbolText};
use volt_core::project::*;

use super::net::resolve_net_uuid_in_context;
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
        /// Gate UUID or suffix (e.g. "A", "B") for multi-gate components. Omit for single-gate.
        #[arg(long)]
        gate: Option<String>,
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
        /// Gate UUID or suffix (e.g. "A", "B") for multi-gate components.
        #[arg(long)]
        gate: Option<String>,
        /// New position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
    },
    /// Move or reset reference/value text fields on a placed symbol
    Field {
        #[command(subcommand)]
        command: SchematicFieldCommands,
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
        /// From endpoint: "component:pin", gate-qualified "component.gate:pin" (e.g. "U1.A:OUT"), or "x,y" for junction
        #[arg(long)]
        from: String,
        /// To endpoint: "component:pin", gate-qualified "component.gate:pin", or "x,y" for junction
        #[arg(long)]
        to: String,
        /// Routing style: "manhattan" (default, right-angle segments) or "direct"
        #[arg(long, default_value = "manhattan")]
        route: String,
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
    /// Auto-place all components and wire nets (replaces existing layout)
    Autoplace {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
    },
    /// Clean up an existing schematic layout
    Tidy {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
    },
    /// Add a child sheet reference to the schematic
    AddSheet {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Instance name on the parent sheet
        #[arg(long)]
        name: String,
        /// Target child schematic name (without .json)
        #[arg(long)]
        target: String,
        /// Position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
    },
    /// Add or bind a visible sheet pin on a child-sheet reference
    AddSheetPin {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Parent-sheet reference name or UUID
        #[arg(long)]
        sheet: String,
        /// Child hierarchical port name or UUID
        #[arg(long)]
        port: String,
        /// Parent-side net binding
        #[arg(long)]
        net: String,
        /// Optional visible pin name (defaults to the child port name)
        #[arg(long)]
        name: Option<String>,
        /// Optional side override (left, right, top, bottom)
        #[arg(long)]
        side: Option<String>,
        /// Optional offset along the selected side, in mm
        #[arg(long)]
        offset: Option<f64>,
    },
    /// Add a hierarchical port to the schematic (child-sheet interface)
    AddPort {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Port name
        #[arg(long)]
        name: String,
        /// Net to connect this port to
        #[arg(long)]
        net: String,
        /// Position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
        /// Side of the sheet (left, right, top, bottom)
        #[arg(long, default_value = "left")]
        side: String,
    },
    /// Add a power port symbol (VCC, GND, etc.)
    AddPower {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Net name for the power rail
        #[arg(long)]
        net: String,
        /// Position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
        /// Visual style (vcc, gnd, 3v3, etc.)
        #[arg(long, default_value = "vcc")]
        style: String,
    },
    /// Add a power flag indicating a power net is actively driven
    AddPowerFlag {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Net name for the driven power rail
        #[arg(long)]
        net: String,
        /// Position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
    },
    /// Add a bus wire segment
    AddBus {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Bus label (e.g. "D[0..7]")
        #[arg(long)]
        label: String,
        /// From position as grid coordinates "x,y"
        #[arg(long)]
        from: String,
        /// To position as grid coordinates "x,y"
        #[arg(long)]
        to: String,
    },
    /// Add a reusable bus alias
    AddBusAlias {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Alias name
        #[arg(long)]
        name: String,
        /// Comma-separated scalar members (e.g. D0,D1,D2,D3)
        #[arg(long, value_delimiter = ',', num_args = 1..)]
        members: Vec<String>,
    },
    /// Add a bus entry connecting a bus to a scalar net
    AddBusEntry {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Bus segment UUID
        #[arg(long)]
        bus: uuid::Uuid,
        /// Scalar net name (e.g. "D3")
        #[arg(long)]
        net: String,
        /// Bus member name (e.g. "D[3]")
        #[arg(long)]
        member: String,
        /// Position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
    },
}

#[derive(Subcommand)]
pub enum SchematicFieldCommands {
    /// Move a single field (name/reference or value) on a placed symbol
    Move {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        #[arg(long)]
        component: String,
        /// Gate UUID or suffix (e.g. "A", "B") for multi-gate components.
        #[arg(long)]
        gate: Option<String>,
        /// Field name: `name`/`reference` or `value`
        #[arg(long)]
        field: String,
        /// New absolute position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
    },
    /// Reset one field, or all fields, to the library default positions
    Reset {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        #[arg(long)]
        component: String,
        /// Gate UUID or suffix (e.g. "A", "B") for multi-gate components.
        #[arg(long)]
        gate: Option<String>,
        /// Optional field name: `name`/`reference` or `value`
        #[arg(long)]
        field: Option<String>,
    },
}

pub fn schematic_command(cmd: SchematicCommands) -> Result<()> {
    match cmd {
        SchematicCommands::Place {
            project,
            schematic,
            component,
            gate,
            grid,
            rotation,
            mirror,
        } => place_symbol(
            &project,
            &schematic,
            &component,
            gate.as_deref(),
            &grid,
            rotation,
            mirror,
        ),
        SchematicCommands::Move {
            project,
            schematic,
            component,
            gate,
            grid,
        } => move_symbol(&project, &schematic, &component, gate.as_deref(), &grid),
        SchematicCommands::Field { command } => match command {
            SchematicFieldCommands::Move {
                project,
                schematic,
                component,
                gate,
                field,
                grid,
            } => move_symbol_field(
                &project,
                &schematic,
                &component,
                gate.as_deref(),
                &field,
                &grid,
            ),
            SchematicFieldCommands::Reset {
                project,
                schematic,
                component,
                gate,
                field,
            } => reset_symbol_field(
                &project,
                &schematic,
                &component,
                gate.as_deref(),
                field.as_deref(),
            ),
        },
        SchematicCommands::Wire {
            project,
            schematic,
            net,
            from,
            to,
            route,
        } => add_wire(&project, &schematic, &net, &from, &to, &route),
        SchematicCommands::Label {
            project,
            schematic,
            net,
            grid,
            rotation,
        } => add_label(&project, &schematic, &net, &grid, rotation),
        SchematicCommands::Render {
            project,
            schematic,
            output,
        } => super::render::render_schematic(&project, &schematic, &output),
        SchematicCommands::Autoplace { project, schematic } => {
            super::autoplace::autoplace_schematic(&project, &schematic)
        }
        SchematicCommands::Tidy { project, schematic } => {
            super::autoplace::tidy_schematic(&project, &schematic)
        }
        SchematicCommands::AddSheet {
            project,
            schematic,
            name,
            target,
            grid,
        } => add_sheet(&project, &schematic, &name, &target, &grid),
        SchematicCommands::AddSheetPin {
            project,
            schematic,
            sheet,
            port,
            net,
            name,
            side,
            offset,
        } => add_sheet_pin(
            &project,
            &schematic,
            &sheet,
            &port,
            &net,
            name.as_deref(),
            side.as_deref(),
            offset,
        ),
        SchematicCommands::AddPort {
            project,
            schematic,
            name,
            net,
            grid,
            side,
        } => add_port(&project, &schematic, &name, &net, &grid, &side),
        SchematicCommands::AddPower {
            project,
            schematic,
            net,
            grid,
            style,
        } => add_power_port(&project, &schematic, &net, &grid, &style),
        SchematicCommands::AddPowerFlag {
            project,
            schematic,
            net,
            grid,
        } => add_power_flag(&project, &schematic, &net, &grid),
        SchematicCommands::AddBus {
            project,
            schematic,
            label,
            from,
            to,
        } => add_bus(&project, &schematic, &label, &from, &to),
        SchematicCommands::AddBusAlias {
            project,
            schematic,
            name,
            members,
        } => add_bus_alias(&project, &schematic, &name, &members),
        SchematicCommands::AddBusEntry {
            project,
            schematic,
            bus,
            net,
            member,
            grid,
        } => add_bus_entry(&project, &schematic, bus, &net, &member, &grid),
    }
}

fn parse_grid(s: &str) -> Result<Position> {
    let parts: Vec<&str> = s.split(',').collect();
    if parts.len() != 2 {
        return Err(format!("Invalid grid position '{}': expected 'x,y'", s).into());
    }
    let gx: f64 = parts[0]
        .trim()
        .parse()
        .map_err(|_| format!("Invalid grid x: '{}'", parts[0]))?;
    let gy: f64 = parts[1]
        .trim()
        .parse()
        .map_err(|_| format!("Invalid grid y: '{}'", parts[1]))?;
    // Grid units: 1 grid = 2.54mm
    Ok(Position::new(gx * 2.54, gy * 2.54))
}

fn parse_sheet_side(side_str: &str) -> Result<SheetSide> {
    match side_str.trim().to_ascii_lowercase().as_str() {
        "left" => Ok(SheetSide::Left),
        "right" => Ok(SheetSide::Right),
        "top" => Ok(SheetSide::Top),
        "bottom" => Ok(SheetSide::Bottom),
        _ => Err(format!(
            "Invalid side '{}'. Use left, right, top, or bottom",
            side_str
        )
        .into()),
    }
}

fn resolve_schematic_net_uuid(
    project: &std::path::Path,
    circuit: &Circuit,
    schematic: &Schematic,
    net_name: &str,
) -> Result<Uuid> {
    let _ = project;
    resolve_net_uuid_in_context(circuit, net_name, Some(schematic.uuid))
}

fn next_sheet_pin_offset(pins: &[SheetRefPin], side: SheetSide) -> f64 {
    let count = pins.iter().filter(|pin| pin.side == side).count();
    2.54 + count as f64 * 3.81
}

fn sheet_pin_position(sheet: &SheetRef, pin: &SheetRefPin) -> Position {
    match pin.side {
        SheetSide::Left => Position::new(sheet.position.x, sheet.position.y + pin.offset),
        SheetSide::Right => Position::new(
            sheet.position.x + sheet.width,
            sheet.position.y + pin.offset,
        ),
        SheetSide::Top => Position::new(sheet.position.x + pin.offset, sheet.position.y),
        SheetSide::Bottom => Position::new(
            sheet.position.x + pin.offset,
            sheet.position.y + sheet.height,
        ),
    }
}

fn bus_members_from_label(label: &str, aliases: &[BusAlias]) -> Result<Vec<String>> {
    let trimmed = label.trim();
    if trimmed.is_empty() {
        return Err("Bus label cannot be empty".into());
    }

    if let Some(alias) = aliases.iter().find(|alias| alias.name == trimmed) {
        if alias.members.is_empty() {
            return Err(format!("Bus alias '{}' has no members", trimmed).into());
        }
        return Ok(alias.members.clone());
    }

    let Some(open_idx) = trimmed.find('[') else {
        return Err(format!(
            "Invalid bus label '{}': expected a range like 'D[0..7]' or a known alias",
            trimmed
        )
        .into());
    };
    let Some(close_idx) = trimmed[open_idx + 1..].find(']') else {
        return Err(format!("Invalid bus label '{}': missing ']'", trimmed).into());
    };
    let close_idx = open_idx + 1 + close_idx;
    let prefix = &trimmed[..open_idx];
    let range = &trimmed[open_idx + 1..close_idx];
    let suffix = &trimmed[close_idx + 1..];
    if !suffix.is_empty() {
        return Err(format!(
            "Invalid bus label '{}': unexpected trailing text after range",
            trimmed
        )
        .into());
    }

    let Some((start, end)) = range.split_once("..") else {
        return Err(format!(
            "Invalid bus label '{}': expected '..' inside the range",
            trimmed
        )
        .into());
    };
    let start: i32 = start.trim().parse().map_err(|_| {
        format!(
            "Invalid bus range start '{}' in '{}'",
            start.trim(),
            trimmed
        )
    })?;
    let end: i32 = end
        .trim()
        .parse()
        .map_err(|_| format!("Invalid bus range end '{}' in '{}'", end.trim(), trimmed))?;

    let mut members = Vec::new();
    if start <= end {
        for index in start..=end {
            members.push(format!("{prefix}[{index}]"));
        }
    } else {
        for index in (end..=start).rev() {
            members.push(format!("{prefix}[{index}]"));
        }
    }
    Ok(members)
}

fn canonical_bus_member(member: &str) -> String {
    let trimmed = member.trim();
    if let Some(open_idx) = trimmed.find('[') {
        if let Some(close_rel) = trimmed[open_idx + 1..].find(']') {
            let close_idx = open_idx + 1 + close_rel;
            let prefix = &trimmed[..open_idx];
            let index = trimmed[open_idx + 1..close_idx].trim();
            return format!("{}{}", prefix.trim(), index);
        }
    }
    trimmed.to_string()
}

fn place_symbol(
    project: &std::path::Path,
    sch_name: &str,
    comp_name: &str,
    gate_selector: Option<&str>,
    grid: &str,
    rotation: f64,
    mirror: bool,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    let position = parse_grid(grid)?;

    // Find the component instance
    let comp_instance = circuit
        .components
        .iter()
        .find(|c| c.name == comp_name)
        .ok_or_else(|| format!("Component '{}' not found in circuit", comp_name))?;

    // Look up the library component to get gates
    let lib_comp: Component =
        project_io::read_library_element(project, "components", &comp_instance.lib_component)?;

    let variant = lib_comp
        .variants
        .iter()
        .find(|v| v.uuid == comp_instance.lib_variant)
        .ok_or_else(|| format!("Variant not found for component '{}'", comp_name))?;

    if variant.gates.is_empty() {
        return Err(format!("No gates defined for component '{}'", comp_name).into());
    }

    // Collect already-placed gate UUIDs for this component
    let placed_gates: Vec<Uuid> = schematic
        .symbols
        .iter()
        .filter(|s| s.component == comp_instance.uuid)
        .map(|s| s.lib_gate)
        .collect();

    // Select the gate to place
    let gate = if let Some(selector) = gate_selector {
        // Try UUID first, then suffix match
        if let Ok(uuid) = selector.parse::<Uuid>() {
            variant
                .gates
                .iter()
                .find(|g| g.uuid == uuid)
                .ok_or_else(|| {
                    format!(
                        "Gate UUID '{}' not found in component '{}'",
                        selector, comp_name
                    )
                })?
        } else {
            variant
                .gates
                .iter()
                .find(|g| g.suffix.eq_ignore_ascii_case(selector))
                .ok_or_else(|| {
                    format!(
                        "Gate suffix '{}' not found in component '{}'",
                        selector, comp_name
                    )
                })?
        }
    } else {
        // Auto-select first unplaced gate
        variant
            .gates
            .iter()
            .find(|g| !placed_gates.contains(&g.uuid))
            .ok_or_else(|| {
                format!(
                    "All {} gate(s) of component '{}' are already placed on schematic '{}'",
                    variant.gates.len(),
                    comp_name,
                    sch_name
                )
            })?
    };

    // Check that this specific gate isn't already placed
    if placed_gates.contains(&gate.uuid) {
        let suffix_info = if gate.suffix.is_empty() {
            String::new()
        } else {
            format!(" (gate {})", gate.suffix)
        };
        return Err(format!(
            "Gate{} of component '{}' is already placed on schematic '{}'",
            suffix_info, comp_name, sch_name
        )
        .into());
    }

    // Look up symbol to get text templates
    let lib_sym: Symbol = project_io::read_library_element(project, "symbols", &gate.symbol)?;

    // Create schematic texts from the symbol's text templates, offset by position
    let texts: Vec<SchematicText> = lib_sym
        .texts
        .iter()
        .map(|t| SchematicText {
            uuid: new_uuid(),
            layer: t.layer,
            value: t.value.clone(),
            position: Position::new(position.x + t.position.x, position.y + t.position.y),
            rotation: Angle(t.rotation.0 + rotation),
            height: t.height,
            align: t.align,
            lock: t.lock,
        })
        .collect();

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
    gate_selector: Option<&str>,
    grid: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    let new_pos = parse_grid(grid)?;
    let symbol_index =
        resolve_schematic_symbol_index(project, &circuit, &schematic, comp_name, gate_selector)?;
    let sym = &mut schematic.symbols[symbol_index];

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
            "gate": gate_selector,
            "from": { "x": old_pos.x, "y": old_pos.y },
            "to": { "x": new_pos.x, "y": new_pos.y },
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn move_symbol_field(
    project: &std::path::Path,
    sch_name: &str,
    comp_name: &str,
    gate_selector: Option<&str>,
    field: &str,
    grid: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let new_pos = parse_grid(grid)?;
    let target_layer = field_layer(field)?;
    let symbol_index =
        resolve_schematic_symbol_index(project, &circuit, &schematic, comp_name, gate_selector)?;
    let sym = &mut schematic.symbols[symbol_index];

    let text = sym
        .texts
        .iter_mut()
        .find(|t| t.layer == target_layer)
        .ok_or_else(|| format!("Field '{}' not found on component '{}'", field, comp_name))?;

    let old_pos = text.position;
    text.position = new_pos;

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "field": {
            "component": comp_name,
            "gate": gate_selector,
            "field": canonical_field_name(target_layer),
            "from": { "x": old_pos.x, "y": old_pos.y },
            "to": { "x": new_pos.x, "y": new_pos.y },
            "grid": grid,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn reset_symbol_field(
    project: &std::path::Path,
    sch_name: &str,
    comp_name: &str,
    gate_selector: Option<&str>,
    field: Option<&str>,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let symbol_index =
        resolve_schematic_symbol_index(project, &circuit, &schematic, comp_name, gate_selector)?;
    let comp_instance = circuit
        .components
        .iter()
        .find(|c| c.name == comp_name)
        .ok_or_else(|| format!("Component '{}' not found in circuit", comp_name))?;

    let lib_comp: Component =
        project_io::read_library_element(project, "components", &comp_instance.lib_component)?;
    let variant = lib_comp
        .variants
        .iter()
        .find(|v| v.uuid == comp_instance.lib_variant)
        .ok_or_else(|| format!("Variant not found for component '{}'", comp_name))?;
    let gate = variant
        .gates
        .iter()
        .find(|g| g.uuid == schematic.symbols[symbol_index].lib_gate)
        .or_else(|| variant.gates.first())
        .ok_or_else(|| format!("No gates defined for component '{}'", comp_name))?;
    let lib_sym: Symbol = project_io::read_library_element(project, "symbols", &gate.symbol)?;
    let sym = &mut schematic.symbols[symbol_index];

    let target_layers: Vec<Layer> = if let Some(field) = field {
        vec![field_layer(field)?]
    } else {
        vec![Layer::SchNames, Layer::SchValues]
    };

    let mut reset_fields = Vec::new();
    for layer in target_layers {
        let template = lib_sym
            .texts
            .iter()
            .find(|t| t.layer == layer)
            .ok_or_else(|| {
                format!(
                    "No library template found for '{}' field on '{}'",
                    canonical_field_name(layer),
                    comp_name
                )
            })?;
        apply_symbol_text_template(sym, template);
        reset_fields.push(canonical_field_name(layer));
    }

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "reset": {
            "component": comp_name,
            "gate": gate_selector,
            "fields": reset_fields,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn apply_symbol_text_template(sym: &mut SchematicSymbol, template: &SymbolText) {
    let rebuilt = SchematicText {
        uuid: sym
            .texts
            .iter()
            .find(|t| t.layer == template.layer)
            .map(|t| t.uuid)
            .unwrap_or_else(new_uuid),
        layer: template.layer,
        value: template.value.clone(),
        position: Position::new(
            sym.position.x + template.position.x,
            sym.position.y + template.position.y,
        ),
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
}

fn field_layer(field: &str) -> Result<Layer> {
    match field.trim().to_ascii_lowercase().as_str() {
        "name" | "reference" => Ok(Layer::SchNames),
        "value" => Ok(Layer::SchValues),
        _ => Err(format!(
            "Unknown field '{}'. Use 'name'/'reference' or 'value'",
            field
        )
        .into()),
    }
}

fn canonical_field_name(layer: Layer) -> &'static str {
    match layer {
        Layer::SchNames => "name",
        Layer::SchValues => "value",
        _ => "field",
    }
}

#[derive(Clone)]
struct ResolvedSchematicPin {
    symbol_uuid: Uuid,
    symbol_position: Position,
    symbol_rotation: Angle,
    symbol_mirror: bool,
    gate_uuid: Uuid,
    gate_suffix: String,
    pin: SymbolPin,
}

fn parse_component_gate_selector(selector: &str) -> (&str, Option<&str>) {
    selector
        .rsplit_once('.')
        .filter(|(component, gate)| !component.is_empty() && !gate.is_empty())
        .map_or((selector, None), |(component, gate)| {
            (component, Some(gate))
        })
}

fn gate_matches_selector(gate_uuid: Uuid, gate_suffix: &str, selector: &str) -> bool {
    selector
        .parse::<Uuid>()
        .map(|uuid| uuid == gate_uuid)
        .unwrap_or_else(|_| gate_suffix.eq_ignore_ascii_case(selector))
}

fn gate_display(component_name: &str, gate_suffix: &str, gate_uuid: Uuid) -> String {
    if gate_suffix.is_empty() {
        format!("{component_name}.{}", gate_uuid)
    } else {
        format!("{component_name}.{gate_suffix}")
    }
}

fn resolve_schematic_symbol_index(
    project: &std::path::Path,
    circuit: &Circuit,
    schematic: &Schematic,
    component_name: &str,
    gate_selector: Option<&str>,
) -> Result<usize> {
    let comp_instance = circuit
        .components
        .iter()
        .find(|c| c.name == component_name)
        .ok_or_else(|| format!("Component '{}' not found in circuit", component_name))?;

    let lib_comp: Component =
        project_io::read_library_element(project, "components", &comp_instance.lib_component)?;
    let variant = lib_comp
        .variants
        .iter()
        .find(|v| v.uuid == comp_instance.lib_variant)
        .ok_or("Variant not found")?;

    let mut matches = Vec::new();
    for (idx, sch_sym) in schematic.symbols.iter().enumerate() {
        if sch_sym.component != comp_instance.uuid {
            continue;
        }
        let Some(gate) = variant.gates.iter().find(|g| g.uuid == sch_sym.lib_gate) else {
            continue;
        };
        if let Some(selector) = gate_selector {
            if !gate_matches_selector(gate.uuid, &gate.suffix, selector) {
                continue;
            }
        }
        matches.push((idx, gate));
    }

    match matches.len() {
        1 => Ok(matches[0].0),
        0 => {
            if let Some(selector) = gate_selector {
                Err(format!(
                    "Gate '{}' of component '{}' is not placed on schematic",
                    selector, component_name
                )
                .into())
            } else {
                Err(format!("Component '{}' not placed on schematic", component_name).into())
            }
        }
        _ => {
            let gate_list = matches
                .iter()
                .map(|(_, gate)| gate_display(component_name, &gate.suffix, gate.uuid))
                .collect::<Vec<_>>()
                .join(", ");
            Err(format!(
                "Component '{}' has multiple placed gates: {}. Use --gate to target one",
                component_name, gate_list
            )
            .into())
        }
    }
}

fn resolve_sheet_ref<'a>(selector: &str, schematic: &'a Schematic) -> Result<&'a SheetRef> {
    let matches: Vec<&SheetRef> = if let Ok(uuid) = selector.parse::<Uuid>() {
        schematic
            .sheet_refs
            .iter()
            .filter(|sheet| sheet.uuid == uuid)
            .collect()
    } else {
        schematic
            .sheet_refs
            .iter()
            .filter(|sheet| sheet.name == selector)
            .collect()
    };

    match matches.len() {
        1 => Ok(matches[0]),
        0 => Err(format!("Sheet reference '{}' not found", selector).into()),
        _ => Err(format!(
            "Sheet reference '{}' is ambiguous. Use the sheet UUID instead",
            selector
        )
        .into()),
    }
}

fn resolve_hierarchical_port<'a>(
    selector: &str,
    schematic: &'a Schematic,
) -> Result<&'a HierarchicalPort> {
    let matches: Vec<&HierarchicalPort> = if let Ok(uuid) = selector.parse::<Uuid>() {
        schematic
            .hierarchical_ports
            .iter()
            .filter(|port| port.uuid == uuid)
            .collect()
    } else {
        schematic
            .hierarchical_ports
            .iter()
            .filter(|port| port.name == selector)
            .collect()
    };

    match matches.len() {
        1 => Ok(matches[0]),
        0 => Err(format!("Hierarchical port '{}' not found", selector).into()),
        _ => Err(format!(
            "Hierarchical port '{}' is ambiguous. Use the port UUID instead",
            selector
        )
        .into()),
    }
}

fn resolve_sheet_ref_pin<'a>(
    sheet_selector: &str,
    pin_selector: &str,
    schematic: &'a Schematic,
) -> Result<(&'a SheetRef, &'a SheetRefPin)> {
    let sheet_ref = resolve_sheet_ref(sheet_selector, schematic)?;
    let matches: Vec<&SheetRefPin> = if let Ok(uuid) = pin_selector.parse::<Uuid>() {
        sheet_ref
            .pins
            .iter()
            .filter(|pin| pin.uuid == uuid)
            .collect()
    } else {
        sheet_ref
            .pins
            .iter()
            .filter(|pin| pin.name == pin_selector)
            .collect()
    };

    match matches.len() {
        1 => Ok((sheet_ref, matches[0])),
        0 => Err(format!(
            "Sheet pin '{}' not found on sheet reference '{}'",
            pin_selector, sheet_ref.name
        )
        .into()),
        _ => Err(format!(
            "Sheet pin '{}' is ambiguous on '{}'. Use the pin UUID instead",
            pin_selector, sheet_ref.name
        )
        .into()),
    }
}

fn resolve_schematic_pin(
    component_selector: &str,
    pin_name: &str,
    circuit: &Circuit,
    schematic: &Schematic,
    project: &std::path::Path,
) -> Result<ResolvedSchematicPin> {
    let (component_name, gate_selector) = parse_component_gate_selector(component_selector);
    let comp_instance = circuit
        .components
        .iter()
        .find(|c| c.name == component_name)
        .ok_or_else(|| format!("Component '{}' not found", component_name))?;

    let placed_symbols: Vec<&SchematicSymbol> = schematic
        .symbols
        .iter()
        .filter(|s| s.component == comp_instance.uuid)
        .collect();
    if placed_symbols.is_empty() {
        return Err(format!("Component '{}' not placed on schematic", component_name).into());
    }

    let lib_comp: Component =
        project_io::read_library_element(project, "components", &comp_instance.lib_component)?;
    let variant = lib_comp
        .variants
        .iter()
        .find(|v| v.uuid == comp_instance.lib_variant)
        .ok_or("Variant not found")?;

    let mut matching_symbols = 0usize;
    let mut pin_errors = Vec::new();
    let mut matches = Vec::new();

    for sch_sym in placed_symbols {
        let Some(gate) = variant.gates.iter().find(|g| g.uuid == sch_sym.lib_gate) else {
            continue;
        };
        if let Some(selector) = gate_selector {
            if !gate_matches_selector(gate.uuid, &gate.suffix, selector) {
                continue;
            }
        }
        matching_symbols += 1;

        let lib_sym: Symbol = project_io::read_library_element(project, "symbols", &gate.symbol)?;
        match find_symbol_pin(&lib_sym, pin_name) {
            Ok(pin) => matches.push(ResolvedSchematicPin {
                symbol_uuid: sch_sym.uuid,
                symbol_position: sch_sym.position,
                symbol_rotation: sch_sym.rotation,
                symbol_mirror: sch_sym.mirror,
                gate_uuid: gate.uuid,
                gate_suffix: gate.suffix.clone(),
                pin: pin.clone(),
            }),
            Err(err) => pin_errors.push(err),
        }
    }

    if matching_symbols == 0 {
        let selector = gate_selector.unwrap_or_default();
        return Err(format!(
            "Gate '{}' of component '{}' is not placed on schematic",
            selector, component_name
        )
        .into());
    }

    if matches.len() == 1 {
        return Ok(matches.remove(0));
    }

    if matches.is_empty() {
        let target = gate_selector.map_or_else(
            || component_name.to_string(),
            |gate| format!("{component_name}.{gate}"),
        );
        if pin_errors.len() == 1 {
            return Err(format!("{} on '{}'", pin_errors[0], target).into());
        }
        return Err(format!("Pin '{}' not found on '{}'", pin_name, target).into());
    }

    let gate_list = matches
        .iter()
        .map(|m| gate_display(component_name, &m.gate_suffix, m.gate_uuid))
        .collect::<Vec<_>>()
        .join(", ");
    Err(format!(
        "Pin '{}' is ambiguous on '{}'. Matches: {}. Use a gate-qualified endpoint like '{}:{}'",
        pin_name,
        component_name,
        gate_list,
        gate_display(
            component_name,
            &matches[0].gate_suffix,
            matches[0].gate_uuid
        ),
        pin_name
    )
    .into())
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
            "Pin '{}' not found. Available: {}",
            selector,
            lib_sym
                .pins
                .iter()
                .map(format_pin_display)
                .collect::<Vec<_>>()
                .join(", ")
        )),
        _ => Err(format!(
            "Pin name '{}' is ambiguous. Matches: {}",
            selector,
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

/// Parse an endpoint string: either "Component:Pin" or "x,y" (grid coords for junction)
fn parse_endpoint(
    s: &str,
    circuit: &Circuit,
    schematic: &Schematic,
    project: &std::path::Path,
) -> Result<(LineEndpoint, Option<Position>)> {
    if let Some(selector) = s.strip_prefix("port:") {
        let port = resolve_hierarchical_port(selector, schematic)?;
        return Ok((LineEndpoint::HierPort { port: port.uuid }, None));
    }

    if let Some(rest) = s.strip_prefix("sheet:") {
        let Some((sheet_selector, pin_selector)) = rest.split_once(':') else {
            return Err(format!(
                "Invalid sheet endpoint '{}': expected 'sheet:<sheet-ref>:<pin>'",
                s
            )
            .into());
        };
        let (sheet_ref, pin) = resolve_sheet_ref_pin(sheet_selector, pin_selector, schematic)?;
        return Ok((
            LineEndpoint::SheetPin {
                sheet_ref: sheet_ref.uuid,
                pin: pin.uuid,
            },
            None,
        ));
    }

    if s.contains(':') {
        // Symbol pin: "R1:1" or gate-qualified "U1.A:OUT"
        let parts: Vec<&str> = s.splitn(2, ':').collect();
        let comp_selector = parts[0];
        let pin_name = parts[1];
        let resolved = resolve_schematic_pin(comp_selector, pin_name, circuit, schematic, project)?;

        Ok((
            LineEndpoint::Symbol {
                symbol: resolved.symbol_uuid,
                pin: resolved.pin.uuid,
            },
            None,
        ))
    } else {
        // Junction position: "x,y"
        let pos = parse_grid(s)?;
        Ok((
            LineEndpoint::Junction {
                junction: Uuid::nil(),
            }, // placeholder, will be created
            Some(pos),
        ))
    }
}

/// Resolve a pin endpoint "Component:Pin" to its world position.
fn resolve_pin_position(
    component_selector: &str,
    pin_name: &str,
    circuit: &Circuit,
    schematic: &Schematic,
    project: &std::path::Path,
) -> Result<Position> {
    let resolved =
        resolve_schematic_pin(component_selector, pin_name, circuit, schematic, project)?;

    // Transform pin position by symbol's position and rotation
    let rot_rad = resolved.symbol_rotation.0.to_radians();
    let cos_r = rot_rad.cos();
    let sin_r = rot_rad.sin();
    let mut px = resolved.pin.position.x;
    let py = resolved.pin.position.y;
    if resolved.symbol_mirror {
        px = -px;
    }
    Ok(Position::new(
        resolved.symbol_position.x + px * cos_r - py * sin_r,
        resolved.symbol_position.y + px * sin_r + py * cos_r,
    ))
}

/// Resolve any endpoint string to a world position.
fn resolve_endpoint_position(
    s: &str,
    circuit: &Circuit,
    schematic: &Schematic,
    project: &std::path::Path,
) -> Result<Position> {
    if let Some(selector) = s.strip_prefix("port:") {
        return Ok(resolve_hierarchical_port(selector, schematic)?.position);
    }

    if let Some(rest) = s.strip_prefix("sheet:") {
        let Some((sheet_selector, pin_selector)) = rest.split_once(':') else {
            return Err(format!(
                "Invalid sheet endpoint '{}': expected 'sheet:<sheet-ref>:<pin>'",
                s
            )
            .into());
        };
        let (sheet_ref, pin) = resolve_sheet_ref_pin(sheet_selector, pin_selector, schematic)?;
        return Ok(sheet_pin_position(sheet_ref, pin));
    }

    if s.contains(':') {
        let parts: Vec<&str> = s.splitn(2, ':').collect();
        resolve_pin_position(parts[0], parts[1], circuit, schematic, project)
    } else {
        parse_grid(s)
    }
}

fn add_wire(
    project: &std::path::Path,
    sch_name: &str,
    net_name: &str,
    from_str: &str,
    to_str: &str,
    route_style: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    // Find the net
    let net_uuid = resolve_schematic_net_uuid(project, &circuit, &schematic, net_name)?;

    // Resolve positions for Manhattan routing calculation
    let from_pos = resolve_endpoint_position(from_str, &circuit, &schematic, project)?;
    let to_pos = resolve_endpoint_position(to_str, &circuit, &schematic, project)?;

    let (from_ep, from_junc_pos) = parse_endpoint(from_str, &circuit, &schematic, project)?;
    let (to_ep, to_junc_pos) = parse_endpoint(to_str, &circuit, &schematic, project)?;

    // Find or create the net segment for this net
    let seg = if let Some(seg) = schematic
        .net_segments
        .iter_mut()
        .find(|s| s.net == net_uuid)
    {
        seg
    } else {
        schematic.net_segments.push(SchematicNetSegment {
            uuid: new_uuid(),
            net: net_uuid,
            junctions: vec![],
            lines: vec![],
            labels: vec![],
        });
        schematic.net_segments.last_mut().unwrap()
    };

    // Create junctions for position-based endpoints
    let mut actual_from = from_ep;
    if let Some(pos) = from_junc_pos {
        let junc_uuid = new_uuid();
        seg.junctions.push(Junction {
            uuid: junc_uuid,
            position: pos,
        });
        actual_from = LineEndpoint::Junction {
            junction: junc_uuid,
        };
    }
    let mut actual_to = to_ep;
    if let Some(pos) = to_junc_pos {
        let junc_uuid = new_uuid();
        seg.junctions.push(Junction {
            uuid: junc_uuid,
            position: pos,
        });
        actual_to = LineEndpoint::Junction {
            junction: junc_uuid,
        };
    }

    let use_manhattan = route_style == "manhattan";
    let dx = (to_pos.x - from_pos.x).abs();
    let dy = (to_pos.y - from_pos.y).abs();
    let aligned = dx < 0.01 || dy < 0.01; // Already horizontal or vertical

    let mut line_uuids = Vec::new();

    if use_manhattan && !aligned {
        // Create a bend junction and two wire segments
        // Choose bend direction: horizontal-first if dx >= dy, vertical-first otherwise
        let bend_pos = if dx >= dy {
            // Horizontal first: go to (to_x, from_y), then vertical to target
            Position::new(to_pos.x, from_pos.y)
        } else {
            // Vertical first: go to (from_x, to_y), then horizontal to target
            Position::new(from_pos.x, to_pos.y)
        };

        // Snap bend to grid (2.54mm)
        let grid = 2.54;
        let bend_snapped = Position::new(
            (bend_pos.x / grid).round() * grid,
            (bend_pos.y / grid).round() * grid,
        );

        let bend_uuid = new_uuid();
        seg.junctions.push(Junction {
            uuid: bend_uuid,
            position: bend_snapped,
        });
        let bend_ep = LineEndpoint::Junction {
            junction: bend_uuid,
        };

        // Wire 1: from → bend
        let line1_uuid = new_uuid();
        seg.lines.push(SchematicLine {
            uuid: line1_uuid,
            width: 0.15875,
            from: actual_from,
            to: bend_ep.clone(),
        });
        line_uuids.push(line1_uuid);

        // Wire 2: bend → to
        let line2_uuid = new_uuid();
        seg.lines.push(SchematicLine {
            uuid: line2_uuid,
            width: 0.15875,
            from: bend_ep,
            to: actual_to,
        });
        line_uuids.push(line2_uuid);
    } else {
        // Direct wire (single segment)
        let line_uuid = new_uuid();
        seg.lines.push(SchematicLine {
            uuid: line_uuid,
            width: 0.15875,
            from: actual_from,
            to: actual_to,
        });
        line_uuids.push(line_uuid);
    }

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "wire": {
            "uuids": line_uuids.iter().map(|u| u.to_string()).collect::<Vec<_>>(),
            "net": net_name,
            "from": from_str,
            "to": to_str,
            "route": route_style,
            "segments": line_uuids.len(),
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

    let net_uuid = resolve_schematic_net_uuid(project, &circuit, &schematic, net_name)?;

    // Find or create net segment
    let seg = if let Some(seg) = schematic
        .net_segments
        .iter_mut()
        .find(|s| s.net == net_uuid)
    {
        seg
    } else {
        schematic.net_segments.push(SchematicNetSegment {
            uuid: new_uuid(),
            net: net_uuid,
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

// ===========================================================================
// Hierarchy commands
// ===========================================================================

fn add_sheet(
    project: &std::path::Path,
    sch_name: &str,
    name: &str,
    target: &str,
    grid: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let position = parse_grid(grid)?;
    let child = if {
        let target_path = project.join(format!("schematics/{target}.json"));
        target_path.exists()
    } {
        project_io::read_schematic(project, target)?
    } else {
        let child = Schematic {
            uuid: new_uuid(),
            name: target.to_string(),
            grid: schematic.grid,
            symbols: vec![],
            net_segments: vec![],
            sheet_refs: vec![],
            hierarchical_ports: vec![],
            power_ports: vec![],
            power_flags: vec![],
            bus_segments: vec![],
            bus_entries: vec![],
            bus_aliases: vec![],
        };
        project_io::write_schematic(project, target, &child)?;
        child
    };

    let mut pins = Vec::new();
    for port in &child.hierarchical_ports {
        pins.push(SheetRefPin {
            uuid: new_uuid(),
            name: port.name.clone(),
            port_ref: port.uuid,
            side: port.side,
            offset: next_sheet_pin_offset(&pins, port.side),
            net: None,
        });
    }

    let sheet_ref = SheetRef {
        uuid: new_uuid(),
        name: name.to_string(),
        target_schematic: target.to_string(),
        position,
        width: 20.0,
        height: 15.0,
        pins,
    };

    let uuid_str = sheet_ref.uuid.to_string();
    schematic.sheet_refs.push(sheet_ref);
    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "sheet_ref": {
            "uuid": uuid_str,
            "name": name,
            "target": target,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn add_sheet_pin(
    project: &std::path::Path,
    sch_name: &str,
    sheet_selector: &str,
    port_selector: &str,
    net_name: &str,
    pin_name: Option<&str>,
    side: Option<&str>,
    offset: Option<f64>,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let parent_net = resolve_schematic_net_uuid(project, &circuit, &schematic, net_name)?;
    let sheet_index = schematic
        .sheet_refs
        .iter()
        .position(|sheet| sheet.uuid.to_string() == sheet_selector || sheet.name == sheet_selector)
        .ok_or_else(|| format!("Sheet reference '{}' not found", sheet_selector))?;
    let target_name = schematic.sheet_refs[sheet_index].target_schematic.clone();
    let child = project_io::read_schematic(project, &target_name)?;
    let port = resolve_hierarchical_port(port_selector, &child)?;

    if schematic.sheet_refs[sheet_index]
        .pins
        .iter()
        .any(|pin| pin.port_ref == port.uuid)
    {
        return Err(format!(
            "Sheet reference '{}' already has a pin bound to child port '{}'",
            schematic.sheet_refs[sheet_index].name, port.name
        )
        .into());
    }

    let pin_side = match side {
        Some(value) => parse_sheet_side(value)?,
        None => port.side,
    };
    let pin = SheetRefPin {
        uuid: new_uuid(),
        name: pin_name.unwrap_or(&port.name).to_string(),
        port_ref: port.uuid,
        side: pin_side,
        offset: offset.unwrap_or_else(|| {
            next_sheet_pin_offset(&schematic.sheet_refs[sheet_index].pins, pin_side)
        }),
        net: Some(parent_net),
    };
    let pin_uuid = pin.uuid;
    let pin_label = pin.name.clone();
    schematic.sheet_refs[sheet_index].pins.push(pin);

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "sheet_pin": {
            "uuid": pin_uuid.to_string(),
            "sheet": schematic.sheet_refs[sheet_index].name,
            "name": pin_label,
            "net": net_name,
            "port": port.name,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn add_port(
    project: &std::path::Path,
    sch_name: &str,
    name: &str,
    net_name: &str,
    grid: &str,
    side_str: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let position = parse_grid(grid)?;

    let net_uuid = resolve_schematic_net_uuid(project, &circuit, &schematic, net_name)?;
    let side = parse_sheet_side(side_str)?;

    let port = HierarchicalPort {
        uuid: new_uuid(),
        name: name.to_string(),
        position,
        side,
        net: net_uuid,
    };

    let uuid_str = port.uuid.to_string();
    schematic.hierarchical_ports.push(port);
    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "port": { "uuid": uuid_str, "name": name, "net": net_name }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn add_power_port(
    project: &std::path::Path,
    sch_name: &str,
    net_name: &str,
    grid: &str,
    style: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let position = parse_grid(grid)?;

    let net_uuid = resolve_schematic_net_uuid(project, &circuit, &schematic, net_name)?;

    let pp = PowerPort {
        uuid: new_uuid(),
        net: net_uuid,
        position,
        rotation: Angle(0.0),
        style: style.to_string(),
    };

    let uuid_str = pp.uuid.to_string();
    schematic.power_ports.push(pp);
    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "power_port": { "uuid": uuid_str, "net": net_name, "style": style }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn add_power_flag(
    project: &std::path::Path,
    sch_name: &str,
    net_name: &str,
    grid: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let position = parse_grid(grid)?;
    let net_uuid = resolve_schematic_net_uuid(project, &circuit, &schematic, net_name)?;

    let flag = PowerFlag {
        uuid: new_uuid(),
        net: net_uuid,
        position,
    };
    let flag_uuid = flag.uuid;
    schematic.power_flags.push(flag);
    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "power_flag": {
            "uuid": flag_uuid.to_string(),
            "net": net_name,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// Bus commands
// ===========================================================================

fn add_bus(
    project: &std::path::Path,
    sch_name: &str,
    label: &str,
    from_str: &str,
    to_str: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let from_pos = parse_grid(from_str)?;
    let to_pos = parse_grid(to_str)?;

    let _members = bus_members_from_label(label, &schematic.bus_aliases)?;

    let j_from = new_uuid();
    let j_to = new_uuid();

    let bus_seg = BusSegment {
        uuid: new_uuid(),
        label: label.to_string(),
        junctions: vec![
            Junction {
                uuid: j_from,
                position: from_pos,
            },
            Junction {
                uuid: j_to,
                position: to_pos,
            },
        ],
        lines: vec![SchematicLine {
            uuid: new_uuid(),
            width: 0.3,
            from: LineEndpoint::Junction { junction: j_from },
            to: LineEndpoint::Junction { junction: j_to },
        }],
    };

    let uuid_str = bus_seg.uuid.to_string();
    schematic.bus_segments.push(bus_seg);
    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "bus": { "uuid": uuid_str, "label": label }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn add_bus_alias(
    project: &std::path::Path,
    sch_name: &str,
    name: &str,
    members: &[String],
) -> Result<()> {
    project_io::ensure_project(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    if members.is_empty() {
        return Err("Bus aliases require at least one member".into());
    }
    if schematic.bus_aliases.iter().any(|alias| alias.name == name) {
        return Err(format!("Bus alias '{}' already exists", name).into());
    }

    let alias = BusAlias {
        uuid: new_uuid(),
        name: name.to_string(),
        members: members
            .iter()
            .map(|member| member.trim().to_string())
            .collect(),
    };
    let alias_uuid = alias.uuid;
    schematic.bus_aliases.push(alias);
    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "bus_alias": {
            "uuid": alias_uuid.to_string(),
            "name": name,
            "members": members,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn add_bus_entry(
    project: &std::path::Path,
    sch_name: &str,
    bus_uuid: Uuid,
    net_name: &str,
    member: &str,
    grid: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let position = parse_grid(grid)?;

    let bus = schematic
        .bus_segments
        .iter()
        .find(|bus| bus.uuid == bus_uuid)
        .ok_or_else(|| format!("Bus segment '{}' not found", bus_uuid))?;
    let members = bus_members_from_label(&bus.label, &schematic.bus_aliases)?;
    if !members.iter().any(|candidate| candidate == member) {
        return Err(format!(
            "Bus member '{}' is not part of bus '{}'. Valid members: {}",
            member,
            bus.label,
            members.join(", ")
        )
        .into());
    }

    let net_uuid = resolve_schematic_net_uuid(project, &circuit, &schematic, net_name)?;
    if canonical_bus_member(member) != canonical_bus_member(net_name) {
        return Err(format!(
            "Bus member '{}' does not match scalar net '{}'. Use a matching member like '{}'",
            member,
            net_name,
            canonical_bus_member(member)
        )
        .into());
    }

    let entry = BusEntry {
        uuid: new_uuid(),
        position,
        bus_segment: bus_uuid,
        net: net_uuid,
        member_name: member.to_string(),
    };

    let uuid_str = entry.uuid.to_string();
    schematic.bus_entries.push(entry);
    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "bus_entry": { "uuid": uuid_str, "net": net_name, "member": member }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

#[cfg(test)]
mod tests {
    use std::path::{Path, PathBuf};

    use tempfile::TempDir;
    use volt_core::common::SignalRole;
    use volt_core::library::{ComponentVariant, Gate, LibraryMeta, PinMapping, Signal};

    use super::*;
    use crate::commands::net::{NetCommands, net_command};
    use crate::commands::new_project;

    fn create_temp_project() -> (TempDir, PathBuf) {
        let dir = tempfile::tempdir().unwrap();
        let project = dir.path().join("proj");
        new_project("proj", Some(&project)).unwrap();
        (dir, project)
    }

    fn seed_multi_gate_component(project: &Path) -> (Uuid, Uuid, Uuid) {
        let component_uuid = new_uuid();
        let variant_uuid = new_uuid();
        let gate_a_uuid = new_uuid();
        let gate_b_uuid = new_uuid();
        let symbol_a_uuid = new_uuid();
        let symbol_b_uuid = new_uuid();
        let signal_uuid = new_uuid();
        let pin_a_uuid = new_uuid();
        let pin_b_uuid = new_uuid();

        let symbol_a = Symbol {
            meta: make_lib_meta(symbol_a_uuid, "SYM_A"),
            pins: vec![SymbolPin {
                uuid: pin_a_uuid,
                name: "1".into(),
                pin_name: "OUT".into(),
                position: Position::new(1.0, 0.0),
                rotation: Angle(0.0),
                length: 2.54,
                name_position: Position::new(0.0, 0.0),
                name_rotation: Angle(0.0),
                name_height: 2.5,
                name_align: Alignment {
                    h: HAlign::Left,
                    v: VAlign::Center,
                },
            }],
            polygons: vec![],
            texts: make_symbol_text_templates(),
            grid_interval: 2.54,
        };
        let symbol_b = Symbol {
            meta: make_lib_meta(symbol_b_uuid, "SYM_B"),
            pins: vec![SymbolPin {
                uuid: pin_b_uuid,
                name: "1".into(),
                pin_name: "OUT".into(),
                position: Position::new(2.0, 0.0),
                rotation: Angle(0.0),
                length: 2.54,
                name_position: Position::new(0.0, 0.0),
                name_rotation: Angle(0.0),
                name_height: 2.5,
                name_align: Alignment {
                    h: HAlign::Left,
                    v: VAlign::Center,
                },
            }],
            polygons: vec![],
            texts: make_symbol_text_templates(),
            grid_interval: 2.54,
        };
        project_io::write_library_element(project, "symbols", &symbol_a_uuid, &symbol_a).unwrap();
        project_io::write_library_element(project, "symbols", &symbol_b_uuid, &symbol_b).unwrap();

        let component = Component {
            meta: make_lib_meta(component_uuid, "Dual Buffer"),
            prefix: "U".into(),
            default_value: String::new(),
            schematic_only: false,
            attributes: vec![],
            signals: vec![Signal {
                uuid: signal_uuid,
                name: "OUT".into(),
                role: SignalRole::Output,
                required: false,
                negated: false,
                clock: false,
                forced_net: String::new(),
            }],
            variants: vec![ComponentVariant {
                uuid: variant_uuid,
                norm: String::new(),
                name: "default".into(),
                description: String::new(),
                gates: vec![
                    Gate {
                        uuid: gate_a_uuid,
                        symbol: symbol_a_uuid,
                        position: Position::new(0.0, 0.0),
                        rotation: Angle(0.0),
                        required: true,
                        suffix: "A".into(),
                        pin_mappings: vec![PinMapping {
                            pin: pin_a_uuid,
                            signal: signal_uuid,
                        }],
                    },
                    Gate {
                        uuid: gate_b_uuid,
                        symbol: symbol_b_uuid,
                        position: Position::new(0.0, 0.0),
                        rotation: Angle(0.0),
                        required: true,
                        suffix: "B".into(),
                        pin_mappings: vec![PinMapping {
                            pin: pin_b_uuid,
                            signal: signal_uuid,
                        }],
                    },
                ],
            }],
        };
        project_io::write_library_element(project, "components", &component_uuid, &component)
            .unwrap();

        let mut circuit = project_io::read_circuit(project).unwrap();
        circuit.components.push(ComponentInstance {
            uuid: new_uuid(),
            lib_component: component_uuid,
            lib_variant: variant_uuid,
            name: "U1".into(),
            value: String::new(),
            lock_assembly: false,
            device_assignments: vec![],
            signal_connections: vec![],
        });
        project_io::write_circuit(project, &circuit).unwrap();

        (gate_a_uuid, gate_b_uuid, pin_b_uuid)
    }

    fn make_lib_meta(uuid: Uuid, name: &str) -> LibraryMeta {
        LibraryMeta {
            uuid,
            name: name.into(),
            description: String::new(),
            keywords: String::new(),
            author: "test".into(),
            version: "0.1".into(),
            created: chrono::Utc::now(),
            deprecated: false,
            category: None,
        }
    }

    fn make_symbol_text_templates() -> Vec<SymbolText> {
        vec![
            SymbolText {
                uuid: new_uuid(),
                layer: Layer::SchNames,
                value: "{{NAME}}".into(),
                position: Position::new(0.0, 2.54),
                rotation: Angle(0.0),
                height: 1.27,
                align: Alignment {
                    h: HAlign::Left,
                    v: VAlign::Bottom,
                },
                lock: false,
            },
            SymbolText {
                uuid: new_uuid(),
                layer: Layer::SchValues,
                value: "{{VALUE}}".into(),
                position: Position::new(0.0, -2.54),
                rotation: Angle(0.0),
                height: 1.27,
                align: Alignment {
                    h: HAlign::Left,
                    v: VAlign::Top,
                },
                lock: false,
            },
        ]
    }

    fn add_net(project: &Path, name: &str, scope: &str, schematic: Option<&str>, power: bool) {
        net_command(NetCommands::Add {
            project: project.to_path_buf(),
            name: name.to_string(),
            scope: scope.to_string(),
            schematic: schematic.map(str::to_string),
            power,
        })
        .unwrap();
    }

    #[test]
    fn parse_endpoint_resolves_gate_qualified_symbols() {
        let (_tmp, project) = create_temp_project();
        let (_gate_a_uuid, gate_b_uuid, pin_b_uuid) = seed_multi_gate_component(&project);
        place_symbol(&project, "main", "U1", Some("A"), "10,10", 0.0, false).unwrap();
        place_symbol(&project, "main", "U1", Some("B"), "20,10", 0.0, false).unwrap();

        let circuit = project_io::read_circuit(&project).unwrap();
        let schematic = project_io::read_schematic(&project, "main").unwrap();
        let symbol_b_uuid = schematic
            .symbols
            .iter()
            .find(|sym| sym.lib_gate == gate_b_uuid)
            .map(|sym| sym.uuid)
            .unwrap();

        let (endpoint, junction) =
            parse_endpoint("U1.B:OUT", &circuit, &schematic, &project).unwrap();
        assert!(junction.is_none());
        assert_eq!(
            endpoint,
            LineEndpoint::Symbol {
                symbol: symbol_b_uuid,
                pin: pin_b_uuid,
            }
        );

        let pos = resolve_pin_position("U1.B", "OUT", &circuit, &schematic, &project).unwrap();
        assert!((pos.x - (20.0 * 2.54 + 2.0)).abs() < 1e-6);
        assert!((pos.y - 10.0 * 2.54).abs() < 1e-6);
    }

    #[test]
    fn parse_endpoint_rejects_ambiguous_multi_gate_pin_names() {
        let (_tmp, project) = create_temp_project();
        seed_multi_gate_component(&project);
        place_symbol(&project, "main", "U1", Some("A"), "10,10", 0.0, false).unwrap();
        place_symbol(&project, "main", "U1", Some("B"), "20,10", 0.0, false).unwrap();

        let circuit = project_io::read_circuit(&project).unwrap();
        let schematic = project_io::read_schematic(&project, "main").unwrap();
        let err = parse_endpoint("U1:OUT", &circuit, &schematic, &project).unwrap_err();
        assert!(err.to_string().contains("ambiguous"));
        assert!(err.to_string().contains("U1.A:OUT"));
    }

    #[test]
    fn move_symbol_requires_gate_for_multi_gate_component() {
        let (_tmp, project) = create_temp_project();
        seed_multi_gate_component(&project);
        place_symbol(&project, "main", "U1", Some("A"), "10,10", 0.0, false).unwrap();
        place_symbol(&project, "main", "U1", Some("B"), "20,10", 0.0, false).unwrap();

        let err = move_symbol(&project, "main", "U1", None, "30,10").unwrap_err();
        assert!(err.to_string().contains("Use --gate"));
    }

    #[test]
    fn move_symbol_targets_selected_gate() {
        let (_tmp, project) = create_temp_project();
        let (_gate_a_uuid, gate_b_uuid, _pin_b_uuid) = seed_multi_gate_component(&project);
        place_symbol(&project, "main", "U1", Some("A"), "10,10", 0.0, false).unwrap();
        place_symbol(&project, "main", "U1", Some("B"), "20,10", 0.0, false).unwrap();

        move_symbol(&project, "main", "U1", Some("B"), "30,12").unwrap();

        let schematic = project_io::read_schematic(&project, "main").unwrap();
        let gate_a = schematic
            .symbols
            .iter()
            .find(|sym| sym.lib_gate != gate_b_uuid)
            .unwrap();
        let gate_b = schematic
            .symbols
            .iter()
            .find(|sym| sym.lib_gate == gate_b_uuid)
            .unwrap();
        assert_eq!(gate_a.position, Position::new(10.0 * 2.54, 10.0 * 2.54));
        assert_eq!(gate_b.position, Position::new(30.0 * 2.54, 12.0 * 2.54));
    }

    #[test]
    fn move_symbol_field_targets_selected_gate() {
        let (_tmp, project) = create_temp_project();
        let (_gate_a_uuid, gate_b_uuid, _pin_b_uuid) = seed_multi_gate_component(&project);
        place_symbol(&project, "main", "U1", Some("A"), "10,10", 0.0, false).unwrap();
        place_symbol(&project, "main", "U1", Some("B"), "20,10", 0.0, false).unwrap();

        move_symbol_field(&project, "main", "U1", Some("B"), "value", "25,11").unwrap();

        let schematic = project_io::read_schematic(&project, "main").unwrap();
        let gate_a = schematic
            .symbols
            .iter()
            .find(|sym| sym.lib_gate != gate_b_uuid)
            .unwrap();
        let gate_b = schematic
            .symbols
            .iter()
            .find(|sym| sym.lib_gate == gate_b_uuid)
            .unwrap();
        let gate_a_value = gate_a
            .texts
            .iter()
            .find(|text| text.layer == Layer::SchValues)
            .unwrap();
        let gate_b_value = gate_b
            .texts
            .iter()
            .find(|text| text.layer == Layer::SchValues)
            .unwrap();
        assert_ne!(gate_a_value.position, gate_b_value.position);
        assert_eq!(
            gate_b_value.position,
            Position::new(25.0 * 2.54, 11.0 * 2.54)
        );
    }

    #[test]
    fn reset_symbol_field_targets_selected_gate() {
        let (_tmp, project) = create_temp_project();
        let (_gate_a_uuid, gate_b_uuid, _pin_b_uuid) = seed_multi_gate_component(&project);
        place_symbol(&project, "main", "U1", Some("A"), "10,10", 0.0, false).unwrap();
        place_symbol(&project, "main", "U1", Some("B"), "20,10", 0.0, false).unwrap();
        move_symbol_field(&project, "main", "U1", Some("B"), "value", "25,11").unwrap();

        reset_symbol_field(&project, "main", "U1", Some("B"), Some("value")).unwrap();

        let schematic = project_io::read_schematic(&project, "main").unwrap();
        let gate_b = schematic
            .symbols
            .iter()
            .find(|sym| sym.lib_gate == gate_b_uuid)
            .unwrap();
        let gate_b_value = gate_b
            .texts
            .iter()
            .find(|text| text.layer == Layer::SchValues)
            .unwrap();
        assert_eq!(
            gate_b_value.position,
            Position::new(20.0 * 2.54, 10.0 * 2.54 - 2.54)
        );
    }

    #[test]
    fn local_net_names_are_scoped_per_sheet() {
        let (_tmp, project) = create_temp_project();
        add_sheet(&project, "main", "LEFT", "left", "5,5").unwrap();
        add_sheet(&project, "main", "RIGHT", "right", "25,5").unwrap();

        add_net(&project, "DATA", "local", Some("left"), false);
        add_net(&project, "DATA", "local", Some("right"), false);

        add_label(&project, "left", "DATA", "4,4", 0.0).unwrap();
        add_label(&project, "right", "DATA", "6,6", 0.0).unwrap();

        let circuit = project_io::read_circuit(&project).unwrap();
        let left = project_io::read_schematic(&project, "left").unwrap();
        let right = project_io::read_schematic(&project, "right").unwrap();

        let left_net = circuit
            .nets
            .iter()
            .find(|net| {
                net.name == "DATA"
                    && net.scope == NetScope::Local
                    && net.owner_sheet == Some(left.uuid)
            })
            .unwrap();
        let right_net = circuit
            .nets
            .iter()
            .find(|net| {
                net.name == "DATA"
                    && net.scope == NetScope::Local
                    && net.owner_sheet == Some(right.uuid)
            })
            .unwrap();

        assert_ne!(left_net.uuid, right_net.uuid);
        assert_eq!(left.net_segments[0].net, left_net.uuid);
        assert_eq!(right.net_segments[0].net, right_net.uuid);
    }

    #[test]
    fn sheet_pins_and_ports_can_be_wired_from_cli() {
        let (_tmp, project) = create_temp_project();
        add_net(&project, "IO", "global", None, false);
        add_sheet(&project, "main", "CHILD", "child", "10,10").unwrap();
        add_port(&project, "child", "PORT_IO", "IO", "5,5", "left").unwrap();
        add_sheet_pin(&project, "main", "CHILD", "PORT_IO", "IO", None, None, None).unwrap();

        add_wire(&project, "child", "IO", "port:PORT_IO", "8,5", "direct").unwrap();
        add_wire(
            &project,
            "main",
            "IO",
            "sheet:CHILD:PORT_IO",
            "20,10",
            "direct",
        )
        .unwrap();

        let child = project_io::read_schematic(&project, "child").unwrap();
        let parent = project_io::read_schematic(&project, "main").unwrap();
        let parent_sheet = parent
            .sheet_refs
            .iter()
            .find(|sheet| sheet.name == "CHILD")
            .unwrap();
        assert_eq!(parent_sheet.pins.len(), 1);

        let child_line = &child.net_segments[0].lines[0];
        assert!(matches!(child_line.from, LineEndpoint::HierPort { .. }));
        let parent_line = &parent.net_segments[0].lines[0];
        assert!(matches!(parent_line.from, LineEndpoint::SheetPin { .. }));
    }

    #[test]
    fn bus_aliases_validate_members_and_render() {
        let (_tmp, project) = create_temp_project();
        add_net(&project, "D0", "global", None, false);
        add_net(&project, "VCC", "global", None, true);
        add_bus_alias(&project, "main", "DATA", &["D0".into(), "D1".into()]).unwrap();
        add_bus(&project, "main", "DATA", "5,5", "15,5").unwrap();
        let schematic = project_io::read_schematic(&project, "main").unwrap();
        let bus_uuid = schematic.bus_segments[0].uuid;
        add_bus_entry(&project, "main", bus_uuid, "D0", "D0", "8,5").unwrap();
        let err = add_bus_entry(&project, "main", bus_uuid, "D0", "D2", "9,5").unwrap_err();
        assert!(err.to_string().contains("not part of bus"));

        add_power_port(&project, "main", "VCC", "20,5", "vcc").unwrap();
        add_power_flag(&project, "main", "VCC", "22,5").unwrap();

        let output = project.join("bus.svg");
        super::super::render::render_schematic(&project, "main", &output).unwrap();
        let svg = std::fs::read_to_string(output).unwrap();
        assert!(svg.contains("DATA"));
        assert!(svg.contains("D0"));
        assert!(svg.contains("PWR VCC"));
    }
}
