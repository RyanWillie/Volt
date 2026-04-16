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
    /// Render the board to SVG (default) or interactive 3D HTML
    Render {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "default")]
        board: String,
        /// Output file path (.svg for 2D, .html for 3D)
        #[arg(long)]
        output: PathBuf,
        /// Output format: "svg" (2D) or "3d" (interactive HTML). Auto-detected from extension if omitted.
        #[arg(long)]
        format: Option<String>,
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
        BoardCommands::Trace {
            project,
            board,
            net,
            from,
            to,
            layer,
            route,
            width,
        } => board_trace(&project, &board, &net, &from, &to, &layer, &route, width),
        BoardCommands::Via {
            project,
            board,
            net,
            x,
            y,
            drill,
            from_layer,
            to_layer,
        } => board_via(&project, &board, &net, x, y, drill, &from_layer, &to_layer),
        BoardCommands::Plane {
            project,
            board,
            net,
            layer,
            vertices,
            priority,
            connect_style,
        } => board_plane(&project, &board, &net, &layer, &vertices, priority, &connect_style),
        BoardCommands::Hole {
            project,
            board,
            x,
            y,
            diameter,
            stop_mask,
        } => board_hole(&project, &board, x, y, diameter, stop_mask),
        BoardCommands::Render { project, board, output, format } => {
            let fmt = format.as_deref().unwrap_or_else(|| {
                match output.extension().and_then(|e| e.to_str()) {
                    Some("html" | "htm") => "3d",
                    _ => "svg",
                }
            });
            match fmt {
                "3d" | "3D" | "html" => super::board_render_3d::render_board_3d(&project, &board, &output),
                _ => super::board_render::render_board(&project, &board, &output),
            }
        }
        BoardCommands::Ratsnest { project, board } => {
            board_ratsnest(&project, &board)
        }
        BoardCommands::Autoplace { project, board } => {
            board_autoplace(&project, &board)
        }
    }
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
// board trace
// ===========================================================================

/// Format a copper layer for display in JSON output.
fn layer_display(layer: &Layer) -> String {
    match layer {
        Layer::TopCopper => "top_copper".to_string(),
        Layer::BottomCopper => "bottom_copper".to_string(),
        Layer::InnerCopper(n) => format!("inner_{n}"),
        other => format!("{other:?}"),
    }
}

/// Resolve the absolute board position of a footprint pad given device placement.
///
/// Computes: `device.position + rotate(pad.position, device.rotation)`,
/// with x-mirror if the device is flipped to the bottom side.
fn resolve_pad_position(
    board: &Board,
    project: &std::path::Path,
    device_component_uuid: &uuid::Uuid,
    pad_uuid: &uuid::Uuid,
) -> Result<Position> {
    let board_dev = board
        .devices
        .iter()
        .find(|d| d.component == *device_component_uuid)
        .ok_or_else(|| {
            format!(
                "BoardDevice with component {} not found",
                device_component_uuid
            )
        })?;

    // Load device → package → footprint to find the pad position
    let dev: Device =
        project_io::read_library_element(project, "devices", &board_dev.lib_device)?;
    let pkg: Package =
        project_io::read_library_element(project, "packages", &dev.package)?;

    let footprint = pkg
        .footprints
        .iter()
        .find(|f| f.uuid == board_dev.lib_footprint)
        .or_else(|| pkg.footprints.first())
        .ok_or_else(|| format!("No footprint found in package {}", dev.package))?;

    let fp_pad = footprint
        .pads
        .iter()
        .find(|p| p.uuid == *pad_uuid)
        .ok_or_else(|| format!("FootprintPad {} not found in footprint", pad_uuid))?;

    // Pad position relative to footprint origin
    let mut px = fp_pad.position.x;
    let py = fp_pad.position.y;

    // If device is flipped (bottom side), mirror x
    if board_dev.flip {
        px = -px;
    }

    // Rotate by device rotation
    let theta = board_dev.rotation.0.to_radians();
    let cos_t = theta.cos();
    let sin_t = theta.sin();
    let rx = px * cos_t - py * sin_t;
    let ry = px * sin_t + py * cos_t;

    // Translate by device position
    Ok(Position::new(
        board_dev.position.x + rx,
        board_dev.position.y + ry,
    ))
}

