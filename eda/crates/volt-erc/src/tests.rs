use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::*;
use volt_core::project::*;

use crate::*;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

fn make_lib_component(uuid: Uuid, signals: Vec<(&str, bool)>) -> Component {
    Component {
        meta: LibraryMeta {
            uuid,
            name: "Test".into(),
            description: String::new(),
            keywords: String::new(),
            author: String::new(),
            version: "0.1".into(),
            created: chrono::Utc::now(),
            deprecated: false,
            category: None,
        },
        prefix: "U".into(),
        default_value: String::new(),
        schematic_only: false,
        attributes: vec![],
        signals: signals
            .into_iter()
            .map(|(name, required)| Signal {
                uuid: Uuid::new_v4(),
                name: name.into(),
                role: SignalRole::Passive,
                required,
                negated: false,
                clock: false,
                forced_net: String::new(),
            })
            .collect(),
        variants: vec![],
    }
}

fn make_lib_component_with_forced(uuid: Uuid, signals: Vec<(&str, bool, &str)>) -> Component {
    Component {
        meta: LibraryMeta {
            uuid,
            name: "Test".into(),
            description: String::new(),
            keywords: String::new(),
            author: String::new(),
            version: "0.1".into(),
            created: chrono::Utc::now(),
            deprecated: false,
            category: None,
        },
        prefix: "U".into(),
        default_value: String::new(),
        schematic_only: false,
        attributes: vec![],
        signals: signals
            .into_iter()
            .map(|(name, required, forced)| Signal {
                uuid: Uuid::new_v4(),
                name: name.into(),
                role: SignalRole::Passive,
                required,
                negated: false,
                clock: false,
                forced_net: forced.into(),
            })
            .collect(),
        variants: vec![],
    }
}

fn default_net_class_uuid() -> Uuid {
    Uuid::nil()
}

