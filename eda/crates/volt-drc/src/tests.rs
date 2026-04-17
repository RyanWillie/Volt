use std::collections::HashMap;

use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::*;
use volt_core::project::*;

use crate::*;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

fn make_design_rules() -> DesignRules {
    serde_json::from_str("{}").unwrap()
}

fn make_drc_settings() -> DrcSettings {
    serde_json::from_str("{}").unwrap()
}

fn make_fab_settings() -> FabricationOutputSettings {
    FabricationOutputSettings::default()
}

fn make_grid() -> Grid {
    Grid {
        interval: 2.54,
        unit: GridUnit::Millimeters,
    }
}

#[allow(dead_code)]
fn make_lib_meta(uuid: Uuid, name: &str) -> LibraryMeta {
    LibraryMeta {
        uuid,
        name: name.into(),
        description: String::new(),
        keywords: String::new(),
        author: String::new(),
        version: "0.1".into(),
        created: chrono::Utc::now(),
        deprecated: false,
        category: None,
    }
}

/// Create a minimal board with outline and no devices/traces.
fn make_clean_board() -> Board {
    Board {
        uuid: Uuid::new_v4(),
        name: "test".into(),
        grid: make_grid(),
        inner_layers: 0,
        thickness: 1.6,
        solder_resist: SolderResistColor::Green,
        silkscreen: SilkscreenColor::White,
        default_font: "newstroke.bene".into(),
        design_rules: make_design_rules(),
        drc_settings: make_drc_settings(),
        fabrication_output_settings: make_fab_settings(),
        devices: vec![],
        net_segments: vec![],
        planes: vec![],
        polygons: vec![board_outline(0.0, 0.0, 100.0, 100.0)],
        holes: vec![],
    }
}

fn board_outline(x1: f64, y1: f64, x2: f64, y2: f64) -> BoardPolygon {
    BoardPolygon {
        uuid: Uuid::new_v4(),
        layer: Layer::BoardOutlines,
        width: 0.0,
        fill: false,
        grab_area: false,
        lock: false,
        vertices: vec![
            Vertex {
                position: Position::new(x1, y1),
                angle: Angle(0.0),
            },
            Vertex {
                position: Position::new(x2, y1),
                angle: Angle(0.0),
            },
            Vertex {
                position: Position::new(x2, y2),
                angle: Angle(0.0),
            },
            Vertex {
                position: Position::new(x1, y2),
                angle: Angle(0.0),
            },
        ],
    }
}

fn empty_circuit() -> Circuit {
    Circuit::default()
}

fn empty_library() -> MapBoardLibrary {
    MapBoardLibrary {
        devices: HashMap::new(),
        packages: HashMap::new(),
    }
}

fn make_trace(layer: Layer, width: f64, from_junction: Uuid, to_junction: Uuid) -> Trace {
    Trace {
        uuid: Uuid::new_v4(),
        layer,
        width,
        from: TraceEndpoint::Junction {
            junction: from_junction,
        },
        to: TraceEndpoint::Junction {
            junction: to_junction,
        },
    }
}

fn make_junction(x: f64, y: f64) -> Junction {
    Junction {
        uuid: Uuid::new_v4(),
        position: Position::new(x, y),
    }
}

fn make_via(x: f64, y: f64, drill: f64, from_layer: Layer, to_layer: Layer) -> Via {
    Via {
        uuid: Uuid::new_v4(),
        from_layer,
        to_layer,
        position: Position::new(x, y),
        drill,
        size: ViaSize::Auto,
        exposure: ViaExposure::Auto,
    }
}

fn make_library_package_with_single_pad(
    package_uuid: Uuid,
    footprint_uuid: Uuid,
    package_pad_uuid: Uuid,
    footprint_pad_uuid: Uuid,
) -> Package {
    Package {
        meta: make_lib_meta(package_uuid, "PKG"),
        assembly_type: AssemblyType::Smt,
        grid_interval: 1.0,
        min_copper_clearance: 0.2,
        pads: vec![PackagePad {
            uuid: package_pad_uuid,
            name: "1".into(),
        }],
        footprints: vec![Footprint {
            uuid: footprint_uuid,
            name: "default".into(),
            description: String::new(),
            model_position: Position3D::default(),
            model_rotation: Position3D::default(),
            pads: vec![FootprintPad {
                uuid: footprint_pad_uuid,
                package_pad: package_pad_uuid,
                side: PadSide::Top,
                shape: PadShape::RoundRect,
                position: Position::new(0.0, 0.0),
                rotation: Angle(0.0),
                width: 1.0,
                height: 1.0,
                radius: 0.0,
                stop_mask: StopMaskConfig::Auto,
                solder_paste: SolderPasteConfig::Auto,
                clearance: 0.0,
                function: PadFunction::Standard,
                holes: vec![],
            }],
            polygons: vec![],
            texts: vec![],
        }],
    }
}

