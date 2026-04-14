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

use std::collections::{HashMap, HashSet};

use serde::{Deserialize, Serialize};
use uuid::Uuid;

use volt_core::library::Component;
use volt_core::project::Circuit;

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
    let mut diagnostics = Vec::new();

    check_duplicate_designators(circuit, &mut diagnostics);
    check_unconnected_required_signals(circuit, library, &mut diagnostics);
    check_forced_net_conflicts(circuit, library, &mut diagnostics);
    check_empty_nets(circuit, &mut diagnostics);
    check_single_connection_nets(circuit, &mut diagnostics);
    check_unused_net_classes(circuit, &mut diagnostics);
    check_missing_device_assignment(circuit, &mut diagnostics);
    check_no_signal_connections(circuit, &mut diagnostics);

    let errors = diagnostics.iter().filter(|d| d.severity == Severity::Error).count();
    let warnings = diagnostics.iter().filter(|d| d.severity == Severity::Warning).count();

    ErcResult {
        passed: errors == 0,
        errors,
        warnings,
        diagnostics,
    }
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
    diags: &mut Vec<ErcDiagnostic>,
) {
    // Build net name lookup
    let net_names: HashMap<Uuid, &str> = circuit
        .nets
        .iter()
        .map(|n| (n.uuid, n.name.as_str()))
        .collect();

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
                    if let Some(&net_name) = net_names.get(&net_uuid) {
                        if net_name != signal.forced_net {
                            diags.push(ErcDiagnostic {
                                rule: "E003".into(),
                                severity: Severity::Error,
                                message: format!(
                                    "Signal '{}' on '{}' must connect to net '{}' but is connected to '{}'",
                                    signal.name, comp.name, signal.forced_net, net_name
                                ),
                                object: Some(comp.uuid),
                            });
                        }
                    }
                }
            }
        }
    }
}

/// W002: Nets with zero connections.
fn check_empty_nets(circuit: &Circuit, diags: &mut Vec<ErcDiagnostic>) {
    let connected_nets: HashSet<Uuid> = circuit
        .components
        .iter()
        .flat_map(|c| &c.signal_connections)
        .filter_map(|sc| sc.net)
        .collect();

    for net in &circuit.nets {
        if !connected_nets.contains(&net.uuid) {
            diags.push(ErcDiagnostic {
                rule: "W002".into(),
                severity: Severity::Warning,
                message: format!("Net '{}' has no connections", net.name),
                object: Some(net.uuid),
            });
        }
    }
}

/// W001: Nets with exactly one connection (likely dangling wire).
fn check_single_connection_nets(circuit: &Circuit, diags: &mut Vec<ErcDiagnostic>) {
    let mut net_conn_count: HashMap<Uuid, usize> = HashMap::new();
    for comp in &circuit.components {
        for sc in &comp.signal_connections {
            if let Some(net_uuid) = sc.net {
                *net_conn_count.entry(net_uuid).or_default() += 1;
            }
        }
    }

    for net in &circuit.nets {
        let count = net_conn_count.get(&net.uuid).copied().unwrap_or(0);
        if count == 1 {
            diags.push(ErcDiagnostic {
                rule: "W001".into(),
                severity: Severity::Warning,
                message: format!(
                    "Net '{}' has only 1 connection (likely dangling)",
                    net.name
                ),
                object: Some(net.uuid),
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

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests;
