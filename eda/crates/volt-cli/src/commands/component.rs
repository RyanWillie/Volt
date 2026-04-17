//! `volt-eda component` subcommands.

use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};

use clap::Subcommand;
use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::*;
use volt_core::project::*;

use super::project_io::{self, Result};

#[derive(Subcommand)]
pub enum ComponentCommands {
    /// Add a new component instance to the circuit
    Add {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Designator name (e.g. "R1", "U1")
        #[arg(long)]
        name: String,
        /// Component value (e.g. "10k", "100nF")
        #[arg(long, default_value = "")]
        value: String,
        /// Library component UUID (if adding from existing library element)
        #[arg(long)]
        lib_component: Option<Uuid>,
        /// Library variant UUID
        #[arg(long)]
        lib_variant: Option<Uuid>,
        /// Assign a concrete library device UUID to this component instance
        #[arg(long)]
        device: Option<Uuid>,
        /// Create a simple 2-pin passive component inline (resistor/capacitor style)
        #[arg(long)]
        simple_passive: bool,
        /// Prefix for auto-created component (e.g. "R", "C")
        #[arg(long, default_value = "R")]
        prefix: String,
    },
    /// Assign or replace the physical device for an existing component instance
    AssignDevice {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Component designator (e.g. "R1", "U1")
        #[arg(long)]
        component: String,
        /// Library device UUID
        #[arg(long)]
        device: Uuid,
    },
    /// List all component instances in the circuit
    List {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
    },
}

