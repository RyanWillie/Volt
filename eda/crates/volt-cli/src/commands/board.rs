//! `volt-eda board` subcommands.

use std::fs;
use std::path::PathBuf;

use clap::Subcommand;

use volt_core::common::*;
use volt_core::library::{Device, Package};
use volt_core::project::*;

use super::project_io::{self, Result};

#[derive(Subcommand)]
pub enum BoardCommands {
    /// Create a board from the project circuit
    Init {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Board name (without .json)
        #[arg(long, default_value = "default")]
        name: String,
    },
    /// Define the board outline
    Outline {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "default")]
        board: String,
        /// Rectangular outline shorthand "WxH" in mm (e.g. "100x80")
        #[arg(long)]
        rect: Option<String>,
        /// Explicit polygon vertices as "x1,y1;x2,y2;..." in mm
        #[arg(long)]
        vertices: Option<String>,
    },
    /// Place a device footprint on the board
    Place {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "default")]
        board: String,
        /// Component designator (e.g. "R1")
        #[arg(long)]
        component: String,
        /// X position in mm
        #[arg(long)]
        x: f64,
        /// Y position in mm
        #[arg(long)]
        y: f64,
        /// Rotation in degrees
        #[arg(long, default_value = "0")]
        rotation: f64,
        /// Place on bottom side
        #[arg(long, default_value = "false")]
        flip: bool,
        /// Lock placement
        #[arg(long, default_value = "false")]
        lock: bool,
    },
    /// Move a placed device to a new position
    Move {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "default")]
        board: String,
        /// Component designator to move
        #[arg(long)]
        component: String,
        /// New X position in mm
        #[arg(long)]
        x: f64,
        /// New Y position in mm
        #[arg(long)]
        y: f64,
        /// New rotation in degrees
        #[arg(long)]
        rotation: Option<f64>,
        /// Flip to bottom side
        #[arg(long)]
        flip: Option<bool>,
    },
    /// Route a copper trace between two points
    Trace {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "default")]
        board: String,
        /// Net name
        #[arg(long)]
        net: String,
        /// From endpoint: "component:pad" or "x,y"
        #[arg(long)]
        from: String,
        /// To endpoint: "component:pad" or "x,y"
        #[arg(long)]
        to: String,
        /// Copper layer
        #[arg(long, default_value = "top_copper")]
        layer: String,
        /// Routing style: "manhattan" or "direct"
        #[arg(long, default_value = "manhattan")]
        route: String,
        /// Trace width in mm (None = use design rules default)
        #[arg(long)]
        width: Option<f64>,
    },
    /// Add a via
    Via {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "default")]
        board: String,
        /// Net name
        #[arg(long)]
        net: String,
        /// X position in mm
        #[arg(long)]
        x: f64,
        /// Y position in mm
        #[arg(long)]
        y: f64,
        /// Drill diameter in mm
        #[arg(long)]
        drill: f64,
        /// From layer
        #[arg(long)]
        from_layer: String,
        /// To layer
        #[arg(long)]
        to_layer: String,
    },
    /// Add a copper pour / fill zone
    Plane {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "default")]
        board: String,
        /// Net name
        #[arg(long)]
        net: String,
        /// Copper layer
        #[arg(long)]
        layer: String,
        /// Polygon vertices as "x1,y1;x2,y2;..." in mm
        #[arg(long)]
        vertices: String,
        /// Fill priority (higher fills first)
        #[arg(long)]
        priority: u32,
        /// Pad connect style: "thermal", "solid", or "none"
        #[arg(long)]
        connect_style: String,
    },
    /// Add a mounting or tooling hole
    Hole {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "default")]
        board: String,
        /// X position in mm
        #[arg(long)]
        x: f64,
        /// Y position in mm
        #[arg(long)]
        y: f64,
        /// Hole diameter in mm
        #[arg(long)]
        diameter: f64,
        /// Include solder mask opening
        #[arg(long, default_value = "false")]
        stop_mask: bool,
    },
    /// Render the board to SVG
    Render {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "default")]
        board: String,
        /// Output SVG file path
        #[arg(long)]
        output: PathBuf,
    },
    /// Compute and display unrouted connections (ratsnest)
    Ratsnest {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "default")]
        board: String,
    },
    /// Auto-place all devices on the board
    Autoplace {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "default")]
        board: String,
    },
}

