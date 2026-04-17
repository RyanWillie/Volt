//! `volt-eda import` subcommands.

use std::collections::HashSet;
use std::fs;
use std::path::PathBuf;

use clap::Subcommand;
use volt_core::library::{Component, Package};
use volt_import::kicad_mod::parse_kicad_mod_file;
use volt_import::kicad_sym::{import_kicad_sym_dir, resolve_extends};

use super::component::create_auto_mapped_device;
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
    /// Import a KiCad `.kicad_mod` footprint file into the project library.
    Footprint {
        /// Path to the `.kicad_mod` file.
        #[arg(long)]
        file: PathBuf,
        /// Path to project directory.
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Optional library component UUID or name to auto-create a matching device.
        #[arg(long)]
        component: Option<String>,
        /// Optional device name override when `--component` is used.
        #[arg(long)]
        device_name: Option<String>,
    },
}

pub fn import_command(cmd: ImportCommands) -> Result<()> {
    match cmd {
        ImportCommands::Footprint {
            file,
            project,
            component,
            device_name,
        } => import_footprint(
            &file,
            &project,
            component.as_deref(),
            device_name.as_deref(),
        ),
        ImportCommands::KicadSymbols {
            project,
            dir,
            limit,
        } => {
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

fn existing_package_names(project: &std::path::Path) -> Result<HashSet<String>> {
    let mut names = HashSet::new();
    let dir = project.join("library/packages");
    if !dir.exists() {
        return Ok(names);
    }
    for entry in fs::read_dir(dir)? {
        let path = entry?.path();
        if path.extension().is_some_and(|ext| ext == "json") {
            if let Ok(package) = project_io::read_json::<Package>(&path) {
                names.insert(package.meta.name);
            }
        }
    }
    Ok(names)
}

fn import_footprint(
    file: &std::path::Path,
    project: &std::path::Path,
    component_selector: Option<&str>,
    device_name: Option<&str>,
) -> Result<()> {
    project_io::ensure_project(project)?;

    let package = parse_kicad_mod_file(file).map_err(|e| format!("{}: {e}", file.display()))?;

    let existing = existing_package_names(project)?;
    if existing.contains(&package.meta.name) {
        let device = if let Some(component) = component_selector {
            Some(create_auto_mapped_device(
                project,
                component,
                &package.meta.name,
                device_name,
            )?)
        } else {
            None
        };
        let result = serde_json::json!({
            "status": "skipped",
            "reason": "package already exists",
            "name": package.meta.name,
            "device": device.map(|created| serde_json::json!({
                "action": if created.created { "created" } else { "existing" },
                "uuid": created.device.meta.uuid.to_string(),
                "name": created.device.meta.name,
                "pad_mapping_count": created.device.pad_mappings.len(),
            })),
        });
        println!("{}", serde_json::to_string_pretty(&result)?);
        return Ok(());
    }

    project_io::write_library_element(project, "packages", &package.meta.uuid, &package)?;
    let device = if let Some(component) = component_selector {
        Some(create_auto_mapped_device(
            project,
            component,
            &package.meta.name,
            device_name,
        )?)
    } else {
        None
    };

    let fp = package.footprints.first();
    let result = serde_json::json!({
        "status": "ok",
        "import": {
            "kind": "kicad_footprint",
            "file": file.display().to_string(),
            "name": package.meta.name,
            "package_uuid": package.meta.uuid.to_string(),
            "pads": package.pads.len(),
            "footprint_pads": fp.map(|f| f.pads.len()).unwrap_or(0),
            "polygons": fp.map(|f| f.polygons.len()).unwrap_or(0),
            "texts": fp.map(|f| f.texts.len()).unwrap_or(0),
            "assembly_type": format!("{:?}", package.assembly_type),
        },
        "device": device.map(|created| serde_json::json!({
            "action": if created.created { "created" } else { "existing" },
            "uuid": created.device.meta.uuid.to_string(),
            "name": created.device.meta.name,
            "pad_mapping_count": created.device.pad_mappings.len(),
        })),
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use tempfile::TempDir;
    use uuid::Uuid;
    use volt_core::common::{AssemblyType, SignalRole, new_uuid};
    use volt_core::library::{Component, ComponentVariant, Device, LibraryMeta, Package, Signal};

    use super::*;
    use crate::commands::new_project;

    fn create_temp_project() -> (TempDir, PathBuf) {
        let dir = tempfile::tempdir().unwrap();
        let project = dir.path().join("proj");
        new_project("proj", Some(&project)).unwrap();
        (dir, project)
    }

    fn seed_component(project: &std::path::Path) -> Uuid {
        let component_uuid = new_uuid();
        let variant_uuid = new_uuid();
        let component = Component {
            meta: LibraryMeta {
                uuid: component_uuid,
                name: "Imported Resistor".into(),
                description: String::new(),
                keywords: String::new(),
                author: "test".into(),
                version: "0.1".into(),
                created: chrono::Utc::now(),
                deprecated: false,
                category: None,
            },
            prefix: "R".into(),
            default_value: String::new(),
            schematic_only: false,
            attributes: vec![],
            signals: vec![
                Signal {
                    uuid: new_uuid(),
                    name: "1".into(),
                    role: SignalRole::Passive,
                    required: true,
                    negated: false,
                    clock: false,
                    forced_net: String::new(),
                },
                Signal {
                    uuid: new_uuid(),
                    name: "2".into(),
                    role: SignalRole::Passive,
                    required: true,
                    negated: false,
                    clock: false,
                    forced_net: String::new(),
                },
            ],
            variants: vec![ComponentVariant {
                uuid: variant_uuid,
                norm: String::new(),
                name: "default".into(),
                description: String::new(),
                gates: vec![],
            }],
        };
        project_io::write_library_element(project, "components", &component_uuid, &component)
            .unwrap();
        component_uuid
    }

    #[test]
    fn footprint_import_can_create_auto_mapped_device() {
        let (_tmp, project) = create_temp_project();
        let component_uuid = seed_component(&project);
        let footprint = project.join("R_0805_2012Metric.kicad_mod");
        fs::write(
            &footprint,
            r#"(footprint "R_0805_2012Metric"
  (version 20240108)
  (generator "pcbnew")
  (layer "F.Cu")
  (descr "Resistor SMD 0805 (2012 Metric)")
  (tags "resistor 0805")
  (attr smd)
  (pad "1" smd roundrect (at -0.9125 0) (size 1.025 1.4) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.243902))
  (pad "2" smd roundrect (at 0.9125 0) (size 1.025 1.4) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.243902))
)"#,
        )
        .unwrap();

        import_command(ImportCommands::Footprint {
            file: footprint,
            project: project.clone(),
            component: Some(component_uuid.to_string()),
            device_name: None,
        })
        .unwrap();

        let packages_dir = project.join("library/packages");
        let devices_dir = project.join("library/devices");
        let packages = fs::read_dir(packages_dir).unwrap().count();
        let devices = fs::read_dir(devices_dir).unwrap().count();
        assert_eq!(packages, 1);
        assert_eq!(devices, 1);

        let imported_device: Device = project_io::read_json(
            &fs::read_dir(project.join("library/devices"))
                .unwrap()
                .next()
                .unwrap()
                .unwrap()
                .path(),
        )
        .unwrap();
        let imported_package: Package = project_io::read_json(
            &fs::read_dir(project.join("library/packages"))
                .unwrap()
                .next()
                .unwrap()
                .unwrap()
                .path(),
        )
        .unwrap();
        assert_eq!(imported_device.component, component_uuid);
        assert_eq!(imported_device.package, imported_package.meta.uuid);
        assert_eq!(imported_device.pad_mappings.len(), 2);
        assert_eq!(imported_package.assembly_type, AssemblyType::Smt);
    }
}
