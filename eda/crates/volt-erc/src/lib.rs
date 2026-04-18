//! Circuit-level Electrical Rule Checking (ERC).
//!
//! Validates a [`Circuit`] netlist and returns a list of [`ErcDiagnostic`]s.
//! Each diagnostic has a severity (Error, Warning) and a human-readable message.
//!
//! # Rules implemented
//!
//! | ID | Severity | Description |
//! |----|----------|-------------|
//! | E001 | Error | Unconnected required signal |
//! | E002 | Error | Duplicate component designator |
//! | E003 | Error | Forced net conflict |
//! | W001 | Warning | Single-connection net (likely dangling) |
//! | W002 | Warning | Empty net (no connections) |
//! | W003 | Warning | Unused net class |
//! | W004 | Warning | Missing device assignment |
//! | W005 | Warning | Component with no signal connections |
//! | E004 | Error | Broken hierarchical sheet interface binding |
//! | W006 | Warning | Orphan hierarchical port |
//! | W007 | Warning | Power net has no power flag or output-capable driver |

use std::collections::{HashMap, HashSet};

use serde::{Deserialize, Serialize};
use uuid::Uuid;

use volt_core::common::SignalRole;
use volt_core::library::Component;
use volt_core::project::{Circuit, HierarchicalPort, Schematic};

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Severity {
    Error,
    Warning,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ErcDiagnostic {
    /// Rule ID (e.g. "E001", "W001").
    pub rule: String,
    pub severity: Severity,
    pub message: String,
    /// UUID of the related object (component, net, etc.), if applicable.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub object: Option<Uuid>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ErcResult {
    pub diagnostics: Vec<ErcDiagnostic>,
    pub errors: usize,
    pub warnings: usize,
    pub passed: bool,
}

// ---------------------------------------------------------------------------
// Library resolver
// ---------------------------------------------------------------------------

/// Provides access to library components for ERC checks that need
/// signal metadata (required flag, forced_net, etc.).
pub trait LibraryResolver {
    fn get_component(&self, uuid: &Uuid) -> Option<&Component>;
}

/// Simple map-based resolver.
pub struct MapResolver {
    pub components: HashMap<Uuid, Component>,
}

impl LibraryResolver for MapResolver {
    fn get_component(&self, uuid: &Uuid) -> Option<&Component> {
        self.components.get(uuid)
    }
}

// ---------------------------------------------------------------------------
// ERC engine
// ---------------------------------------------------------------------------

/// Run all circuit-level ERC checks.
pub fn run_erc(circuit: &Circuit, library: &dyn LibraryResolver) -> ErcResult {
    run_erc_with_schematics(circuit, &[], library)
}

/// Run ERC with schematic context so hierarchy- and sheet-aware rules can
/// resolve local nets, sheet-pin bindings, and power flags.
pub fn run_erc_with_schematics(
    circuit: &Circuit,
    schematics: &[Schematic],
    library: &dyn LibraryResolver,
) -> ErcResult {
    let mut diagnostics = Vec::new();
    let hierarchy = build_net_hierarchy(circuit, schematics, &mut diagnostics);

    check_duplicate_designators(circuit, &mut diagnostics);
    check_unconnected_required_signals(circuit, library, &mut diagnostics);
    check_forced_net_conflicts(circuit, library, &hierarchy, &mut diagnostics);
    check_empty_nets(circuit, &hierarchy, &mut diagnostics);
    check_single_connection_nets(circuit, &hierarchy, &mut diagnostics);
    check_unused_net_classes(circuit, &mut diagnostics);
    check_missing_device_assignment(circuit, &mut diagnostics);
    check_no_signal_connections(circuit, &mut diagnostics);
    check_power_nets(circuit, library, &hierarchy, &mut diagnostics);

    let errors = diagnostics
        .iter()
        .filter(|d| d.severity == Severity::Error)
        .count();
    let warnings = diagnostics
        .iter()
        .filter(|d| d.severity == Severity::Warning)
        .count();

    ErcResult {
        passed: errors == 0,
        errors,
        warnings,
        diagnostics,
    }
}

#[derive(Debug, Default)]
struct NetUnionFind {
    parent: HashMap<Uuid, Uuid>,
}

impl NetUnionFind {
    fn new(circuit: &Circuit) -> Self {
        Self {
            parent: circuit
                .nets
                .iter()
                .map(|net| (net.uuid, net.uuid))
                .collect(),
        }
    }

    fn find(&mut self, net: Uuid) -> Option<Uuid> {
        let parent = *self.parent.get(&net)?;
        if parent == net {
            return Some(net);
        }
        let root = self.find(parent)?;
        self.parent.insert(net, root);
        Some(root)
    }

    fn union(&mut self, a: Uuid, b: Uuid) {
        let Some(root_a) = self.find(a) else {
            return;
        };
        let Some(root_b) = self.find(b) else {
            return;
        };
        if root_a != root_b {
            self.parent.insert(root_a, root_b);
        }
    }
}

#[derive(Debug, Default)]
struct NetHierarchy {
    group_for_net: HashMap<Uuid, Uuid>,
    group_nets: HashMap<Uuid, Vec<Uuid>>,
    group_names: HashMap<Uuid, Vec<String>>,
    power_flag_groups: HashSet<Uuid>,
    power_port_groups: HashSet<Uuid>,
}

impl NetHierarchy {
    fn root_for(&self, net: Uuid) -> Uuid {
        self.group_for_net.get(&net).copied().unwrap_or(net)
    }

    fn representative_net(&self, root: Uuid) -> Option<Uuid> {
        self.group_nets
            .get(&root)
            .and_then(|members| members.first())
            .copied()
    }

    fn display_name(&self, root: Uuid) -> String {
        self.group_names
            .get(&root)
            .filter(|names| !names.is_empty())
            .map(|names| names.join(" / "))
            .unwrap_or_else(|| root.to_string())
    }

    fn contains_name(&self, root: Uuid, name: &str) -> bool {
        self.group_names
            .get(&root)
            .is_some_and(|names| names.iter().any(|candidate| candidate == name))
    }
}

fn build_net_hierarchy(
    circuit: &Circuit,
    schematics: &[Schematic],
    diags: &mut Vec<ErcDiagnostic>,
) -> NetHierarchy {
    let known_nets: HashSet<Uuid> = circuit.nets.iter().map(|net| net.uuid).collect();
    let mut union_find = NetUnionFind::new(circuit);
    let schematic_by_name: HashMap<&str, &Schematic> = schematics
        .iter()
        .map(|schematic| (schematic.name.as_str(), schematic))
        .collect();
    let mut referenced_ports: HashSet<(String, Uuid)> = HashSet::new();
    let mut raw_power_flags = HashSet::new();
    let mut raw_power_ports = HashSet::new();

    for parent_schematic in schematics {
        for sheet_ref in &parent_schematic.sheet_refs {
            let Some(child_schematic) = schematic_by_name.get(sheet_ref.target_schematic.as_str())
            else {
                diags.push(ErcDiagnostic {
                    rule: "E004".into(),
                    severity: Severity::Error,
                    message: format!(
                        "Sheet reference '{}' targets missing schematic '{}'",
                        sheet_ref.name, sheet_ref.target_schematic
                    ),
                    object: Some(sheet_ref.uuid),
                });
                continue;
            };

            let child_ports: HashMap<Uuid, &HierarchicalPort> = child_schematic
                .hierarchical_ports
                .iter()
                .map(|port| (port.uuid, port))
                .collect();

            for pin in &sheet_ref.pins {
                let Some(port) = child_ports.get(&pin.port_ref) else {
                    diags.push(ErcDiagnostic {
                        rule: "E004".into(),
                        severity: Severity::Error,
                        message: format!(
                            "Sheet pin '{}' on '{}' references a missing child port",
                            pin.name, sheet_ref.name
                        ),
                        object: Some(pin.uuid),
                    });
                    continue;
                };

                referenced_ports.insert((child_schematic.name.clone(), port.uuid));

                let Some(parent_net) = pin.net else {
                    diags.push(ErcDiagnostic {
                        rule: "E004".into(),
                        severity: Severity::Error,
                        message: format!(
                            "Sheet pin '{}' on '{}' is not bound to any parent net",
                            pin.name, sheet_ref.name
                        ),
                        object: Some(pin.uuid),
                    });
                    continue;
                };

                if !known_nets.contains(&parent_net) {
                    diags.push(ErcDiagnostic {
                        rule: "E004".into(),
                        severity: Severity::Error,
                        message: format!(
                            "Sheet pin '{}' on '{}' references a parent net that does not exist",
                            pin.name, sheet_ref.name
                        ),
                        object: Some(pin.uuid),
                    });
                    continue;
                }

                if !known_nets.contains(&port.net) {
                    diags.push(ErcDiagnostic {
                        rule: "E004".into(),
                        severity: Severity::Error,
                        message: format!(
                            "Hierarchical port '{}' on '{}' references a child net that does not exist",
                            port.name, child_schematic.name
                        ),
                        object: Some(port.uuid),
                    });
                    continue;
                }

                union_find.union(parent_net, port.net);
            }

            for port in &child_schematic.hierarchical_ports {
                let count = sheet_ref
                    .pins
                    .iter()
                    .filter(|pin| pin.port_ref == port.uuid)
                    .count();
                if count == 0 {
                    diags.push(ErcDiagnostic {
                        rule: "E004".into(),
                        severity: Severity::Error,
                        message: format!(
                            "Sheet reference '{}' is missing a pin for child port '{}'",
                            sheet_ref.name, port.name
                        ),
                        object: Some(sheet_ref.uuid),
                    });
                } else if count > 1 {
                    diags.push(ErcDiagnostic {
                        rule: "E004".into(),
                        severity: Severity::Error,
                        message: format!(
                            "Sheet reference '{}' exposes child port '{}' more than once",
                            sheet_ref.name, port.name
                        ),
                        object: Some(sheet_ref.uuid),
                    });
                }
            }
        }
    }

    for schematic in schematics {
        for port in &schematic.hierarchical_ports {
            if !known_nets.contains(&port.net) {
                diags.push(ErcDiagnostic {
                    rule: "E004".into(),
                    severity: Severity::Error,
                    message: format!(
                        "Hierarchical port '{}' on '{}' references a net that does not exist",
                        port.name, schematic.name
                    ),
                    object: Some(port.uuid),
                });
            }
            if !referenced_ports.contains(&(schematic.name.clone(), port.uuid)) {
                diags.push(ErcDiagnostic {
                    rule: "W006".into(),
                    severity: Severity::Warning,
                    message: format!(
                        "Hierarchical port '{}' on '{}' is not exposed by any sheet reference",
                        port.name, schematic.name
                    ),
                    object: Some(port.uuid),
                });
            }
        }

        for power_flag in &schematic.power_flags {
            if known_nets.contains(&power_flag.net) {
                raw_power_flags.insert(power_flag.net);
            } else {
                diags.push(ErcDiagnostic {
                    rule: "E004".into(),
                    severity: Severity::Error,
                    message: format!(
                        "Power flag on '{}' references a net that does not exist",
                        schematic.name
                    ),
                    object: Some(power_flag.uuid),
                });
            }
        }

        for power_port in &schematic.power_ports {
            if known_nets.contains(&power_port.net) {
                raw_power_ports.insert(power_port.net);
            } else {
                diags.push(ErcDiagnostic {
                    rule: "E004".into(),
                    severity: Severity::Error,
                    message: format!(
                        "Power port on '{}' references a net that does not exist",
                        schematic.name
                    ),
                    object: Some(power_port.uuid),
                });
            }
        }
    }

    let mut group_for_net = HashMap::new();
    let mut group_nets: HashMap<Uuid, Vec<Uuid>> = HashMap::new();
    let mut group_names: HashMap<Uuid, Vec<String>> = HashMap::new();
    for net in &circuit.nets {
        let root = union_find.find(net.uuid).unwrap_or(net.uuid);
        group_for_net.insert(net.uuid, root);
        group_nets.entry(root).or_default().push(net.uuid);
        group_names.entry(root).or_default().push(net.name.clone());
    }

    for names in group_names.values_mut() {
        names.sort();
        names.dedup();
    }

    let power_flag_groups = raw_power_flags
        .into_iter()
        .map(|net| group_for_net.get(&net).copied().unwrap_or(net))
        .collect();
    let power_port_groups = raw_power_ports
        .into_iter()
        .map(|net| group_for_net.get(&net).copied().unwrap_or(net))
        .collect();

    NetHierarchy {
        group_for_net,
        group_nets,
        group_names,
        power_flag_groups,
        power_port_groups,
    }
}

fn group_connection_counts(circuit: &Circuit, hierarchy: &NetHierarchy) -> HashMap<Uuid, usize> {
    let mut counts = HashMap::new();
    for comp in &circuit.components {
        for connection in &comp.signal_connections {
            if let Some(net) = connection.net {
                *counts.entry(hierarchy.root_for(net)).or_default() += 1;
            }
        }
    }
    counts
}

// ---------------------------------------------------------------------------
// Individual checks
// ---------------------------------------------------------------------------

/// E002: Duplicate component designators.
fn check_duplicate_designators(circuit: &Circuit, diags: &mut Vec<ErcDiagnostic>) {
    let mut seen: HashMap<&str, Uuid> = HashMap::new();
    for comp in &circuit.components {
        if let Some(&first_uuid) = seen.get(comp.name.as_str()) {
            diags.push(ErcDiagnostic {
                rule: "E002".into(),
                severity: Severity::Error,
                message: format!(
                    "Duplicate component designator '{}' (instances {} and {})",
                    comp.name, first_uuid, comp.uuid
                ),
                object: Some(comp.uuid),
            });
        } else {
            seen.insert(&comp.name, comp.uuid);
        }
    }
}

/// E001: Unconnected required signals.
fn check_unconnected_required_signals(
    circuit: &Circuit,
    library: &dyn LibraryResolver,
    diags: &mut Vec<ErcDiagnostic>,
) {
    for comp in &circuit.components {
        let Some(lib_comp) = library.get_component(&comp.lib_component) else {
            continue;
        };

        for signal in &lib_comp.signals {
            if !signal.required {
                continue;
            }

            let is_connected = comp
                .signal_connections
                .iter()
                .any(|sc| sc.signal == signal.uuid && sc.net.is_some());

            if !is_connected {
                diags.push(ErcDiagnostic {
                    rule: "E001".into(),
                    severity: Severity::Error,
                    message: format!(
                        "Required signal '{}' on component '{}' is not connected to any net",
                        signal.name, comp.name
                    ),
                    object: Some(comp.uuid),
                });
            }
        }
    }
}

/// E003: Forced net conflicts — signal has forced_net but is connected to a different net.
fn check_forced_net_conflicts(
    circuit: &Circuit,
    library: &dyn LibraryResolver,
    hierarchy: &NetHierarchy,
    diags: &mut Vec<ErcDiagnostic>,
) {
    for comp in &circuit.components {
        let Some(lib_comp) = library.get_component(&comp.lib_component) else {
            continue;
        };

        for signal in &lib_comp.signals {
            if signal.forced_net.is_empty() {
                continue;
            }

            for conn in &comp.signal_connections {
                if conn.signal != signal.uuid {
                    continue;
                }
                if let Some(net_uuid) = conn.net {
                    let root = hierarchy.root_for(net_uuid);
                    if !hierarchy.contains_name(root, &signal.forced_net) {
                        diags.push(ErcDiagnostic {
                            rule: "E003".into(),
                            severity: Severity::Error,
                            message: format!(
                                "Signal '{}' on '{}' must connect to net '{}' but is connected to '{}'",
                                signal.name,
                                comp.name,
                                signal.forced_net,
                                hierarchy.display_name(root)
                            ),
                            object: Some(comp.uuid),
                        });
                    }
                }
            }
        }
    }
}

/// W002: Net groups with zero connections.
fn check_empty_nets(circuit: &Circuit, hierarchy: &NetHierarchy, diags: &mut Vec<ErcDiagnostic>) {
    let connection_counts = group_connection_counts(circuit, hierarchy);

    for root in hierarchy.group_nets.keys().copied() {
        if connection_counts.get(&root).copied().unwrap_or(0) == 0 {
            diags.push(ErcDiagnostic {
                rule: "W002".into(),
                severity: Severity::Warning,
                message: format!(
                    "Net group '{}' has no connections",
                    hierarchy.display_name(root)
                ),
                object: hierarchy.representative_net(root),
            });
        }
    }
}

/// W001: Net groups with exactly one connection (likely dangling wire).
fn check_single_connection_nets(
    circuit: &Circuit,
    hierarchy: &NetHierarchy,
    diags: &mut Vec<ErcDiagnostic>,
) {
    let connection_counts = group_connection_counts(circuit, hierarchy);

    for root in hierarchy.group_nets.keys().copied() {
        if connection_counts.get(&root).copied().unwrap_or(0) == 1 {
            diags.push(ErcDiagnostic {
                rule: "W001".into(),
                severity: Severity::Warning,
                message: format!(
                    "Net group '{}' has only 1 connection (likely dangling)",
                    hierarchy.display_name(root)
                ),
                object: hierarchy.representative_net(root),
            });
        }
    }
}

/// W003: Net classes not referenced by any net.
fn check_unused_net_classes(circuit: &Circuit, diags: &mut Vec<ErcDiagnostic>) {
    let used: HashSet<Uuid> = circuit.nets.iter().map(|n| n.net_class).collect();

    for nc in &circuit.net_classes {
        if !used.contains(&nc.uuid) {
            diags.push(ErcDiagnostic {
                rule: "W003".into(),
                severity: Severity::Warning,
                message: format!("Net class '{}' is not used by any net", nc.name),
                object: Some(nc.uuid),
            });
        }
    }
}

/// W004: Component instances with no device assignment.
fn check_missing_device_assignment(circuit: &Circuit, diags: &mut Vec<ErcDiagnostic>) {
    for comp in &circuit.components {
        if comp.device_assignments.is_empty() {
            diags.push(ErcDiagnostic {
                rule: "W004".into(),
                severity: Severity::Warning,
                message: format!(
                    "Component '{}' has no device assigned (cannot be placed on board)",
                    comp.name
                ),
                object: Some(comp.uuid),
            });
        }
    }
}

/// W005: Component instances with no signal connections at all.
fn check_no_signal_connections(circuit: &Circuit, diags: &mut Vec<ErcDiagnostic>) {
    for comp in &circuit.components {
        let any_connected = comp.signal_connections.iter().any(|sc| sc.net.is_some());
        if !any_connected && !comp.signal_connections.is_empty() {
            diags.push(ErcDiagnostic {
                rule: "W005".into(),
                severity: Severity::Warning,
                message: format!(
                    "Component '{}' has no signals connected to any net",
                    comp.name
                ),
                object: Some(comp.uuid),
            });
        }
    }
}

fn check_power_nets(
    circuit: &Circuit,
    library: &dyn LibraryResolver,
    hierarchy: &NetHierarchy,
    diags: &mut Vec<ErcDiagnostic>,
) {
    let connection_counts = group_connection_counts(circuit, hierarchy);
    let mut power_groups = hierarchy.power_port_groups.clone();
    let mut driver_groups = hierarchy.power_flag_groups.clone();

    for net in &circuit.nets {
        if net.is_power {
            power_groups.insert(hierarchy.root_for(net.uuid));
        }
    }

    for comp in &circuit.components {
        let Some(lib_comp) = library.get_component(&comp.lib_component) else {
            continue;
        };
        let roles: HashMap<Uuid, SignalRole> = lib_comp
            .signals
            .iter()
            .map(|signal| (signal.uuid, signal.role))
            .collect();

        for connection in &comp.signal_connections {
            let Some(net) = connection.net else {
                continue;
            };
            let Some(role) = roles.get(&connection.signal).copied() else {
                continue;
            };
            let root = hierarchy.root_for(net);
            if role == SignalRole::Power {
                power_groups.insert(root);
            }
            if matches!(
                role,
                SignalRole::Output
                    | SignalRole::Bidirectional
                    | SignalRole::OpenDrain
                    | SignalRole::OpenCollector
            ) {
                driver_groups.insert(root);
            }
        }
    }

    for root in power_groups {
        if connection_counts.get(&root).copied().unwrap_or(0) == 0
            && !hierarchy.power_flag_groups.contains(&root)
        {
            continue;
        }

        if !driver_groups.contains(&root) {
            diags.push(ErcDiagnostic {
                rule: "W007".into(),
                severity: Severity::Warning,
                message: format!(
                    "Power net group '{}' has no power flag or output-capable driver",
                    hierarchy.display_name(root)
                ),
                object: hierarchy.representative_net(root),
            });
        }
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests;