fn make_two_component_net_fixture() -> (Board, Circuit, MapBoardLibrary, Uuid) {
    let mut board = make_clean_board();

    let net_class_uuid = Uuid::new_v4();
    let net_uuid = Uuid::new_v4();
    let signal_uuid = Uuid::new_v4();
    let component_lib_uuid = Uuid::new_v4();
    let variant_uuid = Uuid::new_v4();
    let package_uuid = Uuid::new_v4();
    let package_pad_uuid = Uuid::new_v4();
    let footprint_uuid = Uuid::new_v4();
    let footprint_pad_uuid = Uuid::new_v4();
    let device_uuid = Uuid::new_v4();
    let component_a_uuid = Uuid::new_v4();
    let component_b_uuid = Uuid::new_v4();

    let circuit = Circuit {
        assembly_variants: vec![AssemblyVariant {
            uuid: Uuid::new_v4(),
            name: "Std".into(),
            description: String::new(),
        }],
        net_classes: vec![NetClass {
            uuid: net_class_uuid,
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
            uuid: net_uuid,
            name: "N1".into(),
            auto_name: false,
            net_class: net_class_uuid,
            scope: NetScope::Global,
            owner_sheet: None,
            is_power: false,
        }],
        components: vec![
            ComponentInstance {
                uuid: component_a_uuid,
                lib_component: component_lib_uuid,
                lib_variant: variant_uuid,
                name: "U1".into(),
                value: String::new(),
                lock_assembly: false,
                device_assignments: vec![],
                signal_connections: vec![SignalConnection {
                    signal: signal_uuid,
                    net: Some(net_uuid),
                }],
            },
            ComponentInstance {
                uuid: component_b_uuid,
                lib_component: component_lib_uuid,
                lib_variant: variant_uuid,
                name: "U2".into(),
                value: String::new(),
                lock_assembly: false,
                device_assignments: vec![],
                signal_connections: vec![SignalConnection {
                    signal: signal_uuid,
                    net: Some(net_uuid),
                }],
            },
        ],
        differential_pairs: vec![],
    };

    let device = Device {
        meta: make_lib_meta(device_uuid, "DEV"),
        component: component_lib_uuid,
        package: package_uuid,
        pad_mappings: vec![DevicePadMapping {
            pad: package_pad_uuid,
            signal: signal_uuid,
            optional: false,
        }],
        parts: vec![],
    };
    let package = make_library_package_with_single_pad(
        package_uuid,
        footprint_uuid,
        package_pad_uuid,
        footprint_pad_uuid,
    );
    let mut devices = HashMap::new();
    devices.insert(device_uuid, device);
    let mut packages = HashMap::new();
    packages.insert(package_uuid, package);

    board.devices = vec![
        BoardDevice {
            component: component_a_uuid,
            lib_device: device_uuid,
            lib_footprint: footprint_uuid,
            position: Position::new(20.0, 20.0),
            rotation: Angle(0.0),
            flip: false,
            lock: false,
            texts: vec![],
        },
        BoardDevice {
            component: component_b_uuid,
            lib_device: device_uuid,
            lib_footprint: footprint_uuid,
            position: Position::new(80.0, 20.0),
            rotation: Angle(0.0),
            flip: false,
            lock: false,
            texts: vec![],
        },
    ];

    (
        board,
        circuit,
        MapBoardLibrary { devices, packages },
        net_uuid,
    )
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[test]
fn clean_board_passes() {
    let board = make_clean_board();
    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(
        result.passed,
        "Expected clean pass, got: {:?}",
        result.diagnostics
    );
    assert_eq!(result.errors, 0);
}

#[test]
fn d006_trace_below_minimum_width() {
    let mut board = make_clean_board();
    // min_copper_width default is 0.2mm
    let j1 = make_junction(10.0, 10.0);
    let j2 = make_junction(20.0, 10.0);
    let thin_trace = make_trace(Layer::TopCopper, 0.05, j1.uuid, j2.uuid); // 0.05mm < 0.2mm

    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(Uuid::new_v4()),
        traces: vec![thin_trace],
        vias: vec![],
        junctions: vec![j1, j2],
        pads: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(!result.passed);
    let d006 = result.diagnostics.iter().find(|d| d.rule == "D006");
    assert!(
        d006.is_some(),
        "Expected D006 violation, got: {:?}",
        result.diagnostics
    );
    assert_eq!(d006.unwrap().severity, Severity::Error);
    assert!(d006.unwrap().message.contains("0.050"));
}

#[test]
fn d006_trace_at_minimum_width_passes() {
    let mut board = make_clean_board();
    let j1 = make_junction(10.0, 10.0);
    let j2 = make_junction(20.0, 10.0);
    let ok_trace = make_trace(Layer::TopCopper, 0.2, j1.uuid, j2.uuid); // exactly 0.2mm

    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(Uuid::new_v4()),
        traces: vec![ok_trace],
        vias: vec![],
        junctions: vec![j1, j2],
        pads: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(
        !result.diagnostics.iter().any(|d| d.rule == "D006"),
        "Trace at minimum width should not trigger D006"
    );
}

#[test]
fn d008_npth_drill_undersized() {
    let mut board = make_clean_board();
    // min_npth_drill_diameter default is 0.25mm
    board.holes.push(BoardHole {
        uuid: Uuid::new_v4(),
        diameter: 0.1, // too small
        stop_mask: StopMaskConfig::Auto,
        lock: false,
        path: vec![Vertex {
            position: Position::new(50.0, 50.0),
            angle: Angle(0.0),
        }],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(!result.passed);
    let d008 = result.diagnostics.iter().find(|d| d.rule == "D008");
    assert!(
        d008.is_some(),
        "Expected D008 violation, got: {:?}",
        result.diagnostics
    );
    assert!(d008.unwrap().message.contains("0.100"));
}

#[test]
fn d009_pth_drill_undersized() {
    let mut board = make_clean_board();
    // min_pth_drill_diameter default is 0.25mm
    let small_via = make_via(50.0, 50.0, 0.1, Layer::TopCopper, Layer::BottomCopper);

    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(Uuid::new_v4()),
        traces: vec![],
        vias: vec![small_via],
        junctions: vec![],
        pads: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(!result.passed);
    let d009 = result.diagnostics.iter().find(|d| d.rule == "D009");
    assert!(
        d009.is_some(),
        "Expected D009 violation, got: {:?}",
        result.diagnostics
    );
    assert!(d009.unwrap().message.contains("0.100"));
}

#[test]
fn d011_missing_board_outline() {
    let mut board = make_clean_board();
    board.polygons.clear(); // remove outline

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(!result.passed);
    let d011 = result.diagnostics.iter().find(|d| d.rule == "D011");
    assert!(d011.is_some(), "Expected D011 violation");
    assert_eq!(d011.unwrap().severity, Severity::Error);
    assert!(d011.unwrap().message.contains("outline"));
}

#[test]
fn d013_blind_via_not_allowed() {
    let mut board = make_clean_board();
    board.inner_layers = 2;
    board.drc_settings.blind_vias_allowed = false;

    // Blind via: TopCopper → InnerCopper(1)
    let blind_via = make_via(50.0, 50.0, 0.3, Layer::TopCopper, Layer::InnerCopper(1));

    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(Uuid::new_v4()),
        traces: vec![],
        vias: vec![blind_via],
        junctions: vec![],
        pads: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(!result.passed);
    let d013 = result.diagnostics.iter().find(|d| d.rule == "D013");
    assert!(
        d013.is_some(),
        "Expected D013 violation, got: {:?}",
        result.diagnostics
    );
    assert!(d013.unwrap().message.contains("Blind via"));
}

#[test]
fn d013_buried_via_not_allowed() {
    let mut board = make_clean_board();
    board.inner_layers = 2;
    board.drc_settings.buried_vias_allowed = false;

    // Buried via: InnerCopper(1) → InnerCopper(2) — touches neither top nor bottom
    let buried_via = make_via(
        50.0,
        50.0,
        0.3,
        Layer::InnerCopper(1),
        Layer::InnerCopper(2),
    );

    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(Uuid::new_v4()),
        traces: vec![],
        vias: vec![buried_via],
        junctions: vec![],
        pads: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(!result.passed);
    let d013 = result.diagnostics.iter().find(|d| d.rule == "D013");
    assert!(
        d013.is_some(),
        "Expected D013 violation, got: {:?}",
        result.diagnostics
    );
    assert!(d013.unwrap().message.contains("Buried via"));
}

#[test]
fn d013_through_via_always_ok() {
    let mut board = make_clean_board();
    board.inner_layers = 2;
    board.drc_settings.blind_vias_allowed = false;
    board.drc_settings.buried_vias_allowed = false;

    // Through via: TopCopper → BottomCopper — always allowed
    let through_via = make_via(50.0, 50.0, 0.3, Layer::TopCopper, Layer::BottomCopper);

    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(Uuid::new_v4()),
        traces: vec![],
        vias: vec![through_via],
        junctions: vec![],
        pads: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(
        !result.diagnostics.iter().any(|d| d.rule == "D013"),
        "Through via should not trigger D013"
    );
}

#[test]
fn d013_blind_via_allowed() {
    let mut board = make_clean_board();
    board.inner_layers = 2;
    board.drc_settings.blind_vias_allowed = true;

    let blind_via = make_via(50.0, 50.0, 0.3, Layer::TopCopper, Layer::InnerCopper(1));

    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(Uuid::new_v4()),
        traces: vec![],
        vias: vec![blind_via],
        junctions: vec![],
        pads: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(
        !result.diagnostics.iter().any(|d| d.rule == "D013"),
        "Blind via should be OK when allowed"
    );
}

#[test]
fn d010_unplaced_device() {
    let mut board = make_clean_board();
    board.devices.push(BoardDevice {
        component: Uuid::new_v4(),
        lib_device: Uuid::new_v4(),
        lib_footprint: Uuid::new_v4(),
        position: Position::new(0.0, 0.0), // unplaced
        rotation: Angle(0.0),
        flip: false,
        lock: false,
        texts: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    let d010 = result.diagnostics.iter().find(|d| d.rule == "D010");
    assert!(d010.is_some(), "Expected D010 warning");
    assert_eq!(d010.unwrap().severity, Severity::Warning);
}

#[test]
fn drc_result_is_json_serializable() {
    let result = DrcResult {
        passed: true,
        errors: 0,
        warnings: 1,
        diagnostics: vec![DrcDiagnostic {
            rule: "D010".into(),
            severity: Severity::Warning,
            message: "test warning".into(),
            location: Some(Position::new(1.0, 2.0)),
            object: None,
        }],
    };
    let json = serde_json::to_string_pretty(&result).unwrap();
    assert!(json.contains("\"passed\": true"));
    assert!(json.contains("\"D010\""));
}

// ===========================================================================
// Net-awareness tests
// ===========================================================================

#[test]
fn d001_same_net_traces_no_violation() {
    let mut board = make_clean_board();
    let net_a = Uuid::new_v4();
    let j1 = Uuid::new_v4();
    let j2 = Uuid::new_v4();
    let j3 = Uuid::new_v4();
    let j4 = Uuid::new_v4();

    // Two traces on the same net, overlapping/touching
    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(net_a),
        traces: vec![
            Trace {
                uuid: Uuid::new_v4(),
                layer: Layer::TopCopper,
                width: 0.5,
                from: TraceEndpoint::Junction { junction: j1 },
                to: TraceEndpoint::Junction { junction: j2 },
            },
            Trace {
                uuid: Uuid::new_v4(),
                layer: Layer::TopCopper,
                width: 0.5,
                from: TraceEndpoint::Junction { junction: j3 },
                to: TraceEndpoint::Junction { junction: j4 },
            },
        ],
        vias: vec![],
        junctions: vec![
            Junction {
                uuid: j1,
                position: Position::new(10.0, 10.0),
            },
            Junction {
                uuid: j2,
                position: Position::new(20.0, 10.0),
            },
            Junction {
                uuid: j3,
                position: Position::new(15.0, 10.0),
            },
            Junction {
                uuid: j4,
                position: Position::new(25.0, 10.0),
            },
        ],
        pads: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(
        !result.diagnostics.iter().any(|d| d.rule == "D001"),
        "Same-net traces should not produce D001: {:?}",
        result
            .diagnostics
            .iter()
            .filter(|d| d.rule == "D001")
            .collect::<Vec<_>>()
    );
}

#[test]
fn d001_different_net_traces_violation() {
    let mut board = make_clean_board();
    let net_a = Uuid::new_v4();
    let net_b = Uuid::new_v4();
    let j1 = Uuid::new_v4();
    let j2 = Uuid::new_v4();
    let j3 = Uuid::new_v4();
    let j4 = Uuid::new_v4();

    // Two traces on different nets, within min clearance
    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(net_a),
        traces: vec![Trace {
            uuid: Uuid::new_v4(),
            layer: Layer::TopCopper,
            width: 0.5,
            from: TraceEndpoint::Junction { junction: j1 },
            to: TraceEndpoint::Junction { junction: j2 },
        }],
        vias: vec![],
        junctions: vec![
            Junction {
                uuid: j1,
                position: Position::new(10.0, 10.0),
            },
            Junction {
                uuid: j2,
                position: Position::new(20.0, 10.0),
            },
        ],
        pads: vec![],
    });
    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(net_b),
        traces: vec![Trace {
            uuid: Uuid::new_v4(),
            layer: Layer::TopCopper,
            width: 0.5,
            from: TraceEndpoint::Junction { junction: j3 },
            to: TraceEndpoint::Junction { junction: j4 },
        }],
        vias: vec![],
        junctions: vec![
            Junction {
                uuid: j3,
                position: Position::new(10.0, 10.1),
            },
            Junction {
                uuid: j4,
                position: Position::new(20.0, 10.1),
            },
        ],
        pads: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(
        result.diagnostics.iter().any(|d| d.rule == "D001"),
        "Different-net traces within clearance should produce D001: distance is ~0.1 but min is 0.2; diagnostics: {:?}",
        result.diagnostics
    );
}

#[test]
fn d002_plane_copper_is_checked() {
    let mut board = make_clean_board();
    board.planes.push(Plane {
        uuid: Uuid::new_v4(),
        layer: Layer::TopCopper,
        net: Uuid::new_v4(),
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
                position: Position::new(0.05, 10.0),
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
                position: Position::new(0.05, 90.0),
                angle: Angle(0.0),
            },
        ],
        fragments: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(
        result.diagnostics.iter().any(|d| d.rule == "D002"),
        "Expected plane-vs-edge clearance violation, got: {:?}",
        result.diagnostics
    );
}

#[test]
fn d014_reports_split_nets_between_device_pads() {
    let (board, circuit, library, _net_uuid) = make_two_component_net_fixture();
    let result = run_drc(&board, &circuit, &library);
    assert!(
        result.diagnostics.iter().any(|d| d.rule == "D014"),
        "Expected D014 for disconnected board net, got: {:?}",
        result.diagnostics
    );
}

#[test]
fn d014_accepts_same_net_plane_connectivity() {
    let (mut board, circuit, library, net_uuid) = make_two_component_net_fixture();
    board.planes.push(Plane {
        uuid: Uuid::new_v4(),
        layer: Layer::TopCopper,
        net: net_uuid,
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
                position: Position::new(90.0, 30.0),
                angle: Angle(0.0),
            },
            Vertex {
                position: Position::new(10.0, 30.0),
                angle: Angle(0.0),
            },
        ],
        fragments: vec![],
    });

    let result = run_drc(&board, &circuit, &library);
    assert!(
        !result.diagnostics.iter().any(|d| d.rule == "D014"),
        "Same-net plane should connect both pads, got: {:?}",
        result.diagnostics
    );
}

#[test]
fn slot_width_checks_ignore_round_drills() {
    let mut board = make_clean_board();
    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(Uuid::new_v4()),
        traces: vec![],
        vias: vec![make_via(
            50.0,
            50.0,
            0.3,
            Layer::TopCopper,
            Layer::BottomCopper,
        )],
        junctions: vec![],
        pads: vec![],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(
        !result
            .diagnostics
            .iter()
            .any(|d| d.rule == "D017" || d.rule == "D018"),
        "Round drills should not trigger slot width checks: {:?}",
        result.diagnostics
    );
}

#[test]
fn slot_width_checks_apply_to_actual_slots() {
    let mut board = make_clean_board();
    board.holes.push(BoardHole {
        uuid: Uuid::new_v4(),
        diameter: 0.5,
        stop_mask: StopMaskConfig::Auto,
        lock: false,
        path: vec![
            Vertex {
                position: Position::new(50.0, 50.0),
                angle: Angle(0.0),
            },
            Vertex {
                position: Position::new(55.0, 50.0),
                angle: Angle(0.0),
            },
        ],
    });

    let result = run_drc(&board, &empty_circuit(), &empty_library());
    assert!(
        result.diagnostics.iter().any(|d| d.rule == "D017"),
        "Expected D017 for undersized NPTH slot, got: {:?}",
        result.diagnostics
    );
}

#[test]
fn d020_checks_differential_pair_length_delta() {
    let mut board = make_clean_board();
    let net_class_uuid = Uuid::new_v4();
    let pos_net = Uuid::new_v4();
    let neg_net = Uuid::new_v4();
    let pair_uuid = Uuid::new_v4();
    let pos_start = make_junction(10.0, 10.0);
    let pos_end = make_junction(40.0, 10.0);
    let neg_start = make_junction(10.0, 20.0);
    let neg_end = make_junction(24.0, 20.0);
    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(pos_net),
        traces: vec![make_trace(
            Layer::TopCopper,
            0.3,
            pos_start.uuid,
            pos_end.uuid,
        )],
        vias: vec![],
        junctions: vec![pos_start, pos_end],
        pads: vec![],
    });
    board.net_segments.push(BoardNetSegment {
        uuid: Uuid::new_v4(),
        net: Some(neg_net),
        traces: vec![make_trace(
            Layer::TopCopper,
            0.3,
            neg_start.uuid,
            neg_end.uuid,
        )],
        vias: vec![],
        junctions: vec![neg_start, neg_end],
        pads: vec![],
    });

    let circuit = Circuit {
        assembly_variants: vec![],
        net_classes: vec![NetClass {
            uuid: net_class_uuid,
            name: "diff".into(),
            default_trace_width: TraceWidthConfig::Inherit,
            default_via_drill_diameter: TraceWidthConfig::Inherit,
            min_copper_copper_clearance: 0.0,
            min_copper_width: 0.0,
            min_via_drill_diameter: 0.0,
            diff_pair_gap: Some(0.2),
            diff_pair_max_length_delta: Some(5.0),
        }],
        nets: vec![
            Net {
                uuid: pos_net,
                name: "USB_DP".into(),
                auto_name: false,
                net_class: net_class_uuid,
                scope: NetScope::Global,
                owner_sheet: None,
                is_power: false,
            },
            Net {
                uuid: neg_net,
                name: "USB_DN".into(),
                auto_name: false,
                net_class: net_class_uuid,
                scope: NetScope::Global,
                owner_sheet: None,
                is_power: false,
            },
        ],
        components: vec![],
        differential_pairs: vec![DifferentialPair {
            uuid: pair_uuid,
            name: "USB_D".into(),
            positive_net: pos_net,
            negative_net: neg_net,
            max_length_delta: None,
            target_impedance: None,
        }],
    };

    let result = run_drc(&board, &circuit, &empty_library());
    assert!(
        result.diagnostics.iter().any(|diag| diag.rule == "D020"),
        "Expected D020 for differential pair length mismatch, got: {:?}",
        result.diagnostics
    );
}

