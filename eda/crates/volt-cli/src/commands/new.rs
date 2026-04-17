//! `volt-eda new` — scaffold a new project.

use std::fs;
use std::path::Path;

use volt_core::common::*;
use volt_core::project::*;

use super::project_io::{self, Result};

pub fn new_project(name: &str, output: Option<&Path>) -> Result<()> {
    let dir = output
        .map(|p| p.to_path_buf())
        .unwrap_or_else(|| Path::new(name).to_path_buf());

    if dir.exists() {
        return Err(format!("Directory already exists: {}", dir.display()).into());
    }

    // Create directory structure
    fs::create_dir_all(dir.join("schematics"))?;
    fs::create_dir_all(dir.join("boards"))?;
    fs::create_dir_all(dir.join("library/symbols"))?;
    fs::create_dir_all(dir.join("library/components"))?;
    fs::create_dir_all(dir.join("library/packages"))?;
    fs::create_dir_all(dir.join("library/devices"))?;

    // Project metadata
    let metadata = ProjectMetadata {
        uuid: new_uuid(),
        name: name.to_string(),
        author: String::new(),
        version: "v1".to_string(),
        schema_version: volt_core::project::CURRENT_SCHEMA_VERSION,
        created: chrono::Utc::now(),
        settings: ProjectSettings {
            locale_order: vec!["en_US".to_string()],
            norm_order: vec![],
            custom_bom_attributes: vec![],
            default_lock_component_assembly: false,
        },
    };
    write_json(&dir.join("volt.json"), &metadata)?;

    // Empty circuit
    let default_nc_uuid = new_uuid();
    let default_av_uuid = new_uuid();
    let circuit = Circuit {
        assembly_variants: vec![AssemblyVariant {
            uuid: default_av_uuid,
            name: "Std".to_string(),
            description: String::new(),
        }],
        net_classes: vec![NetClass {
            uuid: default_nc_uuid,
            name: "default".to_string(),
            default_trace_width: TraceWidthConfig::Inherit,
            default_via_drill_diameter: TraceWidthConfig::Inherit,
            min_copper_copper_clearance: 0.0,
            min_copper_width: 0.0,
            min_via_drill_diameter: 0.0,
            diff_pair_gap: None,
            diff_pair_max_length_delta: None,
        }],
        nets: vec![],
        components: vec![],
        differential_pairs: vec![],
    };
    write_json(&dir.join("circuit.json"), &circuit)?;

    // Default schematic
    let schematic = Schematic {
        uuid: new_uuid(),
        name: "Main".to_string(),
        grid: Grid {
            interval: 2.54,
            unit: GridUnit::Millimeters,
        },
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
    write_json(&dir.join("schematics/main.json"), &schematic)?;

    // Default board with 100x100mm outline
    let board = Board {
        uuid: new_uuid(),
        name: "default".to_string(),
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
        devices: vec![],
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
                Vertex {
                    position: Position::new(0.0, 0.0),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(100.0, 0.0),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(100.0, 100.0),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(0.0, 100.0),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(0.0, 0.0),
                    angle: Angle(0.0),
                },
            ],
        }],
        holes: vec![],
    };
    write_json(&dir.join("boards/default.json"), &board)?;

    // Output result as JSON (agent-friendly)
    let result = serde_json::json!({
        "status": "ok",
        "project": dir.display().to_string(),
        "files": [
            "volt.json",
            "circuit.json",
            "schematics/main.json",
            "boards/default.json",
        ]
    });
    println!("{}", serde_json::to_string_pretty(&result)?);

    Ok(())
}

fn write_json<T: serde::Serialize>(path: &Path, value: &T) -> Result<()> {
    project_io::write_json(path, value)
}
