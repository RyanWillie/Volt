//! Round-trip JSON serialization tests for all volt-core types.
//!
//! For every major type: construct → serialize → deserialize → assert equal.

use uuid::Uuid;
use volt_core::common::*;
use volt_core::library::*;
use volt_core::project::*;

/// Helper: round-trip a value through JSON and assert equality.
fn roundtrip<T>(value: &T)
where
    T: serde::Serialize + serde::de::DeserializeOwned + PartialEq + std::fmt::Debug,
{
    let json = serde_json::to_string_pretty(value).expect("serialize");
    let back: T = serde_json::from_str(&json).expect("deserialize");
    assert_eq!(*value, back, "round-trip failed.\nJSON:\n{json}");
}

// ===========================================================================
// Common types
// ===========================================================================

#[test]
fn roundtrip_position() {
    roundtrip(&Position::new(1.234, -5.678));
}

#[test]
fn roundtrip_vertex() {
    roundtrip(&Vertex {
        position: Position::new(0.0, 0.0),
        angle: Angle(45.0),
    });
}

#[test]
fn roundtrip_layer_variants() {
    roundtrip(&Layer::TopCopper);
    roundtrip(&Layer::InnerCopper(3));
    roundtrip(&Layer::BottomCopper);
    roundtrip(&Layer::BoardOutlines);
    roundtrip(&Layer::SchOutlines);
}

#[test]
fn roundtrip_grid() {
    roundtrip(&Grid {
        interval: 2.54,
        unit: GridUnit::Millimeters,
    });
}

#[test]
fn roundtrip_stop_mask_config() {
    roundtrip(&StopMaskConfig::Auto);
    roundtrip(&StopMaskConfig::Manual(0.1));
    roundtrip(&StopMaskConfig::Off);
}

// ===========================================================================
// Library types
// ===========================================================================

fn test_meta() -> LibraryMeta {
    LibraryMeta {
        uuid: Uuid::nil(),
        name: "Test Element".into(),
        description: "A test element".into(),
        keywords: "test,example".into(),
        author: "Volt".into(),
        version: "0.1".into(),
        created: chrono::Utc::now(),
        deprecated: false,
        category: None,
    }
}

#[test]
fn roundtrip_symbol() {
    let sym = Symbol {
        meta: test_meta(),
        pins: vec![SymbolPin {
            uuid: Uuid::nil(),
            name: "1".into(),
            pin_name: "IN".into(),
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
        }],
        polygons: vec![Polygon {
            uuid: Uuid::nil(),
            layer: Layer::SchOutlines,
            width: 0.254,
            fill: false,
            grab_area: true,
            vertices: vec![
                Vertex {
                    position: Position::new(-3.08, -1.016),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(-3.08, 1.016),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(3.08, 1.016),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(3.08, -1.016),
                    angle: Angle(0.0),
                },
            ],
        }],
        texts: vec![SymbolText {
            uuid: Uuid::nil(),
            layer: Layer::SchNames,
            value: "{{NAME}}".into(),
            position: Position::new(-3.08, 1.016),
            rotation: Angle(0.0),
            height: 2.54,
            align: Alignment {
                h: HAlign::Left,
                v: VAlign::Bottom,
            },
            lock: false,
        }],
        grid_interval: 2.54,
    };
    roundtrip(&sym);
}

#[test]
fn roundtrip_component() {
    let cmp = Component {
        meta: test_meta(),
        prefix: "R".into(),
        default_value: "{{RESISTANCE}}".into(),
        schematic_only: false,
        attributes: vec![ComponentAttribute {
            key: "RESISTANCE".into(),
            type_name: "resistance".into(),
            unit: "ohm".into(),
            value: "".into(),
        }],
        signals: vec![Signal {
            uuid: Uuid::nil(),
            name: "1".into(),
            role: SignalRole::Passive,
            required: true,
            negated: false,
            clock: false,
            forced_net: String::new(),
        }],
        variants: vec![ComponentVariant {
            uuid: Uuid::nil(),
            norm: "IEC 60617".into(),
            name: "European".into(),
            description: String::new(),
            gates: vec![Gate {
                uuid: Uuid::nil(),
                symbol: Uuid::nil(),
                position: Position::default(),
                rotation: Angle::default(),
                required: true,
                suffix: String::new(),
                pin_mappings: vec![PinMapping {
                    pin: Uuid::nil(),
                    signal: Uuid::nil(),
                }],
            }],
        }],
    };
    roundtrip(&cmp);
}

#[test]
fn roundtrip_package() {
    let pkg = Package {
        meta: test_meta(),
        assembly_type: AssemblyType::Smt,
        grid_interval: 2.54,
        min_copper_clearance: 0.2,
        pads: vec![
            PackagePad {
                uuid: Uuid::nil(),
                name: "1".into(),
            },
            PackagePad {
                uuid: Uuid::nil(),
                name: "2".into(),
            },
        ],
        footprints: vec![Footprint {
            uuid: Uuid::nil(),
            name: "default".into(),
            description: String::new(),
            model_position: Position3D::default(),
            model_rotation: Position3D::default(),
            pads: vec![FootprintPad {
                uuid: Uuid::nil(),
                package_pad: Uuid::nil(),
                side: PadSide::Top,
                shape: PadShape::RoundRect,
                position: Position::new(2.311, 0.0),
                rotation: Angle(0.0),
                width: 2.0,
                height: 1.8,
                radius: 0.0,
                stop_mask: StopMaskConfig::Auto,
                solder_paste: SolderPasteConfig::Auto,
                clearance: 0.0,
                function: PadFunction::Unspecified,
                holes: vec![],
            }],
            polygons: vec![],
            texts: vec![],
        }],
    };
    roundtrip(&pkg);
}

