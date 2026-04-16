//! `volt-eda export` subcommands — BOM, pick & place, Gerber.

use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

use clap::{Subcommand, ValueEnum};

use volt_core::library::{Component, Device, Package};
use volt_export::bom::{self, BomLibrary};
use volt_export::pick_place;

use super::project_io::{self, Result};

// ---------------------------------------------------------------------------
// CLI types
// ---------------------------------------------------------------------------

#[derive(Subcommand)]
pub enum ExportCommands {
    /// Export Bill of Materials
    Bom {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Output format
        #[arg(long, default_value = "csv")]
        format: BomFormat,
        /// Output file (stdout if omitted)
        #[arg(long)]
        output: Option<PathBuf>,
    },
    /// Export pick & place (centroid) file
    PickPlace {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Board name (without .json)
        #[arg(long, default_value = "default")]
        board: String,
        /// Output file (stdout if omitted)
        #[arg(long)]
        output: Option<PathBuf>,
    },
    /// Export Gerber fabrication files
    Gerber {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Board name (without .json)
        #[arg(long, default_value = "default")]
        board: String,
        /// Output directory
        #[arg(long)]
        output_dir: PathBuf,
    },
}

#[derive(Clone, ValueEnum)]
pub enum BomFormat {
    Csv,
    Json,
}

// ---------------------------------------------------------------------------
// Command dispatch
// ---------------------------------------------------------------------------

pub fn export_command(cmd: ExportCommands) -> Result<()> {
    match cmd {
        ExportCommands::Bom {
            project,
            format,
            output,
        } => export_bom(&project, format, output.as_deref()),
        ExportCommands::PickPlace {
            project,
            board,
            output,
        } => export_pick_place(&project, &board, output.as_deref()),
        ExportCommands::Gerber {
            project,
            board,
            output_dir,
        } => export_gerber(&project, &board, &output_dir),
    }
}

// ---------------------------------------------------------------------------
// BOM export
// ---------------------------------------------------------------------------

fn export_bom(
    project: &std::path::Path,
    format: BomFormat,
    output: Option<&std::path::Path>,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let library = load_project_library(project, &circuit)?;

    let bom_result = bom::generate_bom(&circuit, &library);

    let content = match format {
        BomFormat::Csv => bom::export_bom_csv(&bom_result),
        BomFormat::Json => bom::export_bom_json(&bom_result),
    };

    match output {
        Some(path) => {
            if let Some(parent) = path.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::write(path, &content)?;
            eprintln!(
                "BOM exported: {} unique parts, {} total components → {}",
                bom_result.unique_parts,
                bom_result.total_components,
                path.display()
            );
        }
        None => {
            print!("{content}");
        }
    }

    Ok(())
}

// ---------------------------------------------------------------------------
// Pick & place export
// ---------------------------------------------------------------------------

fn export_pick_place(
    project: &std::path::Path,
    board_name: &str,
    output: Option<&std::path::Path>,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let board = project_io::read_board(project, board_name)?;
    let library = load_project_library(project, &circuit)?;

    let entries = pick_place::generate_pick_place(&board, &circuit, &library);
    let content = pick_place::export_pick_place_csv(&entries);

    match output {
        Some(path) => {
            if let Some(parent) = path.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::write(path, &content)?;
            eprintln!(
                "Pick & place exported: {} placements → {}",
                entries.len(),
                path.display()
            );
        }
        None => {
            print!("{content}");
        }
    }

    Ok(())
}

// ---------------------------------------------------------------------------
// Gerber export (stub / wire-up)
// ---------------------------------------------------------------------------

fn export_gerber(
    _project: &std::path::Path,
    _board_name: &str,
    _output_dir: &std::path::Path,
) -> Result<()> {
    // The gerber module will be wired up when volt_export::gerber is implemented.
    eprintln!("Gerber export is not yet implemented.");
    eprintln!("The gerber module will be wired up when available.");
    let result = serde_json::json!({
        "status": "not_implemented",
        "message": "Gerber export is not yet available"
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ---------------------------------------------------------------------------
// Library loader
// ---------------------------------------------------------------------------

/// In-memory library loaded from a project's `library/` directory.
struct ProjectLibrary {
    components: HashMap<uuid::Uuid, Component>,
    devices: HashMap<uuid::Uuid, Device>,
    packages: HashMap<uuid::Uuid, Package>,
}

impl BomLibrary for ProjectLibrary {
    fn get_component(&self, uuid: &uuid::Uuid) -> Option<&Component> {
        self.components.get(uuid)
    }
    fn get_device(&self, uuid: &uuid::Uuid) -> Option<&Device> {
        self.devices.get(uuid)
    }
    fn get_package(&self, uuid: &uuid::Uuid) -> Option<&Package> {
        self.packages.get(uuid)
    }
}

/// Load all library elements referenced by the circuit.
fn load_project_library(
    project: &std::path::Path,
    circuit: &volt_core::project::Circuit,
) -> Result<ProjectLibrary> {
    let mut components: HashMap<uuid::Uuid, Component> = HashMap::new();
    let mut devices: HashMap<uuid::Uuid, Device> = HashMap::new();
    let mut packages: HashMap<uuid::Uuid, Package> = HashMap::new();

    for comp in &circuit.components {
        // Load component
        if !components.contains_key(&comp.lib_component) {
            if let Ok(c) = project_io::read_library_element::<Component>(
                project,
                "components",
                &comp.lib_component,
            ) {
                components.insert(comp.lib_component, c);
            }
        }

        // Load devices from assignments
        for da in &comp.device_assignments {
            if !devices.contains_key(&da.device) {
                if let Ok(d) = project_io::read_library_element::<Device>(
                    project,
                    "devices",
                    &da.device,
                ) {
                    // Also load the package
                    if !packages.contains_key(&d.package) {
                        if let Ok(p) = project_io::read_library_element::<Package>(
                            project,
                            "packages",
                            &d.package,
                        ) {
                            packages.insert(d.package, p);
                        }
                    }
                    devices.insert(da.device, d);
                }
            }
        }
    }

    Ok(ProjectLibrary {
        components,
        devices,
        packages,
    })
}