#[test]
fn d021_warns_when_impedance_target_has_no_gap_constraint() {
    let board = make_clean_board();
    let net_class_uuid = Uuid::new_v4();
    let pos_net = Uuid::new_v4();
    let neg_net = Uuid::new_v4();

    let circuit = Circuit {
        assembly_variants: vec![],
        net_classes: vec![NetClass {
            uuid: net_class_uuid,
            name: "diff".into(),
            default_trace_width: TraceWidthConfig::Inherit,
            default_via_drill_diameter: TraceWidthConfig::Inherit,
            min_copper_copper_clearance: 0.0,
            min_copper_width: 0.0,
            min_via_drill_diameter: 0.0,
            diff_pair_gap: None,
            diff_pair_max_length_delta: Some(5.0),
        }],
        nets: vec![
            Net {
                uuid: pos_net,
                name: "PAIR_P".into(),
                auto_name: false,
                net_class: net_class_uuid,
                scope: NetScope::Global,
                owner_sheet: None,
                is_power: false,
            },
            Net {
                uuid: neg_net,
                name: "PAIR_N".into(),
                auto_name: false,
                net_class: net_class_uuid,
                scope: NetScope::Global,
                owner_sheet: None,
                is_power: false,
            },
        ],
        components: vec![],
        differential_pairs: vec![DifferentialPair {
            uuid: Uuid::new_v4(),
            name: "PAIR".into(),
            positive_net: pos_net,
            negative_net: neg_net,
            max_length_delta: Some(1.0),
            target_impedance: Some(90.0),
        }],
    };

    let result = run_drc(&board, &circuit, &empty_library());
    let d021 = result.diagnostics.iter().find(|diag| diag.rule == "D021");
    assert!(
        d021.is_some(),
        "Expected D021 warning, got: {:?}",
        result.diagnostics
    );
    assert_eq!(d021.unwrap().severity, Severity::Warning);
}