/// Parse a trace endpoint string.
///
/// Formats:
/// - `"R1:1"` → `TraceEndpoint::Device` (component pad)
/// - `"25.0,15.0"` → `TraceEndpoint::Junction` (creates junction at that position)
///
/// Returns the endpoint and optionally a new [`Junction`] to add to the net segment.
fn parse_trace_endpoint(
    s: &str,
    circuit: &Circuit,
    board: &Board,
    project: &std::path::Path,
) -> Result<(TraceEndpoint, Option<Junction>)> {
    if s.contains(':') {
        // "Component:Pad" format
        let parts: Vec<&str> = s.splitn(2, ':').collect();
        let designator = parts[0];
        let pad_name = parts[1];

        // Find component by designator
        let comp = circuit
            .components
            .iter()
            .find(|c| c.name == designator)
            .ok_or_else(|| format!("Component '{designator}' not found in circuit"))?;

        // Find BoardDevice by component UUID
        let board_dev = board
            .devices
            .iter()
            .find(|d| d.component == comp.uuid)
            .ok_or_else(|| {
                format!("Component '{designator}' is not placed on the board")
            })?;

        // Load device to get package UUID
        let dev: Device =
            project_io::read_library_element(project, "devices", &board_dev.lib_device)?;

        // Load package to find pad by name
        let pkg: Package =
            project_io::read_library_element(project, "packages", &dev.package)?;

        // Find PackagePad by name
        let pkg_pad = pkg
            .pads
            .iter()
            .find(|p| p.name == pad_name)
            .ok_or_else(|| {
                let pad_names: Vec<&str> = pkg.pads.iter().map(|p| p.name.as_str()).collect();
                format!(
                    "Pad '{pad_name}' not found in package '{}'. Available pads: {:?}",
                    pkg.meta.name, pad_names
                )
            })?;

        // Find footprint (matching board device's lib_footprint, or first)
        let footprint = pkg
            .footprints
            .iter()
            .find(|f| f.uuid == board_dev.lib_footprint)
            .or_else(|| pkg.footprints.first())
            .ok_or_else(|| {
                format!("No footprint in package '{}'", pkg.meta.name)
            })?;

        // Find FootprintPad that references this PackagePad
        let fp_pad = footprint
            .pads
            .iter()
            .find(|p| p.package_pad == pkg_pad.uuid)
            .ok_or_else(|| {
                format!(
                    "No footprint pad found for package pad '{}' in footprint '{}'",
                    pad_name, footprint.name
                )
            })?;

        Ok((
            TraceEndpoint::Device {
                device: comp.uuid,
                pad: fp_pad.uuid,
            },
            None,
        ))
    } else if s.contains(',') {
        // "x,y" format — create a junction
        let coords: Vec<&str> = s.split(',').collect();
        if coords.len() != 2 {
            return Err(
                format!("Invalid endpoint format '{s}': expected 'x,y'").into()
            );
        }
        let x: f64 = coords[0]
            .trim()
            .parse()
            .map_err(|_| format!("Invalid x coordinate in endpoint '{s}'"))?;
        let y: f64 = coords[1]
            .trim()
            .parse()
            .map_err(|_| format!("Invalid y coordinate in endpoint '{s}'"))?;

        let junction = Junction {
            uuid: new_uuid(),
            position: Position::new(x, y),
        };
        let endpoint = TraceEndpoint::Junction {
            junction: junction.uuid,
        };
        Ok((endpoint, Some(junction)))
    } else {
        Err(format!(
            "Invalid endpoint format '{s}': expected 'Component:Pad' or 'x,y'"
        )
        .into())
    }
}

/// Resolve the (x,y) position of a trace endpoint for routing calculations.
fn resolve_endpoint_position(
    endpoint: &TraceEndpoint,
    board: &Board,
    project: &std::path::Path,
    junctions: &[Junction],
) -> Result<Position> {
    match endpoint {
        TraceEndpoint::Device { device, pad } => {
            resolve_pad_position(board, project, device, pad)
        }
        TraceEndpoint::Junction { junction } => {
            // Search provided junctions first (includes newly created ones),
            // then fall back to existing net segment junctions on the board.
            let j = junctions
                .iter()
                .find(|j| j.uuid == *junction)
                .or_else(|| {
                    board
                        .net_segments
                        .iter()
                        .flat_map(|ns| &ns.junctions)
                        .find(|j| j.uuid == *junction)
                })
                .ok_or_else(|| format!("Junction {} not found", junction))?;
            Ok(j.position)
        }
        TraceEndpoint::Via { via } => {
            let v = board
                .net_segments
                .iter()
                .flat_map(|ns| &ns.vias)
                .find(|v| v.uuid == *via)
                .ok_or_else(|| format!("Via {} not found", via))?;
            Ok(v.position)
        }
    }
}

