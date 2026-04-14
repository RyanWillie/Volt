//! Shared project I/O: read/write JSON files in a Volt project.

use std::fs;
use std::path::Path;

use volt_core::project::{Board, Circuit, ProjectMetadata, Schematic};

pub type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

pub fn read_json<T: serde::de::DeserializeOwned>(path: &Path) -> Result<T> {
    let content = fs::read_to_string(path)
        .map_err(|e| format!("Failed to read {}: {e}", path.display()))?;
    let value = serde_json::from_str(&content)
        .map_err(|e| format!("Failed to parse {}: {e}", path.display()))?;
    Ok(value)
}

pub fn write_json<T: serde::Serialize>(path: &Path, value: &T) -> Result<()> {
    let json = serde_json::to_string_pretty(value)?;
    fs::write(path, json + "\n")?;
    Ok(())
}

pub fn read_metadata(project: &Path) -> Result<ProjectMetadata> {
    read_json(&project.join("volt.json"))
}

pub fn read_circuit(project: &Path) -> Result<Circuit> {
    read_json(&project.join("circuit.json"))
}

pub fn write_circuit(project: &Path, circuit: &Circuit) -> Result<()> {
    write_json(&project.join("circuit.json"), circuit)
}

pub fn read_schematic(project: &Path, name: &str) -> Result<Schematic> {
    read_json(&project.join(format!("schematics/{name}.json")))
}

pub fn write_schematic(project: &Path, name: &str, schematic: &Schematic) -> Result<()> {
    write_json(&project.join(format!("schematics/{name}.json")), schematic)
}

pub fn read_board(project: &Path, name: &str) -> Result<Board> {
    read_json(&project.join(format!("boards/{name}.json")))
}

pub fn write_board(project: &Path, name: &str, board: &Board) -> Result<()> {
    write_json(&project.join(format!("boards/{name}.json")), board)
}

/// Read a library element by type and UUID.
pub fn read_library_element<T: serde::de::DeserializeOwned>(
    project: &Path,
    kind: &str,
    uuid: &uuid::Uuid,
) -> Result<T> {
    read_json(&project.join(format!("library/{kind}/{uuid}.json")))
}

/// Write a library element by type and UUID.
pub fn write_library_element<T: serde::Serialize>(
    project: &Path,
    kind: &str,
    uuid: &uuid::Uuid,
    value: &T,
) -> Result<()> {
    let dir = project.join(format!("library/{kind}"));
    fs::create_dir_all(&dir)?;
    write_json(&dir.join(format!("{uuid}.json")), value)
}

/// Ensure the path points to a valid Volt project.
pub fn ensure_project(project: &Path) -> Result<()> {
    if !project.join("volt.json").exists() {
        return Err(format!(
            "Not a Volt project: {} (no volt.json found)",
            project.display()
        )
        .into());
    }
    Ok(())
}
