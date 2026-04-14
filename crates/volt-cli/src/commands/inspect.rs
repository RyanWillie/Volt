//! `volt-eda inspect` — dump project summary as JSON.

use std::fs;
use std::path::Path;

use volt_core::project::*;

use super::project_io::{self, Result};

pub fn inspect_project(project_dir: &Path) -> Result<()> {
    project_io::ensure_project(project_dir)?;

    let metadata: ProjectMetadata = project_io::read_metadata(project_dir)?;
    let circuit: Circuit = project_io::read_circuit(project_dir)?;

    // Discover schematics
    let mut schematics = Vec::new();
    let sch_dir = project_dir.join("schematics");
    if sch_dir.exists() {
        for entry in fs::read_dir(&sch_dir)? {
            let entry = entry?;
            let path = entry.path();
            if path.extension().is_some_and(|e| e == "json") {
                let sch: Schematic = project_io::read_json(&path)?;
                schematics.push(sch.name);
            }
        }
    }
    schematics.sort();

    // Discover boards
    let mut boards = Vec::new();
    let brd_dir = project_dir.join("boards");
    if brd_dir.exists() {
        for entry in fs::read_dir(&brd_dir)? {
            let entry = entry?;
            let path = entry.path();
            if path.extension().is_some_and(|e| e == "json") {
                let brd: Board = project_io::read_json(&path)?;
                boards.push(brd.name);
            }
        }
    }
    boards.sort();

    // Count library elements
    let count_dir = |subdir: &str| -> usize {
        let dir = project_dir.join("library").join(subdir);
        if !dir.exists() {
            return 0;
        }
        fs::read_dir(&dir)
            .map(|entries| {
                entries
                    .filter_map(|e| e.ok())
                    .filter(|e| e.path().extension().is_some_and(|ext| ext == "json"))
                    .count()
            })
            .unwrap_or(0)
    };

    let result = serde_json::json!({
        "project": {
            "uuid": metadata.uuid.to_string(),
            "name": metadata.name,
            "author": metadata.author,
            "version": metadata.version,
            "created": metadata.created.to_rfc3339(),
        },
        "circuit": {
            "nets": circuit.nets.len(),
            "components": circuit.components.len(),
            "net_classes": circuit.net_classes.len(),
            "assembly_variants": circuit.assembly_variants.len(),
        },
        "schematics": schematics,
        "boards": boards,
        "library": {
            "symbols": count_dir("symbols"),
            "components": count_dir("components"),
            "packages": count_dir("packages"),
            "devices": count_dir("devices"),
        },
    });

    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}