/// Route a copper trace between two endpoints on the board.
///
/// Supports "direct" (single straight trace) and "manhattan" (horizontal-then-
/// vertical with a junction at the bend) routing styles.
fn board_trace(
    project: &std::path::Path,
    board_name: &str,
    net_name: &str,
    from_str: &str,
    to_str: &str,
    layer_str: &str,
    route_style: &str,
    width_override: Option<f64>,
) -> Result<()> {
    project_io::ensure_project(project)?;

    // 1. Read circuit and find the net by name
    let circuit = project_io::read_circuit(project)?;
    let net = circuit
        .nets
        .iter()
        .find(|n| n.name == net_name)
        .ok_or_else(|| format!("Net '{net_name}' not found in circuit"))?;
    let net_uuid = net.uuid;

    // 2. Read the board
    let mut board = project_io::read_board(project, board_name)?;

    // 3. Parse the copper layer
    let layer = parse_layer(layer_str)?;

    // 4. Parse endpoints
    let (endpoint_from, junction_from) =
        parse_trace_endpoint(from_str, &circuit, &board, project)?;
    let (endpoint_to, junction_to) =
        parse_trace_endpoint(to_str, &circuit, &board, project)?;

    // 5. Determine trace width (explicit override or design-rules default)
    let width = width_override.unwrap_or(board.design_rules.default_trace_width);

    // 6. Find or create a BoardNetSegment for this net
    let seg_idx = if let Some(idx) = board
        .net_segments
        .iter()
        .position(|ns| ns.net == Some(net_uuid))
    {
        idx
    } else {
        board.net_segments.push(BoardNetSegment {
            uuid: new_uuid(),
            net: Some(net_uuid),
            traces: vec![],
            vias: vec![],
            junctions: vec![],
            pads: vec![],
        });
        board.net_segments.len() - 1
    };

    // Add any junction endpoints to the segment
    if let Some(j) = junction_from {
        board.net_segments[seg_idx].junctions.push(j);
    }
    if let Some(j) = junction_to {
        board.net_segments[seg_idx].junctions.push(j);
    }

    // 7/8. Create trace(s) based on routing style
    let num_segments = match route_style {
        "direct" => {
            let trace = Trace {
                uuid: new_uuid(),
                layer,
                width,
                from: endpoint_from,
                to: endpoint_to,
            };
            board.net_segments[seg_idx].traces.push(trace);
            1
        }
        "manhattan" => {
            // Resolve physical positions of both endpoints
            let all_junctions = &board.net_segments[seg_idx].junctions;
            let pos_from =
                resolve_endpoint_position(&endpoint_from, &board, project, all_junctions)?;
            let pos_to =
                resolve_endpoint_position(&endpoint_to, &board, project, all_junctions)?;

            // Bend point: go horizontal (keep from.y) then vertical (to to.y)
            let bend = Position::new(pos_to.x, pos_from.y);

            // If endpoints are already colinear, a single segment suffices
            let eps = 1e-6;
            if (pos_from.x - pos_to.x).abs() < eps
                || (pos_from.y - pos_to.y).abs() < eps
            {
                let trace = Trace {
                    uuid: new_uuid(),
                    layer,
                    width,
                    from: endpoint_from,
                    to: endpoint_to,
                };
                board.net_segments[seg_idx].traces.push(trace);
                1
            } else {
                // Create junction at the bend
                let bend_junction = Junction {
                    uuid: new_uuid(),
                    position: bend,
                };
                let bend_uuid = bend_junction.uuid;
                board.net_segments[seg_idx].junctions.push(bend_junction);

                // Horizontal segment: from → bend
                let trace1 = Trace {
                    uuid: new_uuid(),
                    layer,
                    width,
                    from: endpoint_from,
                    to: TraceEndpoint::Junction {
                        junction: bend_uuid,
                    },
                };
                // Vertical segment: bend → to
                let trace2 = Trace {
                    uuid: new_uuid(),
                    layer,
                    width,
                    from: TraceEndpoint::Junction {
                        junction: bend_uuid,
                    },
                    to: endpoint_to,
                };

                board.net_segments[seg_idx].traces.push(trace1);
                board.net_segments[seg_idx].traces.push(trace2);
                2
            }
        }
        other => {
            return Err(format!(
                "Unknown routing style '{other}': expected 'direct' or 'manhattan'"
            )
            .into());
        }
    };

    // 10. Write board back
    project_io::write_board(project, board_name, &board)?;

    // 11. Print JSON result
    let result = serde_json::json!({
        "status": "ok",
        "trace": {
            "net": net_name,
            "layer": layer_display(&layer),
            "width": width,
            "segments": num_segments,
            "route": route_style,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// board via
// ===========================================================================

/// Add a via to the board on a given net.
fn board_via(
    project: &std::path::Path,
    board_name: &str,
    net_name: &str,
    x: f64,
    y: f64,
    drill: f64,
    from_layer_str: &str,
    to_layer_str: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;

    // 1. Read circuit to find net UUID from --net name
    let circuit = project_io::read_circuit(project)?;
    let net = circuit
        .nets
        .iter()
        .find(|n| n.name == net_name)
        .ok_or_else(|| format!("Net '{net_name}' not found in circuit"))?;
    let net_uuid = net.uuid;

    // 2. Read board
    let mut board = project_io::read_board(project, board_name)?;

    // 3. Parse layers
    let from_layer = parse_layer(from_layer_str)?;
    let to_layer = parse_layer(to_layer_str)?;

    // 4. Create the Via
    let via = Via {
        uuid: new_uuid(),
        from_layer,
        to_layer,
        position: Position::new(x, y),
        drill,
        size: ViaSize::Auto,
        exposure: ViaExposure::Auto,
    };
    let via_uuid = via.uuid;

    // 5. Find or create a BoardNetSegment for this net (same pattern as board_trace)
    let seg_idx = if let Some(idx) = board
        .net_segments
        .iter()
        .position(|ns| ns.net == Some(net_uuid))
    {
        idx
    } else {
        board.net_segments.push(BoardNetSegment {
            uuid: new_uuid(),
            net: Some(net_uuid),
            traces: vec![],
            vias: vec![],
            junctions: vec![],
            pads: vec![],
        });
        board.net_segments.len() - 1
    };

    // 6. Push via to the net segment's vias vec
    board.net_segments[seg_idx].vias.push(via);

    // 7. Write board back
    project_io::write_board(project, board_name, &board)?;

    // 8. Print JSON result
    let result = serde_json::json!({
        "status": "ok",
        "via": {
            "uuid": via_uuid.to_string(),
            "position": { "x": x, "y": y },
            "drill": drill,
            "from_layer": layer_display(&from_layer),
            "to_layer": layer_display(&to_layer),
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// board hole
// ===========================================================================

/// Add a non-plated mounting or tooling hole to the board.
fn board_hole(
    project: &std::path::Path,
    board_name: &str,
    x: f64,
    y: f64,
    diameter: f64,
    stop_mask: bool,
) -> Result<()> {
    project_io::ensure_project(project)?;

    let mut board = project_io::read_board(project, board_name)?;

    let hole = BoardHole {
        uuid: new_uuid(),
        diameter,
        stop_mask: if stop_mask {
            StopMaskConfig::Auto
        } else {
            StopMaskConfig::Off
        },
        lock: false,
        path: vec![Vertex {
            position: Position::new(x, y),
            angle: Angle(0.0),
        }],
    };

    let uuid_str = hole.uuid.to_string();
    board.holes.push(hole);
    project_io::write_board(project, board_name, &board)?;

    let result = serde_json::json!({
        "status": "ok",
        "hole": {
            "uuid": uuid_str,
            "position": { "x": x, "y": y },
            "diameter": diameter,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// board plane
// ===========================================================================

/// Add a copper pour / fill zone to the board.
fn board_plane(
    project: &std::path::Path,
    board_name: &str,
    net_name: &str,
    layer_str: &str,
    vertices_str: &str,
    priority: u32,
    connect_style_str: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;

    // Read circuit to resolve net name → UUID
    let circuit = project_io::read_circuit(project)?;
    let net = circuit
        .nets
        .iter()
        .find(|n| n.name == net_name)
        .ok_or_else(|| format!("Net '{net_name}' not found in circuit"))?;
    let net_uuid = net.uuid;

    let mut board = project_io::read_board(project, board_name)?;

    let layer = parse_layer(layer_str)?;
    let vertices = parse_explicit_vertices(vertices_str)?;
    let connect_style = parse_connect_style(connect_style_str)?;

    let plane = Plane {
        uuid: new_uuid(),
        layer,
        net: net_uuid,
        priority: priority as i32,
        min_width: 0.2,
        min_copper_clearance: 0.2,
        min_board_clearance: 0.3,
        min_npth_clearance: 0.2,
        connect_style,
        thermal_gap: 0.3,
        thermal_spoke: 0.3,
        keep_islands: false,
        lock: false,
        vertices: vertices.clone(),
    };

    let uuid_str = plane.uuid.to_string();
    let vertex_count = vertices.len();
    board.planes.push(plane);
    project_io::write_board(project, board_name, &board)?;

    let result = serde_json::json!({
        "status": "ok",
        "plane": {
            "uuid": uuid_str,
            "net": net_name,
            "layer": layer_str,
            "vertices": vertex_count,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// board ratsnest
// ===========================================================================

/// Compute and display unrouted connections (ratsnest).
///
/// For each net, identifies which pads should be connected, builds a
/// connectivity graph from existing traces, and reports unconnected pad pairs.
fn board_ratsnest(
    project: &std::path::Path,
    board_name: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;

    let circuit = project_io::read_circuit(project)?;
    let board = project_io::read_board(project, board_name)?;

    let mut nets_output: Vec<serde_json::Value> = Vec::new();
    let mut total_unrouted: usize = 0;

    for net in &circuit.nets {
        // Collect all (component_uuid, signal_uuid) pairs connected to this net
        let mut pad_entries: Vec<PadEntry> = Vec::new();

        for comp in &circuit.components {
            for sc in &comp.signal_connections {
                if sc.net == Some(net.uuid) {
                    // This component signal is connected to this net.
                    // Find the board device for this component.
                    let board_dev = match board.devices.iter().find(|d| d.component == comp.uuid) {
                        Some(d) => d,
                        None => continue, // component not placed on board
                    };

                    // Load device to get pad mappings (signal → package pad)
                    let dev: Device = match project_io::read_library_element(
                        project, "devices", &board_dev.lib_device,
                    ) {
                        Ok(d) => d,
                        Err(_) => continue,
                    };

                    // Load package to get pad names and footprint pads
                    let pkg: Package = match project_io::read_library_element(
                        project, "packages", &dev.package,
                    ) {
                        Ok(p) => p,
                        Err(_) => continue,
                    };

                    let footprint = match pkg
                        .footprints
                        .iter()
                        .find(|f| f.uuid == board_dev.lib_footprint)
                        .or_else(|| pkg.footprints.first())
                    {
                        Some(f) => f,
                        None => continue,
                    };

                    // Find pad mapping: signal → package pad
                    for pm in &dev.pad_mappings {
                        if pm.signal == sc.signal {
                            // pm.pad is the PackagePad UUID
                            // Find the FootprintPad that references this PackagePad
                            let fp_pad = match footprint
                                .pads
                                .iter()
                                .find(|p| p.package_pad == pm.pad)
                            {
                                Some(p) => p,
                                None => continue,
                            };

                            // Get human-readable pad name
                            let pad_name = pkg
                                .pads
                                .iter()
                                .find(|p| p.uuid == pm.pad)
                                .map(|p| p.name.as_str())
                                .unwrap_or("?");

                            // Resolve absolute position on board
                            let position = match resolve_pad_position(
                                &board, project, &comp.uuid, &fp_pad.uuid,
                            ) {
                                Ok(pos) => pos,
                                Err(_) => continue,
                            };

                            pad_entries.push(PadEntry {
                                designator: comp.name.clone(),
                                pad_name: pad_name.to_string(),
                                fp_pad_uuid: fp_pad.uuid,
                                component_uuid: comp.uuid,
                                position,
                            });
                        }
                    }
                }
            }
        }

        if pad_entries.len() < 2 {
            // A net with 0 or 1 pads has nothing to route
            continue;
        }

        // Build connectivity from existing traces in the board's net segments
        // for this net. Use union-find to group connected pads.
        let n = pad_entries.len();
        let mut parent: Vec<usize> = (0..n).collect();

        // Union-find helpers (inline closures won't work well, use fn-style)
        fn find(parent: &mut Vec<usize>, mut x: usize) -> usize {
            while parent[x] != x {
                parent[x] = parent[parent[x]];
                x = parent[x];
            }
            x
        }
        fn union(parent: &mut Vec<usize>, a: usize, b: usize) {
            let ra = find(parent, a);
            let rb = find(parent, b);
            if ra != rb {
                parent[ra] = rb;
            }
        }

        // For each net segment belonging to this net, examine traces
        for ns in &board.net_segments {
            if ns.net != Some(net.uuid) {
                continue;
            }

            // Build a mapping from trace endpoint identifier to pad index
            // Trace endpoints can be Device{device,pad}, Junction, or Via.
            // We care about Device endpoints that match our pad_entries.
            for trace in &ns.traces {
                let from_idx = endpoint_to_pad_index(&trace.from, &pad_entries);
                let to_idx = endpoint_to_pad_index(&trace.to, &pad_entries);

                match (from_idx, to_idx) {
                    (Some(a), Some(b)) => {
                        union(&mut parent, a, b);
                    }
                    _ => {
                        // At least one endpoint is a junction/via. We need
                        // to do transitive connectivity through junctions/vias.
                        // We'll handle this below with a full graph approach.
                    }
                }
            }

            // For full connectivity through junctions and vias, build a
            // graph of all endpoints and do BFS/union-find.
            // Endpoint key: either "dev:<comp_uuid>:<pad_uuid>" or "junc:<uuid>" or "via:<uuid>"
            let mut endpoint_ids: Vec<String> = Vec::new();
            let mut ep_parent: Vec<usize> = Vec::new();

            let get_or_insert = |id: String, endpoint_ids: &mut Vec<String>, ep_parent: &mut Vec<usize>| -> usize {
                if let Some(pos) = endpoint_ids.iter().position(|x| *x == id) {
                    pos
                } else {
                    let idx = endpoint_ids.len();
                    endpoint_ids.push(id);
                    ep_parent.push(idx);
                    idx
                }
            };

            fn ep_find(ep_parent: &mut Vec<usize>, mut x: usize) -> usize {
                while ep_parent[x] != x {
                    ep_parent[x] = ep_parent[ep_parent[x]];
                    x = ep_parent[x];
                }
                x
            }
            fn ep_union(ep_parent: &mut Vec<usize>, a: usize, b: usize) {
                let ra = ep_find(ep_parent, a);
                let rb = ep_find(ep_parent, b);
                if ra != rb {
                    ep_parent[ra] = rb;
                }
            }

            for trace in &ns.traces {
                let from_id = endpoint_key(&trace.from);
                let to_id = endpoint_key(&trace.to);
                let fi = get_or_insert(from_id, &mut endpoint_ids, &mut ep_parent);
                let ti = get_or_insert(to_id, &mut endpoint_ids, &mut ep_parent);
                ep_union(&mut ep_parent, fi, ti);
            }

            // Now map pad_entries to their endpoint keys and union pad indices
            // if they share the same connected component in the endpoint graph.
            let mut pad_ep_indices: Vec<Option<usize>> = Vec::with_capacity(n);
            for pe in &pad_entries {
                let key = format!("dev:{}:{}", pe.component_uuid, pe.fp_pad_uuid);
                let ep_idx = endpoint_ids.iter().position(|x| *x == key);
                pad_ep_indices.push(ep_idx);
            }

            // Union pad_entries that share the same endpoint connected component
            for i in 0..n {
                for j in (i + 1)..n {
                    if let (Some(ei), Some(ej)) = (pad_ep_indices[i], pad_ep_indices[j]) {
                        if ep_find(&mut ep_parent, ei) == ep_find(&mut ep_parent, ej) {
                            union(&mut parent, i, j);
                        }
                    }
                }
            }
        }

        // Count connected groups
        let mut roots = std::collections::HashSet::new();
        for i in 0..n {
            roots.insert(find(&mut parent, i));
        }
        let connected_groups = roots.len();

        // If everything is in one group, no unrouted connections
        if connected_groups <= 1 {
            continue;
        }

        // Build unrouted pairs: for each pair of groups, pick the closest
        // pad pair (minimum spanning tree approach on groups).
        // Simple approach: connect groups using a minimal set of edges.
        // Use Kruskal's on groups: find the shortest inter-group edge for
        // each pair of groups.
        let mut unrouted: Vec<serde_json::Value> = Vec::new();

        // Collect one representative per group, then find shortest edges
        // between different groups to form MST.
        let mut group_parent: Vec<usize> = (0..n).collect();
        for i in 0..n {
            group_parent[i] = find(&mut parent, i);
        }

        // Build candidate edges between pads in different groups, sorted by distance
        let mut edges: Vec<(f64, usize, usize)> = Vec::new();
        for i in 0..n {
            for j in (i + 1)..n {
                if group_parent[i] != group_parent[j] {
                    let dx = pad_entries[i].position.x - pad_entries[j].position.x;
                    let dy = pad_entries[i].position.y - pad_entries[j].position.y;
                    let dist = (dx * dx + dy * dy).sqrt();
                    edges.push((dist, i, j));
                }
            }
        }
        edges.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap_or(std::cmp::Ordering::Equal));

        // Kruskal's to find minimum spanning tree across groups
        let mut mst_parent: Vec<usize> = (0..n).collect();

        fn mst_find(p: &mut Vec<usize>, mut x: usize) -> usize {
            while p[x] != x {
                p[x] = p[p[x]];
                x = p[x];
            }
            x
        }

        for (_, i, j) in &edges {
            let ri = mst_find(&mut mst_parent, group_parent[*i]);
            let rj = mst_find(&mut mst_parent, group_parent[*j]);
            if ri != rj {
                mst_parent[ri] = rj;
                unrouted.push(serde_json::json!({
                    "from": format!("{}:{}", pad_entries[*i].designator, pad_entries[*i].pad_name),
                    "to": format!("{}:{}", pad_entries[*j].designator, pad_entries[*j].pad_name),
                    "from_position": {
                        "x": pad_entries[*i].position.x,
                        "y": pad_entries[*i].position.y,
                    },
                    "to_position": {
                        "x": pad_entries[*j].position.x,
                        "y": pad_entries[*j].position.y,
                    },
                }));
            }
        }

        total_unrouted += unrouted.len();

        nets_output.push(serde_json::json!({
            "name": net.name,
            "total_pads": n,
            "connected_groups": connected_groups,
            "unrouted": unrouted,
        }));
    }

    let result = serde_json::json!({
        "status": "ok",
        "nets": nets_output,
        "total_unrouted": total_unrouted,
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

/// Internal helper for ratsnest: holds info about a pad on a net.
struct PadEntry {
    designator: String,
    pad_name: String,
    fp_pad_uuid: uuid::Uuid,
    component_uuid: uuid::Uuid,
    position: Position,
}

/// Convert a TraceEndpoint to a string key for union-find.
fn endpoint_key(ep: &TraceEndpoint) -> String {
    match ep {
        TraceEndpoint::Device { device, pad } => format!("dev:{device}:{pad}"),
        TraceEndpoint::Junction { junction } => format!("junc:{junction}"),
        TraceEndpoint::Via { via } => format!("via:{via}"),
    }
}

/// Find the pad_entries index that matches a TraceEndpoint::Device.
fn endpoint_to_pad_index(ep: &TraceEndpoint, pad_entries: &[PadEntry]) -> Option<usize> {
    match ep {
        TraceEndpoint::Device { device, pad } => {
            pad_entries.iter().position(|pe| {
                pe.component_uuid == *device && pe.fp_pad_uuid == *pad
            })
        }
        _ => None,
    }
}

/// Parse a layer string into a [`Layer`] enum variant.
fn parse_layer(s: &str) -> Result<Layer> {
    match s {
        "top_copper" | "top" => Ok(Layer::TopCopper),
        "bottom_copper" | "bottom" => Ok(Layer::BottomCopper),
        other => {
            if let Some(n_str) = other.strip_prefix("inner_") {
                let n: u8 = n_str
                    .parse()
                    .map_err(|_| format!("Invalid inner copper layer number: '{n_str}'"))?;
                Ok(Layer::InnerCopper(n))
            } else {
                Err(format!(
                    "Unknown layer '{other}': expected 'top_copper', 'top', 'bottom_copper', 'bottom', or 'inner_N'"
                ).into())
            }
        }
    }
}

/// Parse a connect-style string into a [`ConnectStyle`] enum variant.
fn parse_connect_style(s: &str) -> Result<ConnectStyle> {
    match s {
        "solid" => Ok(ConnectStyle::Solid),
        "thermal" => Ok(ConnectStyle::Thermal),
        "none" => Ok(ConnectStyle::None),
        other => Err(format!(
            "Unknown connect style '{other}': expected 'solid', 'thermal', or 'none'"
        ).into()),
    }
}

// ===========================================================================
// Helpers
// ===========================================================================

// ===========================================================================
// board autoplace
// ===========================================================================

/// Auto-place all devices on the board within the board outline.
///
/// Strategy:
/// 1. Read circuit, board, and all relevant library packages/footprints.
/// 2. Get the board outline bounding box.
/// 3. Compute each device's footprint bounding box.
/// 4. Build a connectivity graph: which devices share nets?
/// 5. Classify devices as "ICs" (3+ pads) or "passives" (1-2 pads).
/// 6. Place ICs on a grid in the center of the board, spaced apart.
/// 7. Place passives near the IC they're most connected to.
/// 8. Clamp everything within the board outline bounds.
/// 9. Write board back and print JSON summary.
fn board_autoplace(
    project: &std::path::Path,
    board_name: &str,
) -> Result<()> {
    use std::collections::HashMap;

    project_io::ensure_project(project)?;

    let circuit = project_io::read_circuit(project)?;
    let mut board = project_io::read_board(project, board_name)?;

    // -----------------------------------------------------------------------
    // 1. Board outline bounds
    // -----------------------------------------------------------------------
    let outline_poly = board
        .polygons
        .iter()
        .find(|p| p.layer == Layer::BoardOutlines);

    let (brd_min_x, brd_min_y, brd_max_x, brd_max_y) = match outline_poly {
        Some(poly) if !poly.vertices.is_empty() => {
            let mut min_x = f64::INFINITY;
            let mut max_x = f64::NEG_INFINITY;
            let mut min_y = f64::INFINITY;
            let mut max_y = f64::NEG_INFINITY;
            for v in &poly.vertices {
                min_x = min_x.min(v.position.x);
                max_x = max_x.max(v.position.x);
                min_y = min_y.min(v.position.y);
                max_y = max_y.max(v.position.y);
            }
            (min_x, min_y, max_x, max_y)
        }
        _ => (0.0, 0.0, 100.0, 100.0), // fallback
    };

    let brd_w = brd_max_x - brd_min_x;
    let brd_h = brd_max_y - brd_min_y;

    // -----------------------------------------------------------------------
    // 2. For each board device, compute footprint bounding box and pad count
    // -----------------------------------------------------------------------
    struct DeviceInfo {
        index: usize,
        component_uuid: uuid::Uuid,
        pad_count: usize,
        fp_width: f64,
        fp_height: f64,
        locked: bool,
    }

    let mut device_infos: Vec<DeviceInfo> = Vec::new();

    for (idx, bd) in board.devices.iter().enumerate() {
        // Load package to get footprint geometry
        let dev: Device = match project_io::read_library_element(
            project, "devices", &bd.lib_device,
        ) {
            Ok(d) => d,
            Err(_) => {
                device_infos.push(DeviceInfo {
                    index: idx,
                    component_uuid: bd.component,
                    pad_count: 0,
                    fp_width: 2.0,
                    fp_height: 2.0,
                    locked: bd.lock,
                });
                continue;
            }
        };

        let pkg: Package = match project_io::read_library_element(
            project, "packages", &dev.package,
        ) {
            Ok(p) => p,
            Err(_) => {
                device_infos.push(DeviceInfo {
                    index: idx,
                    component_uuid: bd.component,
                    pad_count: 0,
                    fp_width: 2.0,
                    fp_height: 2.0,
                    locked: bd.lock,
                });
                continue;
            }
        };

        let footprint = pkg
            .footprints
            .iter()
            .find(|f| f.uuid == bd.lib_footprint)
            .or_else(|| pkg.footprints.first());

        let (pad_count, fp_width, fp_height) = match footprint {
            Some(fp) => {
                let mut min_x = f64::INFINITY;
                let mut max_x = f64::NEG_INFINITY;
                let mut min_y = f64::INFINITY;
                let mut max_y = f64::NEG_INFINITY;

                // Consider pads
                for pad in &fp.pads {
                    let half_w = pad.width / 2.0;
                    let half_h = pad.height / 2.0;
                    min_x = min_x.min(pad.position.x - half_w);
                    max_x = max_x.max(pad.position.x + half_w);
                    min_y = min_y.min(pad.position.y - half_h);
                    max_y = max_y.max(pad.position.y + half_h);
                }

                // Consider courtyard/outline polygons for size
                for poly in &fp.polygons {
                    for v in &poly.vertices {
                        min_x = min_x.min(v.position.x);
                        max_x = max_x.max(v.position.x);
                        min_y = min_y.min(v.position.y);
                        max_y = max_y.max(v.position.y);
                    }
                }

                if min_x > max_x {
                    // No geometry found
                    (fp.pads.len(), 2.0, 2.0)
                } else {
                    (fp.pads.len(), max_x - min_x, max_y - min_y)
                }
            }
            None => (0, 2.0, 2.0),
        };

        device_infos.push(DeviceInfo {
            index: idx,
            component_uuid: bd.component,
            pad_count,
            fp_width,
            fp_height,
            locked: bd.lock,
        });
    }

    if device_infos.is_empty() {
        let result = serde_json::json!({
            "status": "ok",
            "placed": 0,
            "board_bounds": { "width": brd_w, "height": brd_h },
        });
        println!("{}", serde_json::to_string_pretty(&result)?);
        return Ok(());
    }

    // -----------------------------------------------------------------------
    // 3. Build connectivity graph: which devices share nets?
    // -----------------------------------------------------------------------
    // Map component_uuid → device_info index
    let comp_to_dev: HashMap<uuid::Uuid, usize> = device_infos
        .iter()
        .enumerate()
        .map(|(i, di)| (di.component_uuid, i))
        .collect();

    // For each net, collect the set of device indices connected to it
    let mut net_device_sets: Vec<Vec<usize>> = Vec::new();
    for net in &circuit.nets {
        let mut devs_on_net: Vec<usize> = Vec::new();
        for comp in &circuit.components {
            for sc in &comp.signal_connections {
                if sc.net == Some(net.uuid) {
                    if let Some(&di) = comp_to_dev.get(&comp.uuid) {
                        if !devs_on_net.contains(&di) {
                            devs_on_net.push(di);
                        }
                    }
                }
            }
        }
        if devs_on_net.len() >= 2 {
            net_device_sets.push(devs_on_net);
        }
    }

    // Build adjacency: connection_count[i][j] = number of shared nets
    let n = device_infos.len();
    let mut connection_count: Vec<Vec<u32>> = vec![vec![0u32; n]; n];
    for devs in &net_device_sets {
        for a in 0..devs.len() {
            for b in (a + 1)..devs.len() {
                let i = devs[a];
                let j = devs[b];
                connection_count[i][j] += 1;
                connection_count[j][i] += 1;
            }
        }
    }

    // -----------------------------------------------------------------------
    // 4. Classify: ICs (3+ pads) vs passives (1-2 pads)
    // -----------------------------------------------------------------------
    let mut ic_indices: Vec<usize> = Vec::new();
    let mut passive_indices: Vec<usize> = Vec::new();

    for (i, di) in device_infos.iter().enumerate() {
        if di.locked {
            continue; // skip locked devices
        }
        if di.pad_count >= 3 {
            ic_indices.push(i);
        } else {
            passive_indices.push(i);
        }
    }

    // -----------------------------------------------------------------------
    // 5. Place ICs on a grid in the center of the board
    // -----------------------------------------------------------------------
    let margin = 2.0_f64; // mm margin from board edge
    let spacing = 3.0_f64; // mm extra gap between devices

    // Compute available placement area
    let area_min_x = brd_min_x + margin;
    let area_min_y = brd_min_y + margin;
    let area_max_x = brd_max_x - margin;
    let area_max_y = brd_max_y - margin;
    let area_w = (area_max_x - area_min_x).max(1.0);
    let area_h = (area_max_y - area_min_y).max(1.0);

    // For ICs: arrange in a grid, centered in the board
    let ic_count = ic_indices.len();
    let mut positions: Vec<(f64, f64)> = vec![(0.0, 0.0); n];

    if ic_count > 0 {
        // Determine grid dimensions
        let cols = (ic_count as f64).sqrt().ceil() as usize;
        let rows = (ic_count + cols - 1) / cols;

        // Calculate cell sizes based on largest IC in each row/col
        // For simplicity, use uniform cells based on the max IC size
        let max_ic_w = ic_indices
            .iter()
            .map(|&i| device_infos[i].fp_width)
            .fold(0.0_f64, f64::max);
        let max_ic_h = ic_indices
            .iter()
            .map(|&i| device_infos[i].fp_height)
            .fold(0.0_f64, f64::max);

        let cell_w = max_ic_w + spacing;
        let cell_h = max_ic_h + spacing;

        let grid_w = cols as f64 * cell_w;
        let grid_h = rows as f64 * cell_h;

        // Center the grid in the board area
        let grid_origin_x = area_min_x + (area_w - grid_w).max(0.0) / 2.0;
        let grid_origin_y = area_min_y + (area_h - grid_h).max(0.0) / 2.0;

        for (idx, &di) in ic_indices.iter().enumerate() {
            let col = idx % cols;
            let row = idx / cols;
            let cx = grid_origin_x + (col as f64 + 0.5) * cell_w;
            let cy = grid_origin_y + (row as f64 + 0.5) * cell_h;
            positions[di] = (cx, cy);
        }
    }

    // -----------------------------------------------------------------------
    // 6. Place passives near the IC they're most connected to
    // -----------------------------------------------------------------------
    // For each passive, find the IC (or any device already placed) it shares
    // the most nets with, and place it nearby.
    // Track occupied rectangles to avoid overlap.
    struct Rect {
        cx: f64,
        cy: f64,
        half_w: f64,
        half_h: f64,
    }

    let mut occupied: Vec<Rect> = Vec::new();
    for &i in &ic_indices {
        occupied.push(Rect {
            cx: positions[i].0,
            cy: positions[i].1,
            half_w: device_infos[i].fp_width / 2.0 + spacing / 2.0,
            half_h: device_infos[i].fp_height / 2.0 + spacing / 2.0,
        });
    }

    let overlaps = |cx: f64, cy: f64, hw: f64, hh: f64, rects: &[Rect]| -> bool {
        for r in rects {
            if (cx - r.cx).abs() < hw + r.half_w
                && (cy - r.cy).abs() < hh + r.half_h
            {
                return true;
            }
        }
        false
    };

    for &pi in &passive_indices {
        let di = &device_infos[pi];
        let half_w = di.fp_width / 2.0 + spacing / 2.0;
        let half_h = di.fp_height / 2.0 + spacing / 2.0;

        // Find the most-connected placed device (preferring ICs)
        let mut best_target: Option<usize> = None;
        let mut best_count = 0u32;
        for &ici in &ic_indices {
            if connection_count[pi][ici] > best_count {
                best_count = connection_count[pi][ici];
                best_target = Some(ici);
            }
        }
        // If no IC connection, try any device
        if best_target.is_none() {
            for j in 0..n {
                if j != pi && connection_count[pi][j] > best_count {
                    best_count = connection_count[pi][j];
                    best_target = Some(j);
                }
            }
        }

        let (anchor_x, anchor_y) = match best_target {
            Some(t) => positions[t],
            None => (
                area_min_x + area_w / 2.0,
                area_min_y + area_h / 2.0,
            ),
        };

        // Try positions in a spiral around the anchor
        let mut placed = false;
        'search: for ring in 0..50 {
            let offset = (ring as f64) * (spacing + 1.0);
            // Try 8 directions per ring (+ cardinal offsets)
            let offsets: [(f64, f64); 8] = [
                (offset, 0.0),
                (-offset, 0.0),
                (0.0, offset),
                (0.0, -offset),
                (offset, offset),
                (offset, -offset),
                (-offset, offset),
                (-offset, -offset),
            ];
            for &(dx, dy) in &offsets {
                let cx = anchor_x + dx;
                let cy = anchor_y + dy;
                // Check bounds
                if cx - half_w < area_min_x || cx + half_w > area_max_x {
                    continue;
                }
                if cy - half_h < area_min_y || cy + half_h > area_max_y {
                    continue;
                }
                if !overlaps(cx, cy, half_w, half_h, &occupied) {
                    positions[pi] = (cx, cy);
                    occupied.push(Rect {
                        cx,
                        cy,
                        half_w,
                        half_h,
                    });
                    placed = true;
                    break 'search;
                }
            }
        }

        if !placed {
            // Fallback: just place it offset from anchor, clamped to bounds
            let cx = (anchor_x + spacing)
                .max(area_min_x + half_w)
                .min(area_max_x - half_w);
            let cy = (anchor_y + spacing)
                .max(area_min_y + half_h)
                .min(area_max_y - half_h);
            positions[pi] = (cx, cy);
            occupied.push(Rect {
                cx,
                cy,
                half_w,
                half_h,
            });
        }
    }

    // -----------------------------------------------------------------------
    // 7. Apply positions to board devices (skip locked ones)
    // -----------------------------------------------------------------------
    let mut placed_count = 0usize;
    for di in &device_infos {
        if di.locked {
            continue;
        }
        let (x, y) = positions[di.index];
        // Clamp to board bounds as a safety net
        let x = x
            .max(brd_min_x + margin)
            .min(brd_max_x - margin);
        let y = y
            .max(brd_min_y + margin)
            .min(brd_max_y - margin);
        board.devices[di.index].position = Position::new(
            (x * 100.0).round() / 100.0,
            (y * 100.0).round() / 100.0,
        );
        placed_count += 1;
    }

    // -----------------------------------------------------------------------
    // 8. Write board and emit JSON
    // -----------------------------------------------------------------------
    project_io::write_board(project, board_name, &board)?;

    let result = serde_json::json!({
        "status": "ok",
        "placed": placed_count,
        "board_bounds": {
            "width": brd_w,
            "height": brd_h,
        },
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