pub fn component_command(cmd: ComponentCommands) -> Result<()> {
    match cmd {
        ComponentCommands::Add {
            project,
            name,
            value,
            lib_component,
            lib_variant,
            device,
            simple_passive,
            prefix,
        } => {
            project_io::ensure_project(&project)?;
            let mut circuit = project_io::read_circuit(&project)?;

            if simple_passive && (lib_component.is_some() || lib_variant.is_some() || device.is_some()) {
                return Err(
                    "--simple-passive cannot be combined with --lib-component, --lib-variant, or --device"
                        .into(),
                );
            }

            // Check for duplicate name
            if circuit.components.iter().any(|c| c.name == name) {
                return Err(format!("Component '{name}' already exists").into());
            }

            let (comp_uuid, variant_uuid, assigned_device_uuid) = if let (Some(cu), Some(vu)) =
                (lib_component, lib_variant)
            {
                let comp: Component = project_io::read_library_element(&project, "components", &cu)?;
                if !comp.variants.iter().any(|v| v.uuid == vu) {
                    return Err(
                        format!("Variant '{}' not found in component '{}'", vu, comp.meta.name).into(),
                    );
                }
                (cu, vu, device)
            } else if simple_passive {
                // Create a simple 2-pin passive component + symbol + package + device inline
                create_simple_passive(&project, &prefix)?
            } else {
                return Err(
                    "Must specify --lib-component and --lib-variant, or use --simple-passive"
                        .into(),
                );
            };

            let instance_uuid = new_uuid();

            // Look up the component to get its signals for connection stubs
            let comp: Component = project_io::read_library_element(&project, "components", &comp_uuid)?;

            let signal_connections: Vec<SignalConnection> = comp
                .signals
                .iter()
                .map(|s| SignalConnection {
                    signal: s.uuid,
                    net: None,
                })
                .collect();

            let device_assignments = match assigned_device_uuid {
                Some(device_uuid) => vec![build_validated_device_assignment(
                    &project,
                    &circuit,
                    &comp,
                    device_uuid,
                )?],
                None => vec![],
            };

            let instance = ComponentInstance {
                uuid: instance_uuid,
                lib_component: comp_uuid,
                lib_variant: variant_uuid,
                name: name.clone(),
                value,
                lock_assembly: false,
                device_assignments: device_assignments.clone(),
                signal_connections,
            };

            circuit.components.push(instance);
            project_io::write_circuit(&project, &circuit)?;

            let result = serde_json::json!({
                "status": "ok",
                "component": {
                    "uuid": instance_uuid.to_string(),
                    "name": name,
                    "lib_component": comp_uuid.to_string(),
                    "lib_variant": variant_uuid.to_string(),
                    "device_assignments": device_assignments.iter().map(|da| {
                        serde_json::json!({
                            "device": da.device.to_string(),
                            "variant": da.variant.to_string(),
                        })
                    }).collect::<Vec<_>>(),
                    "signals": comp.signals.iter().map(|s| {
                        serde_json::json!({
                            "uuid": s.uuid.to_string(),
                            "name": s.name,
                        })
                    }).collect::<Vec<_>>(),
                }
            });
            println!("{}", serde_json::to_string_pretty(&result)?);
            Ok(())
        }
        ComponentCommands::AssignDevice {
            project,
            component,
            device,
        } => {
            project_io::ensure_project(&project)?;
            let mut circuit = project_io::read_circuit(&project)?;

            let comp_index = circuit
                .components
                .iter()
                .position(|c| c.name == component)
                .ok_or_else(|| format!("Component '{}' not found in circuit", component))?;

            let comp_uuid = circuit.components[comp_index].uuid;
            if circuit.components[comp_index].lock_assembly {
                return Err(format!("Component '{}' has assembly locked", component).into());
            }
            if let Some(board_name) = find_board_with_component(&project, comp_uuid)? {
                return Err(format!(
                    "Component '{}' is already placed on board '{}'; reassignment is blocked",
                    component, board_name
                )
                .into());
            }

            let lib_component: Component = project_io::read_library_element(
                &project,
                "components",
                &circuit.components[comp_index].lib_component,
            )?;
            let assignment =
                build_validated_device_assignment(&project, &circuit, &lib_component, device)?;
            let replaced = upsert_device_assignment(
                &mut circuit.components[comp_index].device_assignments,
                assignment.clone(),
            );

            project_io::write_circuit(&project, &circuit)?;

            let result = serde_json::json!({
                "status": "ok",
                "component": component,
                "device_assignment": {
                    "device": assignment.device.to_string(),
                    "variant": assignment.variant.to_string(),
                },
                "action": if replaced { "updated" } else { "assigned" },
            });
            println!("{}", serde_json::to_string_pretty(&result)?);
            Ok(())
        }
        ComponentCommands::List { project } => {
            project_io::ensure_project(&project)?;
            let circuit = project_io::read_circuit(&project)?;

            let components: Vec<serde_json::Value> = circuit
                .components
                .iter()
                .map(|c| {
                    let connected_nets: Vec<&SignalConnection> = c
                        .signal_connections
                        .iter()
                        .filter(|sc| sc.net.is_some())
                        .collect();
                    serde_json::json!({
                        "uuid": c.uuid.to_string(),
                        "name": c.name,
                        "value": c.value,
                        "lib_component": c.lib_component.to_string(),
                        "signals_connected": connected_nets.len(),
                        "signals_total": c.signal_connections.len(),
                        "device_assignments": c.device_assignments.iter().map(|da| serde_json::json!({
                            "device": da.device.to_string(),
                            "variant": da.variant.to_string(),
                        })).collect::<Vec<_>>(),
                    })
                })
                .collect();

            let result = serde_json::json!({
                "status": "ok",
                "components": components,
                "count": components.len(),
            });
            println!("{}", serde_json::to_string_pretty(&result)?);
            Ok(())
        }
    }
}

fn default_assembly_variant_uuid(circuit: &Circuit) -> Result<Uuid> {
    circuit
        .assembly_variants
        .first()
        .map(|v| v.uuid)
        .ok_or_else(|| "Project has no assembly variants".into())
}