#[test]
fn roundtrip_device() {
    let dev = Device {
        meta: test_meta(),
        component: Uuid::nil(),
        package: Uuid::nil(),
        pad_mappings: vec![DevicePadMapping {
            pad: Uuid::nil(),
            signal: Uuid::nil(),
            optional: false,
        }],
        parts: vec![DevicePart {
            mpn: "RC0805FR-0710KL".into(),
            manufacturer: "Yageo".into(),
            attributes: vec![],
        }],
    };
    roundtrip(&dev);
}

// ===========================================================================
// Project types
// ===========================================================================

#[test]
fn roundtrip_project_metadata() {
    let meta = ProjectMetadata {
        uuid: Uuid::nil(),
        name: "Test Project".into(),
        author: "Volt".into(),
        version: "v1".into(),
        schema_version: 1,
        created: chrono::Utc::now(),
        settings: ProjectSettings::default(),
    };
    roundtrip(&meta);
}

#[test]
fn roundtrip_circuit() {
    let circuit = Circuit {
        assembly_variants: vec![AssemblyVariant {
            uuid: Uuid::nil(),
            name: "Std".into(),
            description: String::new(),
        }],
        net_classes: vec![NetClass {
            uuid: Uuid::nil(),
            name: "default".into(),
            default_trace_width: TraceWidthConfig::Inherit,
            default_via_drill_diameter: TraceWidthConfig::Inherit,
            min_copper_copper_clearance: 0.0,
            min_copper_width: 0.0,
            min_via_drill_diameter: 0.0,
            diff_pair_gap: None,
            diff_pair_max_length_delta: None,
        }],
        nets: vec![Net {
            uuid: Uuid::nil(),
            name: "GND".into(),
            auto_name: false,
            net_class: Uuid::nil(),
            scope: NetScope::Global,
            owner_sheet: None,
            is_power: false,
        }],
        components: vec![],
        differential_pairs: vec![],
    };
    roundtrip(&circuit);
}

#[test]
fn roundtrip_schematic() {
    let sch = Schematic {
        uuid: Uuid::nil(),
        name: "Main".into(),
        grid: Grid {
            interval: 2.54,
            unit: GridUnit::Millimeters,
        },
        symbols: vec![],
        net_segments: vec![SchematicNetSegment {
            uuid: Uuid::nil(),
            net: Uuid::nil(),
            junctions: vec![Junction {
                uuid: Uuid::nil(),
                position: Position::new(10.0, 20.0),
            }],
            lines: vec![],
            labels: vec![NetLabel {
                uuid: Uuid::nil(),
                position: Position::new(10.0, 20.0),
                rotation: Angle(0.0),
                mirror: false,
            }],
        }],
        sheet_refs: vec![],
        hierarchical_ports: vec![],
        power_ports: vec![],
        power_flags: vec![],
        bus_segments: vec![],
        bus_entries: vec![],
        bus_aliases: vec![],
    };
    roundtrip(&sch);
}

#[test]
fn roundtrip_board() {
    let board = Board {
        uuid: Uuid::nil(),
        name: "default".into(),
        grid: Grid {
            interval: 1.0,
            unit: GridUnit::Millimeters,
        },
        inner_layers: 0,
        thickness: 1.6,
        solder_resist: SolderResistColor::Green,
        silkscreen: SilkscreenColor::White,
        default_font: "newstroke.bene".into(),
        design_rules: serde_json::from_str("{}").unwrap(),
        drc_settings: serde_json::from_str("{}").unwrap(),
        fabrication_output_settings: FabricationOutputSettings::default(),
        devices: vec![],
        net_segments: vec![],
        planes: vec![],
        polygons: vec![BoardPolygon {
            uuid: Uuid::nil(),
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
    roundtrip(&board);
}

#[test]
fn roundtrip_line_endpoint_enum() {
    let sym_ep = LineEndpoint::Symbol {
        symbol: Uuid::nil(),
        pin: Uuid::nil(),
    };
    roundtrip(&sym_ep);

    let junc_ep = LineEndpoint::Junction {
        junction: Uuid::nil(),
    };
    roundtrip(&junc_ep);
}

#[test]
fn roundtrip_trace_endpoint_enum() {
    let dev_ep = TraceEndpoint::Device {
        device: Uuid::nil(),
        pad: Uuid::nil(),
    };
    roundtrip(&dev_ep);

    let via_ep = TraceEndpoint::Via { via: Uuid::nil() };
    roundtrip(&via_ep);
}

/// Verify that the JSON output is human-readable / pretty-printed.
#[test]
fn json_is_pretty() {
    let meta = ProjectMetadata {
        uuid: Uuid::nil(),
        name: "Test".into(),
        author: "Volt".into(),
        version: "v1".into(),
        schema_version: 1,
        created: chrono::DateTime::parse_from_rfc3339("2026-01-01T00:00:00Z")
            .unwrap()
            .to_utc(),
        settings: ProjectSettings::default(),
    };
    let json = serde_json::to_string_pretty(&meta).unwrap();
    assert!(json.contains('\n'), "JSON should be pretty-printed");
    assert!(
        json.contains("\"name\": \"Test\""),
        "JSON should contain readable fields"
    );
}