fn make_circuit_base() -> Circuit {
    Circuit {
        assembly_variants: vec![AssemblyVariant {
            uuid: Uuid::new_v4(),
            name: "Std".into(),
            description: String::new(),
        }],
        net_classes: vec![NetClass {
            uuid: default_net_class_uuid(),
            name: "default".into(),
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
    }
}

fn resolver_from(components: Vec<Component>) -> MapResolver {
    MapResolver {
        components: components.into_iter().map(|c| (c.meta.uuid, c)).collect(),
    }
}

fn make_schematic(name: &str) -> Schematic {
    Schematic {
        uuid: Uuid::new_v4(),
        name: name.into(),
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
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[test]
fn clean_circuit_passes() {
    let comp_uuid = Uuid::new_v4();
    let lib = make_lib_component(comp_uuid, vec![("1", true), ("2", true)]);

    let net1 = Uuid::new_v4();
    let net2 = Uuid::new_v4();

    let mut circuit = make_circuit_base();
    circuit.nets.push(Net {
        uuid: net1,
        name: "VCC".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: false,
    });
    circuit.nets.push(Net {
        uuid: net2,
        name: "GND".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: false,
    });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "R1".into(),
        value: "10k".into(),
        lock_assembly: false,
        device_assignments: vec![DeviceAssignment {
            device: Uuid::new_v4(),
            variant: Uuid::new_v4(),
            part: DevicePartRef::default(),
        }],
        signal_connections: vec![
            SignalConnection {
                signal: lib.signals[0].uuid,
                net: Some(net1),
            },
            SignalConnection {
                signal: lib.signals[1].uuid,
                net: Some(net2),
            },
        ],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    assert!(
        result.passed,
        "Expected clean pass, got: {:?}",
        result.diagnostics
    );
    assert_eq!(result.errors, 0);
}

#[test]
fn e001_unconnected_required_signal() {
    let comp_uuid = Uuid::new_v4();
    let lib = make_lib_component(comp_uuid, vec![("1", true), ("2", true)]);

    let net1 = Uuid::new_v4();
    let mut circuit = make_circuit_base();
    circuit.nets.push(Net {
        uuid: net1,
        name: "VCC".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: false,
    });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "R1".into(),
        value: "10k".into(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![
            SignalConnection {
                signal: lib.signals[0].uuid,
                net: Some(net1),
            },
            SignalConnection {
                signal: lib.signals[1].uuid,
                net: None,
            }, // pin 2 unconnected!
        ],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    assert!(!result.passed);
    assert!(result.diagnostics.iter().any(|d| d.rule == "E001"));
}

#[test]
fn e001_optional_signal_ok() {
    let comp_uuid = Uuid::new_v4();
    let lib = make_lib_component(comp_uuid, vec![("1", true), ("2", false)]); // pin 2 optional

    let net1 = Uuid::new_v4();
    let mut circuit = make_circuit_base();
    circuit.nets.push(Net {
        uuid: net1,
        name: "VCC".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: false,
    });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "R1".into(),
        value: "10k".into(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![
            SignalConnection {
                signal: lib.signals[0].uuid,
                net: Some(net1),
            },
            SignalConnection {
                signal: lib.signals[1].uuid,
                net: None,
            }, // optional, ok
        ],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    // Should not have E001 (optional signal)
    assert!(!result.diagnostics.iter().any(|d| d.rule == "E001"));
}

#[test]
fn e002_duplicate_designators() {
    let comp_uuid = Uuid::new_v4();
    let lib = make_lib_component(comp_uuid, vec![("1", false)]);

    let mut circuit = make_circuit_base();
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "R1".into(),
        value: "10k".into(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![],
    });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "R1".into(), // duplicate!
        value: "4.7k".into(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    assert!(!result.passed);
    assert!(result.diagnostics.iter().any(|d| d.rule == "E002"));
}

#[test]
fn e003_forced_net_conflict() {
    let comp_uuid = Uuid::new_v4();
    let lib = make_lib_component_with_forced(comp_uuid, vec![("GND", true, "GND")]);

    let wrong_net = Uuid::new_v4();
    let mut circuit = make_circuit_base();
    circuit.nets.push(Net {
        uuid: wrong_net,
        name: "VCC".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: false,
    });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "SUP1".into(),
        value: "GND".into(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![
            SignalConnection {
                signal: lib.signals[0].uuid,
                net: Some(wrong_net),
            }, // connected to VCC, not GND!
        ],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    assert!(!result.passed);
    let conflict = result
        .diagnostics
        .iter()
        .find(|d| d.rule == "E003")
        .unwrap();
    assert!(conflict.message.contains("VCC"));
    assert!(conflict.message.contains("GND"));
}

#[test]
fn w001_single_connection_net() {
    let comp_uuid = Uuid::new_v4();
    let lib = make_lib_component(comp_uuid, vec![("1", false)]);

    let net1 = Uuid::new_v4();
    let mut circuit = make_circuit_base();
    circuit.nets.push(Net {
        uuid: net1,
        name: "LONELY".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: false,
    });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "R1".into(),
        value: String::new(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![SignalConnection {
            signal: lib.signals[0].uuid,
            net: Some(net1),
        }],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    assert!(
        result
            .diagnostics
            .iter()
            .any(|d| d.rule == "W001" && d.message.contains("LONELY"))
    );
}

#[test]
fn w002_empty_net() {
    let mut circuit = make_circuit_base();
    circuit.nets.push(Net {
        uuid: Uuid::new_v4(),
        name: "UNUSED".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: false,
    });

    let result = run_erc(&circuit, &resolver_from(vec![]));
    assert!(
        result
            .diagnostics
            .iter()
            .any(|d| d.rule == "W002" && d.message.contains("UNUSED"))
    );
}

#[test]
fn w003_unused_net_class() {
    let mut circuit = make_circuit_base();
    // Add a second net class that nothing uses
    circuit.net_classes.push(NetClass {
        uuid: Uuid::new_v4(),
        name: "highspeed".into(),
        default_trace_width: TraceWidthConfig::Value(0.3),
        default_via_drill_diameter: TraceWidthConfig::Inherit,
        min_copper_copper_clearance: 0.0,
        min_copper_width: 0.0,
        min_via_drill_diameter: 0.0,
        diff_pair_gap: None,
        diff_pair_max_length_delta: None,
    });

    let result = run_erc(&circuit, &resolver_from(vec![]));
    // Both net classes are unused (no nets at all), but the default one is always there
    assert!(result.diagnostics.iter().any(|d| d.rule == "W003"));
}

#[test]
fn w004_missing_device_assignment() {
    let comp_uuid = Uuid::new_v4();
    let lib = make_lib_component(comp_uuid, vec![("1", false)]);

    let mut circuit = make_circuit_base();
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "R1".into(),
        value: String::new(),
        lock_assembly: false,
        device_assignments: vec![], // no device!
        signal_connections: vec![],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    assert!(
        result
            .diagnostics
            .iter()
            .any(|d| d.rule == "W004" && d.message.contains("R1"))
    );
}

#[test]
fn w005_no_signal_connections() {
    let comp_uuid = Uuid::new_v4();
    let lib = make_lib_component(comp_uuid, vec![("1", false), ("2", false)]);

    let mut circuit = make_circuit_base();
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "R1".into(),
        value: String::new(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![
            SignalConnection {
                signal: lib.signals[0].uuid,
                net: None,
            },
            SignalConnection {
                signal: lib.signals[1].uuid,
                net: None,
            },
        ],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    assert!(
        result
            .diagnostics
            .iter()
            .any(|d| d.rule == "W005" && d.message.contains("R1"))
    );
}

#[test]
fn erc_result_is_json_serializable() {
    let result = ErcResult {
        passed: true,
        errors: 0,
        warnings: 1,
        diagnostics: vec![ErcDiagnostic {
            rule: "W001".into(),
            severity: Severity::Warning,
            message: "test warning".into(),
            object: None,
        }],
    };
    let json = serde_json::to_string_pretty(&result).unwrap();
    assert!(json.contains("\"passed\": true"));
    assert!(json.contains("\"W001\""));
}

#[test]
fn hierarchy_bindings_prevent_false_single_connection_warnings() {
    let comp_uuid = Uuid::new_v4();
    let lib = make_lib_component(comp_uuid, vec![("1", false)]);
    let parent_net = Uuid::new_v4();
    let child_net = Uuid::new_v4();
    let port_uuid = Uuid::new_v4();

    let mut circuit = make_circuit_base();
    circuit.nets.push(Net {
        uuid: parent_net,
        name: "IO".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: false,
    });
    circuit.nets.push(Net {
        uuid: child_net,
        name: "IO".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Local,
        owner_sheet: None,
        is_power: false,
    });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "U_PARENT".into(),
        value: String::new(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![SignalConnection {
            signal: lib.signals[0].uuid,
            net: Some(parent_net),
        }],
    });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "U_CHILD".into(),
        value: String::new(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![SignalConnection {
            signal: lib.signals[0].uuid,
            net: Some(child_net),
        }],
    });

    let mut child = make_schematic("child");
    child.hierarchical_ports.push(HierarchicalPort {
        uuid: port_uuid,
        name: "IO".into(),
        position: Position::new(0.0, 0.0),
        side: SheetSide::Left,
        net: child_net,
    });

    let mut parent = make_schematic("main");
    parent.sheet_refs.push(SheetRef {
        uuid: Uuid::new_v4(),
        name: "U_CHILD".into(),
        target_schematic: "child".into(),
        position: Position::new(0.0, 0.0),
        width: 20.0,
        height: 15.0,
        pins: vec![SheetRefPin {
            uuid: Uuid::new_v4(),
            name: "IO".into(),
            port_ref: port_uuid,
            side: SheetSide::Left,
            offset: 2.54,
            net: Some(parent_net),
        }],
    });

    let result = run_erc_with_schematics(&circuit, &[parent, child], &resolver_from(vec![lib]));
    assert!(
        !result.diagnostics.iter().any(|diag| diag.rule == "W001"),
        "Hierarchy binding should suppress false dangling-net warnings: {:?}",
        result.diagnostics
    );
}

#[test]
fn hierarchy_reports_unbound_sheet_pins() {
    let mut circuit = make_circuit_base();
    let parent_net = Uuid::new_v4();
    let child_net = Uuid::new_v4();
    let port_uuid = Uuid::new_v4();
    circuit.nets.push(Net {
        uuid: parent_net,
        name: "IO".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: false,
    });
    circuit.nets.push(Net {
        uuid: child_net,
        name: "IO_LOCAL".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Local,
        owner_sheet: None,
        is_power: false,
    });

    let mut child = make_schematic("child");
    child.hierarchical_ports.push(HierarchicalPort {
        uuid: port_uuid,
        name: "IO".into(),
        position: Position::new(0.0, 0.0),
        side: SheetSide::Left,
        net: child_net,
    });

    let mut parent = make_schematic("main");
    parent.sheet_refs.push(SheetRef {
        uuid: Uuid::new_v4(),
        name: "U_CHILD".into(),
        target_schematic: "child".into(),
        position: Position::new(0.0, 0.0),
        width: 20.0,
        height: 15.0,
        pins: vec![SheetRefPin {
            uuid: Uuid::new_v4(),
            name: "IO".into(),
            port_ref: port_uuid,
            side: SheetSide::Left,
            offset: 2.54,
            net: None,
        }],
    });

    let result = run_erc_with_schematics(&circuit, &[parent, child], &resolver_from(vec![]));
    assert!(
        result.diagnostics.iter().any(|diag| diag.rule == "E004"),
        "Expected E004 for unbound sheet pin, got: {:?}",
        result.diagnostics
    );
}

#[test]
fn hierarchy_reports_orphan_ports() {
    let mut circuit = make_circuit_base();
    let child_net = Uuid::new_v4();
    circuit.nets.push(Net {
        uuid: child_net,
        name: "IO_LOCAL".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Local,
        owner_sheet: None,
        is_power: false,
    });

    let mut child = make_schematic("child");
    child.hierarchical_ports.push(HierarchicalPort {
        uuid: Uuid::new_v4(),
        name: "IO".into(),
        position: Position::new(0.0, 0.0),
        side: SheetSide::Left,
        net: child_net,
    });

    let result = run_erc_with_schematics(&circuit, &[child], &resolver_from(vec![]));
    assert!(
        result.diagnostics.iter().any(|diag| diag.rule == "W006"),
        "Expected W006 for orphan hierarchical port, got: {:?}",
        result.diagnostics
    );
}

#[test]
fn power_flags_count_as_driver_evidence() {
    let comp_uuid = Uuid::new_v4();
    let mut lib = make_lib_component(comp_uuid, vec![("VCC", false)]);
    lib.signals[0].role = SignalRole::Power;

    let power_net = Uuid::new_v4();
    let mut circuit = make_circuit_base();
    circuit.nets.push(Net {
        uuid: power_net,
        name: "VCC".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: true,
    });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "U1".into(),
        value: String::new(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![SignalConnection {
            signal: lib.signals[0].uuid,
            net: Some(power_net),
        }],
    });

    let mut main = make_schematic("main");
    main.power_flags.push(PowerFlag {
        uuid: Uuid::new_v4(),
        net: power_net,
        position: Position::new(0.0, 0.0),
    });

    let result = run_erc_with_schematics(&circuit, &[main], &resolver_from(vec![lib]));
    assert!(
        !result.diagnostics.iter().any(|diag| diag.rule == "W007"),
        "Power flag should count as source evidence: {:?}",
        result.diagnostics
    );
}

#[test]
fn power_nets_without_driver_or_flag_warn() {
    let comp_uuid = Uuid::new_v4();
    let mut lib = make_lib_component(comp_uuid, vec![("VCC", false)]);
    lib.signals[0].role = SignalRole::Power;

    let power_net = Uuid::new_v4();
    let mut circuit = make_circuit_base();
    circuit.nets.push(Net {
        uuid: power_net,
        name: "VCC".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
        scope: NetScope::Global,
        owner_sheet: None,
        is_power: true,
    });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "U1".into(),
        value: String::new(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![SignalConnection {
            signal: lib.signals[0].uuid,
            net: Some(power_net),
        }],
    });

    let result = run_erc_with_schematics(
        &circuit,
        &[make_schematic("main")],
        &resolver_from(vec![lib]),
    );
    assert!(
        result.diagnostics.iter().any(|diag| diag.rule == "W007"),
        "Expected W007 for undriven power net, got: {:?}",
        result.diagnostics
    );
}