fn build_validated_device_assignment(
    project: &Path,
    circuit: &Circuit,
    lib_component: &Component,
    device_uuid: Uuid,
) -> Result<DeviceAssignment> {
    let variant_uuid = default_assembly_variant_uuid(circuit)?;
    let device: Device = project_io::read_library_element(project, "devices", &device_uuid)?;

    if device.component != lib_component.meta.uuid {
        return Err(format!(
            "Device '{}' does not belong to component '{}'",
            device_uuid, lib_component.meta.name
        )
        .into());
    }

    let package: Package = project_io::read_library_element(project, "packages", &device.package)?;
    if package.footprints.is_empty() {
        return Err(format!(
            "Device '{}' references package '{}' with no footprints",
            device_uuid, package.meta.uuid
        )
        .into());
    }

    let signal_names: HashMap<Uuid, String> = lib_component
        .signals
        .iter()
        .map(|s| (s.uuid, s.name.clone()))
        .collect();
    let required_signal_ids: HashSet<Uuid> = lib_component
        .signals
        .iter()
        .filter(|s| s.required)
        .map(|s| s.uuid)
        .collect();
    let package_pad_ids: HashSet<Uuid> = package.pads.iter().map(|p| p.uuid).collect();
    let mut mapped_required = HashSet::new();

    for mapping in &device.pad_mappings {
        if !package_pad_ids.contains(&mapping.pad) {
            return Err(format!(
                "Device '{}' pad mapping references unknown package pad '{}'",
                device_uuid, mapping.pad
            )
            .into());
        }
        if !signal_names.contains_key(&mapping.signal) {
            return Err(format!(
                "Device '{}' pad mapping references unknown component signal '{}'",
                device_uuid, mapping.signal
            )
            .into());
        }
        if required_signal_ids.contains(&mapping.signal) && !mapping.optional {
            mapped_required.insert(mapping.signal);
        }
    }

    let missing_required: Vec<String> = lib_component
        .signals
        .iter()
        .filter(|s| s.required && !mapped_required.contains(&s.uuid))
        .map(|s| s.name.clone())
        .collect();
    if !missing_required.is_empty() {
        return Err(format!(
            "Device '{}' is missing mappings for required signals: {}",
            device_uuid,
            missing_required.join(", ")
        )
        .into());
    }

    Ok(DeviceAssignment {
        device: device_uuid,
        variant: variant_uuid,
        part: DevicePartRef::default(),
    })
}

fn upsert_device_assignment(
    assignments: &mut Vec<DeviceAssignment>,
    new_assignment: DeviceAssignment,
) -> bool {
    let replaced = assignments
        .iter()
        .any(|assignment| assignment.variant == new_assignment.variant);
    assignments.retain(|assignment| assignment.variant != new_assignment.variant);
    assignments.push(new_assignment);
    replaced
}

fn find_board_with_component(project: &Path, component_uuid: Uuid) -> Result<Option<String>> {
    let boards_dir = project.join("boards");
    if !boards_dir.exists() {
        return Ok(None);
    }

    for entry in fs::read_dir(&boards_dir)? {
        let path = entry?.path();
        if path.extension().and_then(|ext| ext.to_str()) != Some("json") {
            continue;
        }
        let board: Board = project_io::read_json(&path)?;
        if board.devices.iter().any(|dev| dev.component == component_uuid) {
            return Ok(Some(board.name));
        }
    }

    Ok(None)
}

