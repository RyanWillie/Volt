//! `volt-eda import` subcommands.

use std::collections::HashSet;
use std::fs;
use std::path::PathBuf;

use clap::Subcommand;
use volt_core::library::Component;
use volt_import::kicad_sym::{import_kicad_sym_dir, resolve_extends};

use super::project_io::{self, Result};

#[derive(Subcommand)]
pub enum ImportCommands {
    /// Import KiCad 8+ `.kicad_sym` files into the project library.
    KicadSymbols {
        /// Path to project directory.
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Directory containing `.kicad_sym` files or `*.kicad_symdir` trees.
        #[arg(long)]
        dir: PathBuf,
        /// Optional limit for development/testing.
        #[arg(long)]
        limit: Option<usize>,
    },
}

pub fn import_command(cmd: ImportCommands) -> Result<()> {
    match cmd {
        ImportCommands::KicadSymbols { project, dir, limit } => {
            project_io::ensure_project(&project)?;

            let raw_results = import_kicad_sym_dir(&dir);
            let mut parsed = Vec::new();
            let mut errors = Vec::new();

            for result in raw_results {
                match result {
                    Ok(import) => parsed.push(import),
                    Err(err) => errors.push(err),
                }
            }

            let resolved = resolve_extends(parsed)
                .map_err(|e| format!("Failed to resolve KiCad inheritance: {e}"))?;

            let existing_names = existing_component_names(&project)?;
            let mut imported = 0usize;
            let mut skipped = 0usize;
            let mut written = Vec::new();

            for import in resolved.into_iter().take(limit.unwrap_or(usize::MAX)) {
                if existing_names.contains(&import.component.meta.name) {
                    skipped += 1;
                    continue;
                }

                project_io::write_library_element(
                    &project,
                    "symbols",
                    &import.symbol.meta.uuid,
                    &import.symbol,
                )?;
                project_io::write_library_element(
                    &project,
                    "components",
                    &import.component.meta.uuid,
                    &import.component,
                )?;

                imported += 1;
                written.push(serde_json::json!({
                    "name": import.component.meta.name,
                    "component_uuid": import.component.meta.uuid.to_string(),
                    "symbol_uuid": import.symbol.meta.uuid.to_string(),
                    "pins": import.symbol.pins.len(),
                    "extends": import.extends,
                }));
            }

            let result = serde_json::json!({
                "status": "ok",
                "import": {
                    "kind": "kicad_symbols",
                    "dir": dir.display().to_string(),
                    "imported": imported,
                    "skipped_existing": skipped,
                    "parse_errors": errors.len(),
                    "items": written,
                },
                "errors": errors,
            });
            println!("{}", serde_json::to_string_pretty(&result)?);
            Ok(())
        }
    }
}

fn existing_component_names(project: &std::path::Path) -> Result<HashSet<String>> {
    let mut names = HashSet::new();
    let dir = project.join("library/components");
    if !dir.exists() {
        return Ok(names);
    }
    for entry in fs::read_dir(dir)? {
        let path = entry?.path();
        if path.extension().is_some_and(|ext| ext == "json") {
            if let Ok(component) = project_io::read_json::<Component>(&path) {
                names.insert(component.meta.name);
            }
        }
    }
    Ok(names)
}