pub fn board_command(cmd: BoardCommands) -> Result<()> {
    match cmd {
        BoardCommands::Init { project, name } => {
            board_init(&project, &name)
        }
        BoardCommands::Outline { project, board, rect, vertices } => {
            board_outline(&project, &board, rect.as_deref(), vertices.as_deref())
        }
        BoardCommands::Place {
            project,
            board,
            component,
            x,
            y,
            rotation,
            flip,
            lock,
        } => board_place(&project, &board, &component, x, y, rotation, flip, lock),
        BoardCommands::Move {
            project,
            board,
            component,
            x,
            y,
            rotation,
            flip,
        } => board_move(&project, &board, &component, x, y, rotation, flip),
        BoardCommands::Trace { .. } => {
            stub("board trace")
        }
        BoardCommands::Via { .. } => {
            stub("board via")
        }
        BoardCommands::Plane { .. } => {
            stub("board plane")
        }
        BoardCommands::Hole { .. } => {
            stub("board hole")
        }
        BoardCommands::Render { .. } => {
            stub("board render")
        }
        BoardCommands::Ratsnest { .. } => {
            stub("board ratsnest")
        }
        BoardCommands::Autoplace { .. } => {
            stub("board autoplace")
        }
    }
}

fn stub(command: &str) -> Result<()> {
    let result = serde_json::json!({
        "status": "not_implemented",
        "command": command,
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// board outline
// ===========================================================================

/// Define or replace the board outline polygon.
///
/// Accepts either `--rect "WxH"` for a rectangular outline at the origin,
/// or `--vertices "x1,y1;x2,y2;..."` for an explicit polygon (auto-closed).
fn board_outline(
    project: &std::path::Path,
    name: &str,
    rect: Option<&str>,
    vertices_arg: Option<&str>,
) -> Result<()> {
    project_io::ensure_project(project)?;

    // Validate: exactly one of --rect or --vertices must be provided
    match (rect, vertices_arg) {
        (None, None) => {
            return Err("Either --rect or --vertices must be provided".into());
        }
        (Some(_), Some(_)) => {
            return Err("Cannot specify both --rect and --vertices".into());
        }
        _ => {}
    }

    let mut board = project_io::read_board(project, name)?;

    // Parse vertices from the provided argument
    let outline_vertices = if let Some(rect_str) = rect {
        parse_rect_outline(rect_str)?
    } else {
        parse_explicit_vertices(vertices_arg.unwrap())?
    };

    // Remove any existing BoardOutlines polygons
    board.polygons.retain(|p| p.layer != Layer::BoardOutlines);

    // Add the new outline polygon
    board.polygons.push(BoardPolygon {
        uuid: new_uuid(),
        layer: Layer::BoardOutlines,
        width: 0.0,
        fill: false,
        grab_area: false,
        lock: false,
        vertices: outline_vertices.clone(),
    });

    project_io::write_board(project, name, &board)?;

    // Compute bounds for the output
    let (width, height) = compute_bounds(&outline_vertices);

    let result = serde_json::json!({
        "status": "ok",
        "outline": {
            "vertices": outline_vertices.len(),
            "bounds": {
                "width": width,
                "height": height,
            }
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

/// Parse a "WxH" string into a closed rectangular polygon at the origin.
fn parse_rect_outline(rect_str: &str) -> Result<Vec<Vertex>> {
    let parts: Vec<&str> = rect_str.split('x').collect();
    if parts.len() != 2 {
        return Err(format!(
            "Invalid --rect format '{rect_str}': expected 'WxH' (e.g. '50x30')"
        )
        .into());
    }
    let w: f64 = parts[0]
        .trim()
        .parse()
        .map_err(|_| format!("Invalid width in --rect '{rect_str}'"))?;
    let h: f64 = parts[1]
        .trim()
        .parse()
        .map_err(|_| format!("Invalid height in --rect '{rect_str}'"))?;

    if w <= 0.0 || h <= 0.0 {
        return Err(format!("Board dimensions must be positive, got {w}x{h}").into());
    }

    Ok(vec![
        Vertex { position: Position::new(0.0, 0.0), angle: Angle(0.0) },
        Vertex { position: Position::new(w, 0.0), angle: Angle(0.0) },
        Vertex { position: Position::new(w, h), angle: Angle(0.0) },
        Vertex { position: Position::new(0.0, h), angle: Angle(0.0) },
        Vertex { position: Position::new(0.0, 0.0), angle: Angle(0.0) },
    ])
}

/// Parse an explicit vertex list "x1,y1;x2,y2;..." and auto-close if needed.
fn parse_explicit_vertices(vertices_str: &str) -> Result<Vec<Vertex>> {
    let pairs: Vec<&str> = vertices_str.split(';').collect();
    if pairs.len() < 3 {
        return Err("At least 3 vertices are required for a polygon".into());
    }

    let mut vertices = Vec::with_capacity(pairs.len() + 1);
    for (i, pair) in pairs.iter().enumerate() {
        let coords: Vec<&str> = pair.split(',').collect();
        if coords.len() != 2 {
            return Err(format!(
                "Invalid vertex at position {}: '{}' (expected 'x,y')",
                i + 1,
                pair
            )
            .into());
        }
        let x: f64 = coords[0]
            .trim()
            .parse()
            .map_err(|_| format!("Invalid x coordinate in vertex {}: '{}'", i + 1, coords[0]))?;
        let y: f64 = coords[1]
            .trim()
            .parse()
            .map_err(|_| format!("Invalid y coordinate in vertex {}: '{}'", i + 1, coords[1]))?;
        vertices.push(Vertex {
            position: Position::new(x, y),
            angle: Angle(0.0),
        });
    }

    // Auto-close: append first vertex if not already closed
    if let (Some(first), Some(last)) = (vertices.first(), vertices.last()) {
        if first.position != last.position {
            vertices.push(Vertex {
                position: first.position,
                angle: Angle(0.0),
            });
        }
    }

    Ok(vertices)
}

/// Compute the bounding box width and height from a list of vertices.
fn compute_bounds(vertices: &[Vertex]) -> (f64, f64) {
    if vertices.is_empty() {
        return (0.0, 0.0);
    }
    let mut min_x = f64::INFINITY;
    let mut max_x = f64::NEG_INFINITY;
    let mut min_y = f64::INFINITY;
    let mut max_y = f64::NEG_INFINITY;
    for v in vertices {
        min_x = min_x.min(v.position.x);
        max_x = max_x.max(v.position.x);
        min_y = min_y.min(v.position.y);
        max_y = max_y.max(v.position.y);
    }
    (max_x - min_x, max_y - min_y)
}

// ===========================================================================
// board init
// ===========================================================================

/// Create a board from the project circuit.
///
/// Reads the circuit, finds all components that have device assignments,
/// and populates the board's device list. Sets default design rules and
/// creates a 100×100 mm board outline.
fn board_init(project: &std::path::Path, name: &str) -> Result<()> {
    project_io::ensure_project(project)?;

    let board_path = project.join(format!("boards/{name}.json"));
    if board_path.exists() {
        return Err(format!("Board '{name}' already exists").into());
    }

    let circuit = project_io::read_circuit(project)?;

    // Build board devices from circuit components that have device assignments.
    // Each component may have a device assignment which tells us the physical
    // package/footprint to use on the board.
    let mut devices = Vec::new();
    let mut skipped = Vec::new();

    for comp in &circuit.components {
        // Find the first device assignment (use the first assembly variant's assignment)
        let assignment = comp.device_assignments.first();

        if let Some(da) = assignment {
            // Look up the device to get the footprint UUID
            let device: Device = match project_io::read_library_element(
                project, "devices", &da.device,
            ) {
                Ok(d) => d,
                Err(e) => {
                    skipped.push(serde_json::json!({
                        "name": comp.name,
                        "reason": format!("Failed to read device: {e}"),
                    }));
                    continue;
                }
            };

            // Look up the package to get the default footprint
            let package: Package = match project_io::read_library_element(
                project, "packages", &device.package,
            ) {
                Ok(p) => p,
                Err(e) => {
                    skipped.push(serde_json::json!({
                        "name": comp.name,
                        "reason": format!("Failed to read package: {e}"),
                    }));
                    continue;
                }
            };

            let footprint_uuid = package
                .footprints
                .first()
                .map(|f| f.uuid)
                .unwrap_or_else(new_uuid);

            devices.push(BoardDevice {
                component: comp.uuid,
                lib_device: da.device,
                lib_footprint: footprint_uuid,
                position: Position::new(0.0, 0.0), // unplaced — at origin
                rotation: Angle(0.0),
                flip: false,
                lock: false,
                texts: vec![],
            });
        } else {
            // No device assignment — try to find a device in the library
            // that references this component
            match find_device_for_component(project, comp) {
                Some((dev, pkg)) => {
                    let footprint_uuid = pkg
                        .footprints
                        .first()
                        .map(|f| f.uuid)
                        .unwrap_or_else(new_uuid);

                    devices.push(BoardDevice {
                        component: comp.uuid,
                        lib_device: dev.meta.uuid,
                        lib_footprint: footprint_uuid,
                        position: Position::new(0.0, 0.0),
                        rotation: Angle(0.0),
                        flip: false,
                        lock: false,
                        texts: vec![],
                    });
                }
                None => {
                    skipped.push(serde_json::json!({
                        "name": comp.name,
                        "reason": "No device assignment and no matching device found in library",
                    }));
                }
            }
        }
    }

    // Create board with default 100×100 mm outline
    let board = Board {
        uuid: new_uuid(),
        name: name.to_string(),
        grid: Grid {
            interval: 1.0,
            unit: GridUnit::Millimeters,
        },
        inner_layers: 0,
        thickness: 1.6,
        solder_resist: SolderResistColor::Green,
        silkscreen: SilkscreenColor::White,
        default_font: "newstroke.bene".to_string(),
        design_rules: serde_json::from_str("{}").unwrap(),
        drc_settings: serde_json::from_str("{}").unwrap(),
        fabrication_output_settings: FabricationOutputSettings::default(),
        devices,
        net_segments: vec![],
        planes: vec![],
        polygons: vec![BoardPolygon {
            uuid: new_uuid(),
            layer: Layer::BoardOutlines,
            width: 0.0,
            fill: false,
            grab_area: false,
            lock: false,
            vertices: vec![
                Vertex { position: Position::new(0.0, 0.0), angle: Angle(0.0) },
                Vertex { position: Position::new(100.0, 0.0), angle: Angle(0.0) },
                Vertex { position: Position::new(100.0, 100.0), angle: Angle(0.0) },
                Vertex { position: Position::new(0.0, 100.0), angle: Angle(0.0) },
                Vertex { position: Position::new(0.0, 0.0), angle: Angle(0.0) },
            ],
        }],
        holes: vec![],
    };

    // Ensure boards/ directory exists
    fs::create_dir_all(project.join("boards"))?;
    project_io::write_board(project, name, &board)?;

    let result = serde_json::json!({
        "status": "ok",
        "board": name,
        "uuid": board.uuid.to_string(),
        "devices": board.devices.len(),
        "skipped": skipped,
        "outline": "100x100mm",
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// board place
// ===========================================================================

/// Place a device footprint on the board.
///
/// Looks up the component by designator in the circuit, then finds (or creates)
/// the corresponding `BoardDevice` entry on the board and sets its position,
/// rotation, flip, and lock fields.
fn board_place(
    project: &std::path::Path,
    board_name: &str,
    designator: &str,
    x: f64,
    y: f64,
    rotation: f64,
    flip: bool,
    lock: bool,
) -> Result<()> {
    project_io::ensure_project(project)?;

    let circuit = project_io::read_circuit(project)?;
    let comp = circuit
        .components
        .iter()
        .find(|c| c.name == designator)
        .ok_or_else(|| format!("Component '{designator}' not found in circuit"))?;
    let comp_uuid = comp.uuid;

    let mut board = project_io::read_board(project, board_name)?;

    let action = if let Some(dev) = board.devices.iter_mut().find(|d| d.component == comp_uuid) {
        // Update existing device entry
        dev.position = Position::new(x, y);
        dev.rotation = Angle(rotation);
        dev.flip = flip;
        dev.lock = lock;
        "updated"
    } else {
        // Device not on board yet — try to discover device/package and add it
        let (dev, pkg) = discover_device_and_package(project, comp)?;
        let footprint_uuid = pkg
            .footprints
            .first()
            .map(|f| f.uuid)
            .unwrap_or_else(new_uuid);

        board.devices.push(BoardDevice {
            component: comp_uuid,
            lib_device: dev.meta.uuid,
            lib_footprint: footprint_uuid,
            position: Position::new(x, y),
            rotation: Angle(rotation),
            flip,
            lock,
            texts: vec![],
        });
        "placed"
    };

    project_io::write_board(project, board_name, &board)?;

    let result = serde_json::json!({
        "status": "ok",
        "component": designator,
        "position": { "x": x, "y": y },
        "rotation": rotation,
        "flip": flip,
        "action": action,
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

/// Try to find a device and package for a component — first via device
/// assignments, then by searching the library.
fn discover_device_and_package(
    project: &std::path::Path,
    comp: &ComponentInstance,
) -> Result<(Device, Package)> {
    // Try device assignments first
    if let Some(da) = comp.device_assignments.first() {
        let dev: Device = project_io::read_library_element(project, "devices", &da.device)?;
        let pkg: Package = project_io::read_library_element(project, "packages", &dev.package)?;
        return Ok((dev, pkg));
    }
    // Fall back to library search
    find_device_for_component(project, comp)
        .ok_or_else(|| {
            format!(
                "No device assignment and no matching device found in library for '{}'",
                comp.name
            )
            .into()
        })
}

// ===========================================================================
// board move
// ===========================================================================

/// Move an already-placed device to a new position on the board.
///
/// Optionally updates rotation and flip. Errors if the component is not
/// found on the board.
fn board_move(
    project: &std::path::Path,
    board_name: &str,
    designator: &str,
    x: f64,
    y: f64,
    rotation: Option<f64>,
    flip: Option<bool>,
) -> Result<()> {
    project_io::ensure_project(project)?;

    let circuit = project_io::read_circuit(project)?;
    let comp = circuit
        .components
        .iter()
        .find(|c| c.name == designator)
        .ok_or_else(|| format!("Component '{designator}' not found in circuit"))?;
    let comp_uuid = comp.uuid;

    let mut board = project_io::read_board(project, board_name)?;

    let dev = board
        .devices
        .iter_mut()
        .find(|d| d.component == comp_uuid)
        .ok_or_else(|| {
            format!("Component '{designator}' is not placed on board '{board_name}'")
        })?;

    dev.position = Position::new(x, y);
    if let Some(r) = rotation {
        dev.rotation = Angle(r);
    }
    if let Some(f) = flip {
        dev.flip = f;
    }

    let final_rotation = dev.rotation.0;
    let final_flip = dev.flip;

    project_io::write_board(project, board_name, &board)?;

    let result = serde_json::json!({
        "status": "ok",
        "component": designator,
        "position": { "x": x, "y": y },
        "rotation": final_rotation,
        "flip": final_flip,
        "action": "moved",
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// Helpers
// ===========================================================================

/// Search the project library for a Device that references the given component.
fn find_device_for_component(
    project: &std::path::Path,
    comp: &ComponentInstance,
) -> Option<(Device, Package)> {
    let devices_dir = project.join("library/devices");
    if !devices_dir.exists() {
        return None;
    }

    let entries = fs::read_dir(&devices_dir).ok()?;
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|e| e.to_str()) != Some("json") {
            continue;
        }
        if let Ok(dev) = project_io::read_json::<Device>(&path) {
            if dev.component == comp.lib_component {
                // Found a matching device — now load its package
                if let Ok(pkg) = project_io::read_library_element::<Package>(
                    project, "packages", &dev.package,
                ) {
                    return Some((dev, pkg));
                }
            }
        }
    }
    None
}