/// Create a simple 2-pin passive component (resistor/capacitor style) with
/// symbol, component, package, and device in the project library.
/// Returns (component_uuid, variant_uuid, device_uuid).
fn create_simple_passive(project: &std::path::Path, prefix: &str) -> Result<(Uuid, Uuid, Option<Uuid>)> {
    let now = chrono::Utc::now();

    // Symbol: simple 2-pin box
    let pin1_uuid = new_uuid();
    let pin2_uuid = new_uuid();
    let sym = Symbol {
        meta: LibraryMeta {
            uuid: new_uuid(),
            name: format!("{prefix} Symbol"),
            description: format!("Simple 2-pin {prefix} symbol"),
            keywords: String::new(),
            author: "volt-eda".into(),
            version: "0.1".into(),
            created: now,
            deprecated: false,
            category: None,
        },
        pins: vec![
            SymbolPin {
                uuid: pin1_uuid,
                name: "1".into(),
                pin_name: String::new(),
                position: Position::new(-5.08, 0.0),
                rotation: Angle(0.0),
                length: 2.0,
                name_position: Position::new(3.27, 0.0),
                name_rotation: Angle(0.0),
                name_height: 2.5,
                name_align: Alignment {
                    h: HAlign::Left,
                    v: VAlign::Center,
                },
            },
            SymbolPin {
                uuid: pin2_uuid,
                name: "2".into(),
                pin_name: String::new(),
                position: Position::new(5.08, 0.0),
                rotation: Angle(180.0),
                length: 2.0,
                name_position: Position::new(3.27, 0.0),
                name_rotation: Angle(0.0),
                name_height: 2.5,
                name_align: Alignment {
                    h: HAlign::Left,
                    v: VAlign::Center,
                },
            },
        ],
        polygons: vec![Polygon {
            uuid: new_uuid(),
            layer: Layer::SchOutlines,
            width: 0.254,
            fill: false,
            grab_area: true,
            vertices: vec![
                Vertex { position: Position::new(-3.08, -1.016), angle: Angle(0.0) },
                Vertex { position: Position::new(-3.08, 1.016), angle: Angle(0.0) },
                Vertex { position: Position::new(3.08, 1.016), angle: Angle(0.0) },
                Vertex { position: Position::new(3.08, -1.016), angle: Angle(0.0) },
                Vertex { position: Position::new(-3.08, -1.016), angle: Angle(0.0) },
            ],
        }],
        texts: vec![
            SymbolText {
                uuid: new_uuid(),
                layer: Layer::SchNames,
                value: "{{NAME}}".into(),
                position: Position::new(-3.08, 1.016),
                rotation: Angle(0.0),
                height: 2.54,
                align: Alignment { h: HAlign::Left, v: VAlign::Bottom },
                lock: false,
            },
            SymbolText {
                uuid: new_uuid(),
                layer: Layer::SchValues,
                value: "{{VALUE}}".into(),
                position: Position::new(-3.08, -1.016),
                rotation: Angle(0.0),
                height: 2.54,
                align: Alignment { h: HAlign::Left, v: VAlign::Top },
                lock: false,
            },
        ],
        grid_interval: 2.54,
    };
    project_io::write_library_element(project, "symbols", &sym.meta.uuid, &sym)?;

    // Component: 2 signals, 1 gate
    let sig1_uuid = new_uuid();
    let sig2_uuid = new_uuid();
    let gate_uuid = new_uuid();
    let variant_uuid = new_uuid();

    let comp = Component {
        meta: LibraryMeta {
            uuid: new_uuid(),
            name: format!("{prefix} Passive"),
            description: format!("Simple 2-pin passive ({prefix})"),
            keywords: String::new(),
            author: "volt-eda".into(),
            version: "0.1".into(),
            created: now,
            deprecated: false,
            category: None,
        },
        prefix: prefix.to_string(),
        default_value: String::new(),
        schematic_only: false,
        attributes: vec![],
        signals: vec![
            Signal {
                uuid: sig1_uuid,
                name: "1".into(),
                role: SignalRole::Passive,
                required: true,
                negated: false,
                clock: false,
                forced_net: String::new(),
            },
            Signal {
                uuid: sig2_uuid,
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
            gates: vec![Gate {
                uuid: gate_uuid,
                symbol: sym.meta.uuid,
                position: Position::default(),
                rotation: Angle::default(),
                required: true,
                suffix: String::new(),
                pin_mappings: vec![
                    PinMapping { pin: pin1_uuid, signal: sig1_uuid },
                    PinMapping { pin: pin2_uuid, signal: sig2_uuid },
                ],
            }],
        }],
    };
    project_io::write_library_element(project, "components", &comp.meta.uuid, &comp)?;

    // Package: simple 2-pad SMD (0805 style)
    let pad1_uuid = new_uuid();
    let pad2_uuid = new_uuid();
    let pkg = Package {
        meta: LibraryMeta {
            uuid: new_uuid(),
            name: "0805".into(),
            description: "Generic 0805 (2012 metric) 2-pad SMD".into(),
            keywords: "0805,2012".into(),
            author: "volt-eda".into(),
            version: "0.1".into(),
            created: now,
            deprecated: false,
            category: None,
        },
        assembly_type: AssemblyType::Smt,
        grid_interval: 2.54,
        min_copper_clearance: 0.2,
        pads: vec![
            PackagePad { uuid: pad1_uuid, name: "1".into() },
            PackagePad { uuid: pad2_uuid, name: "2".into() },
        ],
        footprints: vec![Footprint {
            uuid: new_uuid(),
            name: "default".into(),
            description: String::new(),
            model_position: Position3D::default(),
            model_rotation: Position3D::default(),
            pads: vec![
                FootprintPad {
                    uuid: new_uuid(),
                    package_pad: pad1_uuid,
                    side: PadSide::Top,
                    shape: PadShape::RoundRect,
                    position: Position::new(-1.0, 0.0),
                    rotation: Angle(0.0),
                    width: 1.0,
                    height: 1.3,
                    radius: 0.0,
                    stop_mask: StopMaskConfig::Auto,
                    solder_paste: SolderPasteConfig::Auto,
                    clearance: 0.0,
                    function: PadFunction::Standard,
                    holes: vec![],
                },
                FootprintPad {
                    uuid: new_uuid(),
                    package_pad: pad2_uuid,
                    side: PadSide::Top,
                    shape: PadShape::RoundRect,
                    position: Position::new(1.0, 0.0),
                    rotation: Angle(0.0),
                    width: 1.0,
                    height: 1.3,
                    radius: 0.0,
                    stop_mask: StopMaskConfig::Auto,
                    solder_paste: SolderPasteConfig::Auto,
                    clearance: 0.0,
                    function: PadFunction::Standard,
                    holes: vec![],
                },
            ],
            polygons: vec![
                // Silkscreen outline (F.SilkS equivalent)
                Polygon {
                    uuid: new_uuid(),
                    layer: Layer::TopLegend,
                    width: 0.12,
                    fill: false,
                    grab_area: false,
                    vertices: vec![
                        Vertex { position: Position::new(-1.0, -0.65), angle: Angle(0.0) },
                        Vertex { position: Position::new(1.0, -0.65), angle: Angle(0.0) },
                        Vertex { position: Position::new(1.0, 0.65), angle: Angle(0.0) },
                        Vertex { position: Position::new(-1.0, 0.65), angle: Angle(0.0) },
                        Vertex { position: Position::new(-1.0, -0.65), angle: Angle(0.0) },
                    ],
                },
                // Courtyard
                Polygon {
                    uuid: new_uuid(),
                    layer: Layer::TopCourtyard,
                    width: 0.05,
                    fill: false,
                    grab_area: false,
                    vertices: vec![
                        Vertex { position: Position::new(-1.68, -0.95), angle: Angle(0.0) },
                        Vertex { position: Position::new(1.68, -0.95), angle: Angle(0.0) },
                        Vertex { position: Position::new(1.68, 0.95), angle: Angle(0.0) },
                        Vertex { position: Position::new(-1.68, 0.95), angle: Angle(0.0) },
                        Vertex { position: Position::new(-1.68, -0.95), angle: Angle(0.0) },
                    ],
                },
            ],
            texts: vec![
                StrokeText {
                    uuid: new_uuid(),
                    layer: Layer::TopLegend,
                    value: "{{NAME}}".into(),
                    position: Position::new(0.0, -1.4),
                    rotation: Angle(0.0),
                    height: 1.0,
                    stroke_width: 0.15,
                    letter_spacing: None,
                    line_spacing: None,
                    align: Alignment { h: HAlign::Center, v: VAlign::Bottom },
                    mirror: false,
                    auto_rotate: true,
                    lock: false,
                },
                StrokeText {
                    uuid: new_uuid(),
                    layer: Layer::TopDocumentation,
                    value: "{{VALUE}}".into(),
                    position: Position::new(0.0, 1.4),
                    rotation: Angle(0.0),
                    height: 1.0,
                    stroke_width: 0.15,
                    letter_spacing: None,
                    line_spacing: None,
                    align: Alignment { h: HAlign::Center, v: VAlign::Top },
                    mirror: false,
                    auto_rotate: true,
                    lock: false,
                },
            ],
        }],
    };
    project_io::write_library_element(project, "packages", &pkg.meta.uuid, &pkg)?;

    // Device: binds component to package
    let dev = Device {
        meta: LibraryMeta {
            uuid: new_uuid(),
            name: format!("{prefix}-0805"),
            description: format!("{prefix} in 0805 package"),
            keywords: String::new(),
            author: "volt-eda".into(),
            version: "0.1".into(),
            created: now,
            deprecated: false,
            category: None,
        },
        component: comp.meta.uuid,
        package: pkg.meta.uuid,
        pad_mappings: vec![
            DevicePadMapping { pad: pad1_uuid, signal: sig1_uuid, optional: false },
            DevicePadMapping { pad: pad2_uuid, signal: sig2_uuid, optional: false },
        ],
        parts: vec![],
    };
    project_io::write_library_element(project, "devices", &dev.meta.uuid, &dev)?;

    Ok((comp.meta.uuid, variant_uuid, Some(dev.meta.uuid)))
}

