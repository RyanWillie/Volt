//! `volt-eda board` subcommands.

use std::path::PathBuf;

use clap::Subcommand;

use super::project_io::Result;

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
        BoardCommands::Init { .. } => {
            stub("board init")
        }
        BoardCommands::Outline { .. } => {
            stub("board outline")
        }
        BoardCommands::Place { .. } => {
            stub("board place")
        }
        BoardCommands::Move { .. } => {
            stub("board move")
        }
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
