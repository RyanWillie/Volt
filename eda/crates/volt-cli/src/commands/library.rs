//! `volt-eda library` subcommands.

use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use clap::Subcommand;
use uuid::Uuid;
use volt_core::library::{Component, Device, Package, Symbol};

use super::project_io::{self, Result};

#[derive(Subcommand)]
pub enum LibraryCommands {
    /// Search imported library components by name, description, keywords, or prefix.
    Search {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long)]
        query: String,
        #[arg(long, default_value_t = 20)]
        limit: usize,
    },
    /// Show detailed info about a library component.
    Info {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Component UUID.
        #[arg(long)]
        component: Option<Uuid>,
        /// Component name fallback if UUID not provided.
        #[arg(long)]
        name: Option<String>,
    },
}

pub fn library_command(cmd: LibraryCommands) -> Result<()> {
    match cmd {
        LibraryCommands::Search {
            project,
            query,
            limit,
        } => search_library(&project, &query, limit),
        LibraryCommands::Info {
            project,
            component,
            name,
        } => library_info(&project, component, name.as_deref()),
    }
}

fn search_library(project: &Path, query: &str, limit: usize) -> Result<()> {
    project_io::ensure_project(project)?;
    let q = query.to_lowercase();
    let mut hits = Vec::new();
    let devices = read_all_devices(project)?;
    let packages = read_all_packages(project)?;

    for component in read_all_components(project)? {
        let haystack = format!(
            "{}\n{}\n{}\n{}\n{}",
            component.meta.name,
            component.meta.description,
            component.meta.keywords,
            component.prefix,
            component.default_value,
        )
        .to_lowercase();

        let score = score_match(&haystack, &q, &component);
        if score == 0 {
            continue;
        }

        let variant = component.variants.first();
        let matching_devices = matching_devices_json(&component, &devices, &packages);
        hits.push((
            score,
            serde_json::json!({
                "component_uuid": component.meta.uuid.to_string(),
                "variant_uuid": variant.map(|v| v.uuid.to_string()),
                "name": component.meta.name,
                "description": component.meta.description,
                "keywords": component.meta.keywords,
                "prefix": component.prefix,
                "default_value": component.default_value,
                "signals": component.signals.iter().map(|s| s.name.clone()).collect::<Vec<_>>(),
                "signal_count": component.signals.len(),
                "device_count": matching_devices.len(),
                "devices": matching_devices,
            }),
        ));
    }

    hits.sort_by(|a, b| {
        b.0.cmp(&a.0)
            .then_with(|| a.1["name"].as_str().cmp(&b.1["name"].as_str()))
    });
    let results: Vec<_> = hits.into_iter().take(limit).map(|(_, item)| item).collect();

    let out = serde_json::json!({
        "status": "ok",
        "query": query,
        "results": results,
        "count": results.len(),
    });
    println!("{}", serde_json::to_string_pretty(&out)?);
    Ok(())
}

