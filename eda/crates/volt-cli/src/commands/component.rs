//! `volt-eda component` subcommands.

use std::path::PathBuf;

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
        /// Create a simple 2-pin passive component inline (resistor/capacitor style)
        #[arg(long)]
        simple_passive: bool,
        /// Prefix for auto-created component (e.g. "R", "C")
        #[arg(long, default_value = "R")]
        prefix: String,
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
            simple_passive,
            prefix,
        } => {
            project_io::ensure_project(&project)?;
            let mut circuit = project_io::read_circuit(&project)?;

            // Check for duplicate name
            if circuit.components.iter().any(|c| c.name == name) {
                return Err(format!("Component '{name}' already exists").into());
            }

            let (comp_uuid, variant_uuid) = if let (Some(cu), Some(vu)) =
                (lib_component, lib_variant)
            {
                // Verify the library component exists
                let _comp: Component =
                    project_io::read_library_element(&project, "components", &cu)?;
                (cu, vu)
            } else if simple_passive {
                // Create a simple 2-pin passive component + symbol + package + device inline
                let (cu, vu) = create_simple_passive(&project, &prefix)?;
                (cu, vu)
            } else {
                return Err(
                    "Must specify --lib-component and --lib-variant, or use --simple-passive"
                        .into(),
                );
            };

            let instance_uuid = new_uuid();

            // Look up the component to get its signals for connection stubs
            let comp: Component =
                project_io::read_library_element(&project, "components", &comp_uuid)?;

            let signal_connections: Vec<SignalConnection> = comp
                .signals
                .iter()
                .map(|s| SignalConnection {
                    signal: s.uuid,
                    net: None,
                })
                .collect();

            let instance = ComponentInstance {
                uuid: instance_uuid,
                lib_component: comp_uuid,
                lib_variant: variant_uuid,
                name: name.clone(),
                value,
                lock_assembly: false,
                device_assignments: vec![],
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

/// Create a simple 2-pin passive component (resistor/capacitor style) with
/// symbol, component, package, and device in the project library.
/// Returns (component_uuid, variant_uuid).
fn create_simple_passive(project: &std::path::Path, prefix: &str) -> Result<(Uuid, Uuid)> {
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
            polygons: vec![],
            texts: vec![],
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

    Ok((comp.meta.uuid, variant_uuid))
}
