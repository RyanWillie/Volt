use std::collections::HashMap;

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

fn make_lib_component_with_forced(
    uuid: Uuid,
    signals: Vec<(&str, bool, &str)>,
) -> Component {
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
        }],
        nets: vec![],
        components: vec![],
    }
}

fn resolver_from(components: Vec<Component>) -> MapResolver {
    MapResolver {
        components: components
            .into_iter()
            .map(|c| (c.meta.uuid, c))
            .collect(),
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
    circuit.nets.push(Net { uuid: net1, name: "VCC".into(), auto_name: false, net_class: default_net_class_uuid() });
    circuit.nets.push(Net { uuid: net2, name: "GND".into(), auto_name: false, net_class: default_net_class_uuid() });
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
            SignalConnection { signal: lib.signals[0].uuid, net: Some(net1) },
            SignalConnection { signal: lib.signals[1].uuid, net: Some(net2) },
        ],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    assert!(result.passed, "Expected clean pass, got: {:?}", result.diagnostics);
    assert_eq!(result.errors, 0);
}

#[test]
fn e001_unconnected_required_signal() {
    let comp_uuid = Uuid::new_v4();
    let lib = make_lib_component(comp_uuid, vec![("1", true), ("2", true)]);

    let net1 = Uuid::new_v4();
    let mut circuit = make_circuit_base();
    circuit.nets.push(Net { uuid: net1, name: "VCC".into(), auto_name: false, net_class: default_net_class_uuid() });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "R1".into(),
        value: "10k".into(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![
            SignalConnection { signal: lib.signals[0].uuid, net: Some(net1) },
            SignalConnection { signal: lib.signals[1].uuid, net: None }, // pin 2 unconnected!
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
    circuit.nets.push(Net { uuid: net1, name: "VCC".into(), auto_name: false, net_class: default_net_class_uuid() });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "R1".into(),
        value: "10k".into(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![
            SignalConnection { signal: lib.signals[0].uuid, net: Some(net1) },
            SignalConnection { signal: lib.signals[1].uuid, net: None }, // optional, ok
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
    circuit.nets.push(Net { uuid: wrong_net, name: "VCC".into(), auto_name: false, net_class: default_net_class_uuid() });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "SUP1".into(),
        value: "GND".into(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![
            SignalConnection { signal: lib.signals[0].uuid, net: Some(wrong_net) }, // connected to VCC, not GND!
        ],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    assert!(!result.passed);
    let conflict = result.diagnostics.iter().find(|d| d.rule == "E003").unwrap();
    assert!(conflict.message.contains("VCC"));
    assert!(conflict.message.contains("GND"));
}

#[test]
fn w001_single_connection_net() {
    let comp_uuid = Uuid::new_v4();
    let lib = make_lib_component(comp_uuid, vec![("1", false)]);

    let net1 = Uuid::new_v4();
    let mut circuit = make_circuit_base();
    circuit.nets.push(Net { uuid: net1, name: "LONELY".into(), auto_name: false, net_class: default_net_class_uuid() });
    circuit.components.push(ComponentInstance {
        uuid: Uuid::new_v4(),
        lib_component: comp_uuid,
        lib_variant: Uuid::new_v4(),
        name: "R1".into(),
        value: String::new(),
        lock_assembly: false,
        device_assignments: vec![],
        signal_connections: vec![
            SignalConnection { signal: lib.signals[0].uuid, net: Some(net1) },
        ],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    assert!(result.diagnostics.iter().any(|d| d.rule == "W001" && d.message.contains("LONELY")));
}

#[test]
fn w002_empty_net() {
    let mut circuit = make_circuit_base();
    circuit.nets.push(Net {
        uuid: Uuid::new_v4(),
        name: "UNUSED".into(),
        auto_name: false,
        net_class: default_net_class_uuid(),
    });

    let result = run_erc(&circuit, &resolver_from(vec![]));
    assert!(result.diagnostics.iter().any(|d| d.rule == "W002" && d.message.contains("UNUSED")));
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
    assert!(result.diagnostics.iter().any(|d| d.rule == "W004" && d.message.contains("R1")));
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
            SignalConnection { signal: lib.signals[0].uuid, net: None },
            SignalConnection { signal: lib.signals[1].uuid, net: None },
        ],
    });

    let result = run_erc(&circuit, &resolver_from(vec![lib]));
    assert!(result.diagnostics.iter().any(|d| d.rule == "W005" && d.message.contains("R1")));
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
