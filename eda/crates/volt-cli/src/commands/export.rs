//! `volt-eda export` subcommands — BOM, pick & place, Gerber.

use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

use clap::{Subcommand, ValueEnum};

use volt_core::library::{Component, Device, Package};
use volt_export::bom::{self, BomLibrary};
use volt_export::excellon;
use volt_export::gerber::{self, BoardLibrary};
use volt_export::pick_place;
use volt_refill::refill_board;

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
    /// Export Excellon drill files (PTH and NPTH)
    Drills {
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
    /// Export IPC-D-356 bare-board test netlist
    Netlist {
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
    /// Export interactive HTML BOM
    Ibom {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Board name (without .json)
        #[arg(long, default_value = "default")]
        board: String,
        /// Output HTML file
        #[arg(long)]
        output: PathBuf,
    },
    /// Export Specctra DSN file for external autorouting
    Dsn {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Board name (without .json)
        #[arg(long, default_value = "default")]
        board: String,
        /// Output .dsn file
        #[arg(long)]
        output: PathBuf,
    },
    /// Export STEP 3D file of the board
    Step {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Board name (without .json)
        #[arg(long, default_value = "default")]
        board: String,
        /// Output .step file
        #[arg(long)]
        output: PathBuf,
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
        ExportCommands::Drills {
            project,
            board,
            output_dir,
        } => export_drills(&project, &board, &output_dir),
        ExportCommands::Netlist {
            project,
            board,
            output,
        } => export_netlist(&project, &board, output.as_deref()),
        ExportCommands::Ibom {
            project,
            board,
            output,
        } => export_ibom(&project, &board, &output),
        ExportCommands::Dsn {
            project,
            board,
            output,
        } => export_dsn(&project, &board, &output),
        ExportCommands::Step {
            project,
            board,
            output,
        } => export_step_file(&project, &board, &output),
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
// Gerber export
// ---------------------------------------------------------------------------

fn export_gerber(
    project: &std::path::Path,
    board_name: &str,
    output_dir: &std::path::Path,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut board = project_io::read_board(project, board_name)?;
    let library = load_project_library(project, &circuit)?;
    refill_board(&mut board, &circuit, &library);

    fs::create_dir_all(output_dir)?;
    let summary = gerber::export_all(&board, &circuit, &library, output_dir)
        .map_err(|e| format!("gerber export failed: {e}"))?;

    let files_json: Vec<_> = summary
        .files
        .iter()
        .map(|f| {
            serde_json::json!({
                "layer": f.layer_name,
                "path": f.path.display().to_string(),
            })
        })
        .collect();
    let result = serde_json::json!({
        "status": "ok",
        "board": board.name,
        "file_count": summary.files.len(),
        "files": files_json,
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ---------------------------------------------------------------------------
// Excellon drill export
// ---------------------------------------------------------------------------

fn export_drills(
    project: &std::path::Path,
    board_name: &str,
    output_dir: &std::path::Path,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let board = project_io::read_board(project, board_name)?;
    let library = load_project_library(project, &circuit)?;

    fs::create_dir_all(output_dir)?;
    let settings = board.fabrication_output_settings.clone();
    let summary = excellon::export_all_drills(&board, &library, output_dir, &settings)
        .map_err(|e| format!("drill export failed: {e}"))?;

    let files_json: Vec<_> = summary
        .files
        .iter()
        .map(|f| {
            let kind = match f.kind {
                excellon::DrillFileKind::Pth => "pth",
                excellon::DrillFileKind::Npth => "npth",
                excellon::DrillFileKind::Merged => "merged",
            };
            serde_json::json!({
                "kind": kind,
                "path": f.path.display().to_string(),
                "hole_count": f.hole_count,
            })
        })
        .collect();
    let result = serde_json::json!({
        "status": "ok",
        "board": board.name,
        "pth_hole_count": summary.pth_hole_count,
        "npth_hole_count": summary.npth_hole_count,
        "file_count": summary.files.len(),
        "files": files_json,
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ---------------------------------------------------------------------------
// IPC-D-356 netlist export
// ---------------------------------------------------------------------------

fn export_netlist(
    project: &std::path::Path,
    board_name: &str,
    output: Option<&std::path::Path>,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let board = project_io::read_board(project, board_name)?;
    let library = load_project_library(project, &circuit)?;

    let content = volt_export::ipc_d356::export_ipc_d356(&board, &circuit, &library);

    if let Some(path) = output {
        fs::write(path, &content)?;
        let result = serde_json::json!({
            "status": "ok",
            "output": path.display().to_string(),
        });
        println!("{}", serde_json::to_string_pretty(&result)?);
    } else {
        print!("{}", content);
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// Interactive HTML BOM export
// ---------------------------------------------------------------------------

fn export_ibom(
    project: &std::path::Path,
    board_name: &str,
    output: &std::path::Path,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let board = project_io::read_board(project, board_name)?;
    let library = load_project_library(project, &circuit)?;

    let html = volt_export::ibom::export_interactive_html_bom(&board, &circuit, &library);
    fs::write(output, &html)?;

    let result = serde_json::json!({
        "status": "ok",
        "output": output.display().to_string(),
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ---------------------------------------------------------------------------
// Specctra DSN export
// ---------------------------------------------------------------------------

fn export_dsn(project: &std::path::Path, board_name: &str, output: &std::path::Path) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let board = project_io::read_board(project, board_name)?;
    let library = load_project_library(project, &circuit)?;

    let content = volt_export::specctra::export_dsn(&board, &circuit, &library);
    fs::write(output, &content)?;

    let result = serde_json::json!({
        "status": "ok",
        "output": output.display().to_string(),
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ---------------------------------------------------------------------------
// STEP 3D export
// ---------------------------------------------------------------------------

fn export_step_file(
    project: &std::path::Path,
    board_name: &str,
    output: &std::path::Path,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let board = project_io::read_board(project, board_name)?;

    let content = volt_export::step::export_step(&board);
    fs::write(output, &content)?;

    let result = serde_json::json!({
        "status": "ok",
        "output": output.display().to_string(),
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

impl BoardLibrary for ProjectLibrary {
    fn get_device(&self, uuid: &uuid::Uuid) -> Option<&Device> {
        self.devices.get(uuid)
    }
    fn get_package(&self, uuid: &uuid::Uuid) -> Option<&Package> {
        self.packages.get(uuid)
    }
}

impl volt_refill::RefillLibrary for ProjectLibrary {
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
                if let Ok(d) =
                    project_io::read_library_element::<Device>(project, "devices", &da.device)
                {
                    // Also load the package
                    if !packages.contains_key(&d.package) {
                        if let Ok(p) = project_io::read_library_element::<Package>(
                            project, "packages", &d.package,
                        ) {
                            packages.insert(d.package, p);
                        }
                    }
                    devices.insert(da.device, d);
                }
            }
        }
    }

    // Also scan the library/devices directory for any devices not referenced
    // by assignments (e.g. auto-discovered devices from board init)
    let devices_dir = project.join("library/devices");
    if devices_dir.exists() {
        if let Ok(entries) = fs::read_dir(&devices_dir) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.extension().and_then(|e| e.to_str()) == Some("json") {
                    if let Ok(d) = project_io::read_json::<Device>(&path) {
                        if !devices.contains_key(&d.meta.uuid) {
                            if !packages.contains_key(&d.package) {
                                if let Ok(p) = project_io::read_library_element::<Package>(
                                    project, "packages", &d.package,
                                ) {
                                    packages.insert(d.package, p);
                                }
                            }
                            devices.insert(d.meta.uuid, d);
                        }
                    }
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

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use tempfile::TempDir;
    use uuid::Uuid;
    use volt_core::common::*;
    use volt_core::project::*;

    use super::*;
    use crate::commands::new_project;

    fn create_temp_project() -> (TempDir, PathBuf) {
        let dir = tempfile::tempdir().unwrap();
        let project = dir.path().join("proj");
        new_project("proj", Some(&project)).unwrap();
        (dir, project)
    }

    #[test]
    fn gerber_export_refills_planes_before_writing() {
        let (_tmp, project) = create_temp_project();

        let mut circuit = project_io::read_circuit(&project).unwrap();
        let net_class = circuit.net_classes[0].uuid;
        let plane_net = Uuid::new_v4();
        let trace_net = Uuid::new_v4();
        circuit.nets = vec![
            Net {
                uuid: plane_net,
                name: "GND".into(),
                auto_name: false,
                net_class,
                scope: NetScope::Global,
                owner_sheet: None,
                is_power: true,
            },
            Net {
                uuid: trace_net,
                name: "SIG".into(),
                auto_name: false,
                net_class,
                scope: NetScope::Global,
                owner_sheet: None,
                is_power: false,
            },
        ];
        project_io::write_circuit(&project, &circuit).unwrap();

        let mut board = project_io::read_board(&project, "default").unwrap();
        board.planes.push(Plane {
            uuid: Uuid::new_v4(),
            layer: Layer::TopCopper,
            net: plane_net,
            priority: 0,
            min_width: 0.2,
            min_copper_clearance: 0.2,
            min_board_clearance: 0.0,
            min_npth_clearance: 0.2,
            connect_style: ConnectStyle::Solid,
            thermal_gap: 0.3,
            thermal_spoke: 0.3,
            keep_islands: true,
            lock: false,
            vertices: vec![
                Vertex {
                    position: Position::new(10.0, 10.0),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(90.0, 10.0),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(90.0, 90.0),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(10.0, 90.0),
                    angle: Angle(0.0),
                },
            ],
            fragments: vec![],
        });
        let j1 = Junction {
            uuid: Uuid::new_v4(),
            position: Position::new(50.0, 10.0),
        };
        let j2 = Junction {
            uuid: Uuid::new_v4(),
            position: Position::new(50.0, 90.0),
        };
        board.net_segments.push(BoardNetSegment {
            uuid: Uuid::new_v4(),
            net: Some(trace_net),
            traces: vec![Trace {
                uuid: Uuid::new_v4(),
                layer: Layer::TopCopper,
                width: 20.0,
                from: TraceEndpoint::Junction { junction: j1.uuid },
                to: TraceEndpoint::Junction { junction: j2.uuid },
            }],
            vias: vec![],
            junctions: vec![j1, j2],
            pads: vec![],
        });
        project_io::write_board(&project, "default", &board).unwrap();

        let output_dir = project.join("out");
        export_gerber(&project, "default", &output_dir).unwrap();

        let top_copper = output_dir.join("default_COPPER-TOP.gbr");
        let content = fs::read_to_string(top_copper).unwrap();
        assert!(
            content.matches("G36*").count() >= 2,
            "Expected split refill fragments in top copper output, got:\n{}",
            content
        );
    }
}