#[cfg(test)]
mod tests {
    use std::path::{Path, PathBuf};

    use tempfile::TempDir;

    use super::*;
    use crate::commands::new_project;

    fn create_temp_project() -> (TempDir, PathBuf) {
        let dir = tempfile::tempdir().unwrap();
        let project = dir.path().join("proj");
        new_project("proj", Some(&project)).unwrap();
        (dir, project)
    }

    fn seed_library_component_with_device(project: &Path, missing_required_mapping: bool) -> (Uuid, Uuid, Uuid) {
        let now = chrono::Utc::now();
        let signal_in = new_uuid();
        let signal_out = new_uuid();
        let variant_uuid = new_uuid();
        let component_uuid = new_uuid();
        let pad_in = new_uuid();
        let pad_out = new_uuid();
        let footprint_uuid = new_uuid();
        let device_uuid = new_uuid();

        let component = Component {
            meta: LibraryMeta {
                uuid: component_uuid,
                name: "Test IC".into(),
                description: String::new(),
                keywords: String::new(),
                author: "test".into(),
                version: "0.1".into(),
                created: now,
                deprecated: false,
                category: None,
            },
            prefix: "U".into(),
            default_value: String::new(),
            schematic_only: false,
            attributes: vec![],
            signals: vec![
                Signal {
                    uuid: signal_in,
                    name: "IN".into(),
                    role: SignalRole::Input,
                    required: true,
                    negated: false,
                    clock: false,
                    forced_net: String::new(),
                },
                Signal {
                    uuid: signal_out,
                    name: "OUT".into(),
                    role: SignalRole::Output,
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
        project_io::write_library_element(project, "components", &component_uuid, &component).unwrap();

        let package_uuid = new_uuid();
        let package = Package {
            meta: LibraryMeta {
                uuid: package_uuid,
                name: "PKG".into(),
                description: String::new(),
                keywords: String::new(),
                author: "test".into(),
                version: "0.1".into(),
                created: now,
                deprecated: false,
                category: None,
            },
            assembly_type: AssemblyType::Smt,
            grid_interval: 1.0,
            min_copper_clearance: 0.2,
            pads: vec![
                PackagePad { uuid: pad_in, name: "1".into() },
                PackagePad { uuid: pad_out, name: "2".into() },
            ],
            footprints: vec![Footprint {
                uuid: footprint_uuid,
                name: "default".into(),
                description: String::new(),
                model_position: Position3D::default(),
                model_rotation: Position3D::default(),
                pads: vec![
                    FootprintPad {
                        uuid: new_uuid(),
                        package_pad: pad_in,
                        side: PadSide::Top,
                        shape: PadShape::RoundRect,
                        position: Position::new(-1.0, 0.0),
                        rotation: Angle(0.0),
                        width: 1.0,
                        height: 1.0,
                        radius: 0.0,
                        stop_mask: StopMaskConfig::Auto,
                        solder_paste: SolderPasteConfig::Auto,
                        clearance: 0.0,
                        function: PadFunction::Standard,
                        holes: vec![],
                    },
                    FootprintPad {
                        uuid: new_uuid(),
                        package_pad: pad_out,
                        side: PadSide::Top,
                        shape: PadShape::RoundRect,
                        position: Position::new(1.0, 0.0),
                        rotation: Angle(0.0),
                        width: 1.0,
                        height: 1.0,
                        radius: 0.0,
                        stop_mask: StopMaskConfig::Auto,
                        solder_paste: SolderPasteConfig::Auto,
                        clearance: 0.0,
                        function: PadFunction::Standard,
                        holes: vec![],
                    },
                ],
                polygons: vec![],
                texts: vec![],
            }],
        };
        project_io::write_library_element(project, "packages", &package_uuid, &package).unwrap();

        let mut pad_mappings = vec![DevicePadMapping {
            pad: pad_in,
            signal: signal_in,
            optional: false,
        }];
        if !missing_required_mapping {
            pad_mappings.push(DevicePadMapping {
                pad: pad_out,
                signal: signal_out,
                optional: false,
            });
        }

        let device = Device {
            meta: LibraryMeta {
                uuid: device_uuid,
                name: "Test IC Device".into(),
                description: String::new(),
                keywords: String::new(),
                author: "test".into(),
                version: "0.1".into(),
                created: now,
                deprecated: false,
                category: None,
            },
            component: component_uuid,
            package: package_uuid,
            pad_mappings,
            parts: vec![],
        };
        project_io::write_library_element(project, "devices", &device_uuid, &device).unwrap();

        (component_uuid, variant_uuid, device_uuid)
    }

    #[test]
    fn component_add_with_device_writes_assignment() {
        let (_tmp, project) = create_temp_project();
        let (component_uuid, variant_uuid, device_uuid) =
            seed_library_component_with_device(&project, false);

        component_command(ComponentCommands::Add {
            project: project.clone(),
            name: "U1".into(),
            value: String::new(),
            lib_component: Some(component_uuid),
            lib_variant: Some(variant_uuid),
            device: Some(device_uuid),
            simple_passive: false,
            prefix: "U".into(),
        })
        .unwrap();

        let circuit = project_io::read_circuit(&project).unwrap();
        let component = circuit.components.iter().find(|c| c.name == "U1").unwrap();
        assert_eq!(component.device_assignments.len(), 1);
        assert_eq!(component.device_assignments[0].device, device_uuid);
        assert_eq!(
            component.device_assignments[0].variant,
            circuit.assembly_variants[0].uuid
        );
    }

    #[test]
    fn simple_passive_add_auto_assigns_created_device() {
        let (_tmp, project) = create_temp_project();

        component_command(ComponentCommands::Add {
            project: project.clone(),
            name: "R1".into(),
            value: "10k".into(),
            lib_component: None,
            lib_variant: None,
            device: None,
            simple_passive: true,
            prefix: "R".into(),
        })
        .unwrap();

        let circuit = project_io::read_circuit(&project).unwrap();
        let component = circuit.components.iter().find(|c| c.name == "R1").unwrap();
        assert_eq!(component.device_assignments.len(), 1);
        let device_uuid = component.device_assignments[0].device;
        let _: Device = project_io::read_library_element(&project, "devices", &device_uuid).unwrap();
    }

    #[test]
    fn assign_device_updates_existing_component() {
        let (_tmp, project) = create_temp_project();
        let (component_uuid, variant_uuid, device_uuid) =
            seed_library_component_with_device(&project, false);

        component_command(ComponentCommands::Add {
            project: project.clone(),
            name: "U1".into(),
            value: String::new(),
            lib_component: Some(component_uuid),
            lib_variant: Some(variant_uuid),
            device: None,
            simple_passive: false,
            prefix: "U".into(),
        })
        .unwrap();

        component_command(ComponentCommands::AssignDevice {
            project: project.clone(),
            component: "U1".into(),
            device: device_uuid,
        })
        .unwrap();

        let circuit = project_io::read_circuit(&project).unwrap();
        let component = circuit.components.iter().find(|c| c.name == "U1").unwrap();
        assert_eq!(component.device_assignments.len(), 1);
        assert_eq!(component.device_assignments[0].device, device_uuid);
    }

    #[test]
    fn assign_device_rejects_missing_required_mapping() {
        let (_tmp, project) = create_temp_project();
        let (component_uuid, variant_uuid, bad_device_uuid) =
            seed_library_component_with_device(&project, true);

        component_command(ComponentCommands::Add {
            project: project.clone(),
            name: "U1".into(),
            value: String::new(),
            lib_component: Some(component_uuid),
            lib_variant: Some(variant_uuid),
            device: None,
            simple_passive: false,
            prefix: "U".into(),
        })
        .unwrap();

        let err = component_command(ComponentCommands::AssignDevice {
            project: project.clone(),
            component: "U1".into(),
            device: bad_device_uuid,
        })
        .unwrap_err();
        assert!(err.to_string().contains("missing mappings for required signals"));

        let circuit = project_io::read_circuit(&project).unwrap();
        let component = circuit.components.iter().find(|c| c.name == "U1").unwrap();
        assert!(component.device_assignments.is_empty());
    }
}