fn library_info(
    project: &Path,
    component_uuid: Option<Uuid>,
    component_name: Option<&str>,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let component = if let Some(uuid) = component_uuid {
        project_io::read_library_element::<Component>(project, "components", &uuid)?
    } else if let Some(name) = component_name {
        read_all_components(project)?
            .into_iter()
            .find(|c| c.meta.name == name)
            .ok_or_else(|| format!("Component '{name}' not found"))?
    } else {
        return Err("Must provide --component <uuid> or --name <component-name>".into());
    };

    let symbols = component
        .variants
        .iter()
        .flat_map(|variant| variant.gates.iter())
        .filter_map(|gate| {
            project_io::read_library_element::<Symbol>(project, "symbols", &gate.symbol).ok()
        })
        .collect::<Vec<_>>();
    let devices = read_all_devices(project)?;
    let packages = read_all_packages(project)?;

    let out = serde_json::json!({
        "status": "ok",
        "component": {
            "uuid": component.meta.uuid.to_string(),
            "name": component.meta.name,
            "description": component.meta.description,
            "keywords": component.meta.keywords,
            "prefix": component.prefix,
            "default_value": component.default_value,
            "signals": component.signals.iter().map(|s| serde_json::json!({
                "uuid": s.uuid.to_string(),
                "name": s.name,
                "role": s.role,
                "required": s.required,
            })).collect::<Vec<_>>(),
            "variants": component.variants.iter().map(|v| serde_json::json!({
                "uuid": v.uuid.to_string(),
                "name": v.name,
                "norm": v.norm,
                "gates": v.gates.iter().map(|g| serde_json::json!({
                    "uuid": g.uuid.to_string(),
                    "symbol": g.symbol.to_string(),
                    "pin_mappings": g.pin_mappings.iter().map(|m| serde_json::json!({
                        "pin": m.pin.to_string(),
                        "signal": m.signal.to_string(),
                    })).collect::<Vec<_>>(),
                })).collect::<Vec<_>>(),
            })).collect::<Vec<_>>(),
            "symbols": symbols.iter().map(|s| serde_json::json!({
                "uuid": s.meta.uuid.to_string(),
                "name": s.meta.name,
                "pin_count": s.pins.len(),
                "pins": s.pins.iter().map(|p| serde_json::json!({
                    "uuid": p.uuid.to_string(),
                    "name": p.name,
                    "pin_name": p.pin_name,
                    "position": {"x": p.position.x, "y": p.position.y},
                    "rotation": p.rotation.0,
                })).collect::<Vec<_>>(),
            })).collect::<Vec<_>>(),
            "devices": matching_devices_json(&component, &devices, &packages),
        }
    });
    println!("{}", serde_json::to_string_pretty(&out)?);
    Ok(())
}

fn matching_devices_json(
    component: &Component,
    devices: &[Device],
    packages: &HashMap<Uuid, Package>,
) -> Vec<serde_json::Value> {
    devices
        .iter()
        .filter(|device| device.component == component.meta.uuid)
        .map(|device| {
            let package_name = packages
                .get(&device.package)
                .map(|package| package.meta.name.clone());
            serde_json::json!({
                "device_uuid": device.meta.uuid.to_string(),
                "device_name": device.meta.name,
                "package_uuid": device.package.to_string(),
                "package_name": package_name,
                "pad_mapping_count": device.pad_mappings.len(),
                "part_count": device.parts.len(),
            })
        })
        .collect()
}

fn read_all_components(project: &Path) -> Result<Vec<Component>> {
    let mut components = Vec::new();
    let dir = project.join("library/components");
    if !dir.exists() {
        return Ok(components);
    }
    for entry in fs::read_dir(dir)? {
        let path = entry?.path();
        if path.extension().is_some_and(|ext| ext == "json") {
            components.push(project_io::read_json::<Component>(&path)?);
        }
    }
    Ok(components)
}

fn read_all_devices(project: &Path) -> Result<Vec<Device>> {
    let mut devices = Vec::new();
    let dir = project.join("library/devices");
    if !dir.exists() {
        return Ok(devices);
    }
    for entry in fs::read_dir(dir)? {
        let path = entry?.path();
        if path.extension().is_some_and(|ext| ext == "json") {
            devices.push(project_io::read_json::<Device>(&path)?);
        }
    }
    Ok(devices)
}

fn read_all_packages(project: &Path) -> Result<HashMap<Uuid, Package>> {
    let mut packages = HashMap::new();
    let dir = project.join("library/packages");
    if !dir.exists() {
        return Ok(packages);
    }
    for entry in fs::read_dir(dir)? {
        let path = entry?.path();
        if path.extension().is_some_and(|ext| ext == "json") {
            let package = project_io::read_json::<Package>(&path)?;
            packages.insert(package.meta.uuid, package);
        }
    }
    Ok(packages)
}

fn score_match(haystack: &str, query: &str, component: &Component) -> usize {
    if component.meta.name.eq_ignore_ascii_case(query) {
        return 1000;
    }
    if component.meta.name.to_lowercase().contains(query) {
        return 500;
    }
    if haystack.contains(query) {
        return 100;
    }
    query
        .split_whitespace()
        .filter(|term| haystack.contains(term))
        .count()
}
