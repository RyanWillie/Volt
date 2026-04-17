//! Phase 1 — Analysis
//!
//! Classifies nets and components, builds a directed signal-flow graph, and
//! detects companion components (bypass caps, pull-ups/pull-downs) that should
//! be placed close to their parent ICs.

use std::collections::{HashMap, HashSet};
use uuid::Uuid;

use volt_core::common::SignalRole;
use volt_core::library::Component;
use volt_core::library::Symbol;
use volt_core::project::{Circuit, ComponentInstance};

use super::types::*;

// ===================================================================
// 1. Net membership
// ===================================================================

/// For each net, collect the unique component instances that have at least one
/// signal connected to it.
///
/// Returns an entry for every net in the circuit, even if no component connects
/// to it (the value will be an empty `Vec`).
pub fn build_net_members(circuit: &Circuit) -> HashMap<Uuid, Vec<Uuid>> {
    let mut per_net: HashMap<Uuid, HashSet<Uuid>> = HashMap::new();

    // Seed every net so callers always get an entry.
    for net in &circuit.nets {
        per_net.entry(net.uuid).or_default();
    }

    for comp in &circuit.components {
        for sc in &comp.signal_connections {
            if let Some(nid) = sc.net {
                per_net.entry(nid).or_default().insert(comp.uuid);
            }
        }
    }

    per_net
        .into_iter()
        .map(|(nid, set)| (nid, set.into_iter().collect()))
        .collect()
}

// ===================================================================
// 2. Net classification
// ===================================================================

/// Classify every net as [`NetClass::Power`], [`NetClass::Signal`], or
/// [`NetClass::HighFanout`].
///
/// * **Power** — name matches a known power-rail pattern (VCC, GND, 3V3, …).
/// * **HighFanout** — signal net with more than [`HIGH_FANOUT_THRESHOLD`]
///   component connections.
/// * **Signal** — everything else.
pub fn classify_nets(
    circuit: &Circuit,
    net_members: &HashMap<Uuid, Vec<Uuid>>,
) -> HashMap<Uuid, NetClass> {
    let mut out = HashMap::with_capacity(circuit.nets.len());

    for net in &circuit.nets {
        let cls = if is_power_net_name(&net.name) {
            NetClass::Power
        } else {
            let n = net_members.get(&net.uuid).map_or(0, Vec::len);
            if n > HIGH_FANOUT_THRESHOLD {
                NetClass::HighFanout
            } else {
                NetClass::Signal
            }
        };
        out.insert(net.uuid, cls);
    }

    out
}

// ===================================================================
// 3. Component classification
// ===================================================================

/// Classify every component instance by its circuit role.
///
/// The function examines each component's library signals and the nets they
/// connect to in order to distinguish Sources, Sinks, Processors, and the
/// various passive roles.  When library data is unavailable or inconclusive
/// the designator prefix is used as a fallback (U → Processor, R/C/L →
/// SeriesPassive, J → Source, D → Sink).
pub fn classify_components(
    circuit: &Circuit,
    net_classes: &HashMap<Uuid, NetClass>,
    net_members: &HashMap<Uuid, Vec<Uuid>>,
    lib_comps: &HashMap<Uuid, Component>,
    _lib_syms: &HashMap<Uuid, Symbol>,
) -> HashMap<Uuid, ComponentRole> {
    let mut out = HashMap::with_capacity(circuit.components.len());

    for inst in &circuit.components {
        out.insert(
            inst.uuid,
            classify_one(inst, net_classes, net_members, lib_comps),
        );
    }

    out
}

// -------------------------------------------------------------------
// Internal helpers for component classification
// -------------------------------------------------------------------

/// Aggregated information about one connected pin of a component.
struct PinInfo {
    _signal: Uuid,
    role: SignalRole,
    net: Uuid,
    net_class: NetClass,
}

/// Classify a single component instance.
fn classify_one(
    inst: &ComponentInstance,
    net_classes: &HashMap<Uuid, NetClass>,
    net_members: &HashMap<Uuid, Vec<Uuid>>,
    lib_comps: &HashMap<Uuid, Component>,
) -> ComponentRole {
    let lib = match lib_comps.get(&inst.lib_component) {
        Some(c) => c,
        None => return role_from_designator(&inst.name),
    };

    // Map signal uuid → role from the library definition.
    let sig_role: HashMap<Uuid, SignalRole> =
        lib.signals.iter().map(|s| (s.uuid, s.role)).collect();

    // Build connected-pin info for every signal that has a net.
    let pins: Vec<PinInfo> = inst
        .signal_connections
        .iter()
        .filter_map(|sc| {
            let nid = sc.net?;
            let role = *sig_role.get(&sc.signal)?;
            let nc = net_classes.get(&nid).copied().unwrap_or(NetClass::Signal);
            Some(PinInfo {
                _signal: sc.signal,
                role,
                net: nid,
                net_class: nc,
            })
        })
        .collect();

    if pins.is_empty() {
        return role_from_designator(&inst.name);
    }

    // ── All connected nets are Power → PowerChain ────────────────────
    if pins.iter().all(|p| p.net_class == NetClass::Power) {
        return ComponentRole::PowerChain;
    }

    // ── 2-pin component (exactly 2 library signals) ─────────────────
    if lib.signals.len() == 2 && pins.len() == 2 {
        return classify_two_pin(inst.uuid, &pins, net_members);
    }

    // ── N-pin: check for input / output signals on signal nets ───────
    let on_signal_net =
        |p: &&PinInfo| matches!(p.net_class, NetClass::Signal | NetClass::HighFanout);

    // Bidirectional, OpenDrain, and OpenCollector count as BOTH input AND output.
    let has_output = pins.iter().filter(on_signal_net).any(|p| {
        matches!(
            p.role,
            SignalRole::Output
                | SignalRole::Bidirectional
                | SignalRole::OpenDrain
                | SignalRole::OpenCollector
        )
    });

    let has_input = pins.iter().filter(on_signal_net).any(|p| {
        matches!(
            p.role,
            SignalRole::Input
                | SignalRole::Bidirectional
                | SignalRole::OpenDrain
                | SignalRole::OpenCollector
        )
    });

    match (has_output, has_input) {
        (true, false) => ComponentRole::Source,
        (false, true) => ComponentRole::Sink,
        (true, true) => ComponentRole::Processor,
        (false, false) => role_from_designator(&inst.name),
    }
}

/// Sub-classify a 2-pin component based on the net types its pins sit on.
///
/// * Both signals on signal nets → [`ComponentRole::SeriesPassive`]
/// * Both on power nets → [`ComponentRole::BypassPassive`]
/// * One power + one signal (with other components) → [`ComponentRole::ShuntPassive`]
/// * One power + one signal (no other components) → [`ComponentRole::BypassPassive`]
fn classify_two_pin(
    comp_uuid: Uuid,
    pins: &[PinInfo],
    net_members: &HashMap<Uuid, Vec<Uuid>>,
) -> ComponentRole {
    debug_assert_eq!(pins.len(), 2);

    let power_count = pins
        .iter()
        .filter(|p| p.net_class == NetClass::Power)
        .count();

    let signal_pins: Vec<&PinInfo> = pins
        .iter()
        .filter(|p| matches!(p.net_class, NetClass::Signal | NetClass::HighFanout))
        .collect();

    match (power_count, signal_pins.len()) {
        // Both on signal nets → in-line series passive (coupling cap, series R).
        (0, 2) => ComponentRole::SeriesPassive,

        // Both on power nets → bypass / decoupling cap between VCC and GND.
        (2, 0) => ComponentRole::BypassPassive,

        // One power, one signal.
        (1, 1) => {
            let sig_net = signal_pins[0].net;
            let others = net_members
                .get(&sig_net)
                .map_or(0, |m| m.iter().filter(|&&id| id != comp_uuid).count());

            if others > 0 {
                // Signal net has other components → pull-up / pull-down.
                ComponentRole::ShuntPassive
            } else {
                // Signal net is private (no other component) → treat as bypass.
                ComponentRole::BypassPassive
            }
        }

        // Fallback for partially-connected or unexpected topologies.
        _ => ComponentRole::SeriesPassive,
    }
}

/// Heuristic role derived from the designator prefix (e.g. "R1" → `R` →
/// [`ComponentRole::SeriesPassive`]).
fn role_from_designator(name: &str) -> ComponentRole {
    let prefix: String = name
        .chars()
        .take_while(|c| c.is_ascii_alphabetic())
        .collect::<String>()
        .to_uppercase();

    match prefix.as_str() {
        "U" => ComponentRole::Processor,
        "R" | "C" | "L" => ComponentRole::SeriesPassive,
        "J" | "P" => ComponentRole::Source,
        "D" => ComponentRole::Sink,
        _ => ComponentRole::Processor,
    }
}

// ===================================================================
// 4. Signal-flow DAG
// ===================================================================

/// Build a directed acyclic graph of signal flow between components.
///
/// For every **Signal** net the function determines which component is the
/// *driver* (has an Output / Bidirectional / OpenDrain / OpenCollector pin)
/// and which are *receivers* (Input / Passive).  Edges point driver → receiver.
///
/// For nets where every pin is Passive the component roles are used to infer
/// direction: [`ComponentRole::Source`] and [`ComponentRole::Processor`] drive;
/// everything else receives.
///
/// **Power** and **HighFanout** nets are skipped entirely.  Cycles are broken
/// by omitting DFS back-edges so the result is guaranteed acyclic.
pub fn build_flow_dag(
    circuit: &Circuit,
    net_classes: &HashMap<Uuid, NetClass>,
    net_members: &HashMap<Uuid, Vec<Uuid>>,
    comp_roles: &HashMap<Uuid, ComponentRole>,
    lib_comps: &HashMap<Uuid, Component>,
) -> FlowGraph {
    let mut graph = FlowGraph::new();

    // Every component is a node even if it has no signal-flow edges.
    for comp in &circuit.components {
        graph.add_node(comp.uuid);
    }

    let comp_by_id: HashMap<Uuid, &ComponentInstance> =
        circuit.components.iter().map(|c| (c.uuid, c)).collect();

    // Collect candidate edges (may contain cycles).
    let mut candidates: Vec<(Uuid, Uuid, Uuid)> = Vec::new(); // (from, to, net)

    for net in &circuit.nets {
        let nc = net_classes
            .get(&net.uuid)
            .copied()
            .unwrap_or(NetClass::Signal);

        // Skip power rails and high-fanout nets.
        if matches!(nc, NetClass::Power | NetClass::HighFanout) {
            continue;
        }

        let members = match net_members.get(&net.uuid) {
            Some(m) if m.len() >= 2 => m,
            _ => continue,
        };

        // Categorise each member's pin on this net by signal role.
        let mut drivers: Vec<Uuid> = Vec::new();
        let mut receivers: Vec<Uuid> = Vec::new();
        let mut all_passive = true;

        for &cid in members {
            match signal_role_on_net(cid, net.uuid, &comp_by_id, lib_comps) {
                Some(
                    SignalRole::Output
                    | SignalRole::Bidirectional
                    | SignalRole::OpenDrain
                    | SignalRole::OpenCollector,
                ) => {
                    all_passive = false;
                    drivers.push(cid);
                }
                Some(SignalRole::Input) => {
                    all_passive = false;
                    receivers.push(cid);
                }
                // Passive, Power, or unknown → tentative receiver.
                _ => {
                    receivers.push(cid);
                }
            }
        }

        // When every pin is passive, infer direction from component roles.
        if all_passive {
            drivers.clear();
            receivers.clear();
            for &cid in members {
                match comp_roles.get(&cid).copied() {
                    Some(ComponentRole::Source | ComponentRole::Processor) => {
                        drivers.push(cid);
                    }
                    _ => {
                        receivers.push(cid);
                    }
                }
            }
        }

        // Create driver → receiver edges for this net.
        for &d in &drivers {
            for &r in &receivers {
                if d != r {
                    candidates.push((d, r, net.uuid));
                }
            }
        }
    }

    // Insert edges while breaking cycles.
    add_edges_acyclic(&mut graph, candidates);

    graph
}

/// Look up the [`SignalRole`] of the pin that connects component `comp_id` to
/// net `net_id`.  Returns `None` when the library data is missing or no
/// connection to that net exists.
fn signal_role_on_net(
    comp_id: Uuid,
    net_id: Uuid,
    comp_by_id: &HashMap<Uuid, &ComponentInstance>,
    lib_comps: &HashMap<Uuid, Component>,
) -> Option<SignalRole> {
    let inst = comp_by_id.get(&comp_id)?;
    let lib = lib_comps.get(&inst.lib_component)?;
    let sig_roles: HashMap<Uuid, SignalRole> =
        lib.signals.iter().map(|s| (s.uuid, s.role)).collect();

    inst.signal_connections
        .iter()
        .find(|sc| sc.net == Some(net_id))
        .and_then(|sc| sig_roles.get(&sc.signal).copied())
}

// -------------------------------------------------------------------
// Cycle-breaking helpers
// -------------------------------------------------------------------

/// Insert candidate edges into `graph`, silently dropping any that would
/// introduce a cycle (i.e. DFS back-edges).
fn add_edges_acyclic(graph: &mut FlowGraph, edges: Vec<(Uuid, Uuid, Uuid)>) {
    if edges.is_empty() {
        return;
    }

    // Build temporary adjacency list for cycle detection.
    let mut adj: HashMap<Uuid, Vec<(Uuid, Uuid)>> = HashMap::new();
    let mut all_nodes: HashSet<Uuid> = HashSet::new();
    for &(from, to, net) in &edges {
        adj.entry(from).or_default().push((to, net));
        all_nodes.insert(from);
        all_nodes.insert(to);
    }

    let back = find_back_edges(&adj, &all_nodes);

    for (from, to, net) in edges {
        if !back.contains(&(from, to)) {
            graph.add_edge(from, to, net);
        }
    }
}

/// Iterative DFS that returns the set of *back-edges* `(u, v)` where `v` is
/// an ancestor of `u` on the current DFS path.
fn find_back_edges(
    adj: &HashMap<Uuid, Vec<(Uuid, Uuid)>>,
    nodes: &HashSet<Uuid>,
) -> HashSet<(Uuid, Uuid)> {
    let mut visited = HashSet::new();
    let mut on_stack = HashSet::new();
    let mut back = HashSet::new();

    for &start in nodes {
        if visited.contains(&start) {
            continue;
        }

        // Stack frames: (node, index-into-adjacency-list).
        let mut stack: Vec<(Uuid, usize)> = Vec::new();
        visited.insert(start);
        on_stack.insert(start);
        stack.push((start, 0));

        loop {
            let Some(&(node, idx)) = stack.last() else {
                break;
            };

            let neighbors = adj.get(&node).map(|v| v.as_slice()).unwrap_or(&[]);

            if idx >= neighbors.len() {
                // All neighbours explored — backtrack.
                on_stack.remove(&node);
                stack.pop();
                continue;
            }

            // Advance the index for this frame.
            stack.last_mut().unwrap().1 += 1;

            let (target, _net) = neighbors[idx];

            if !visited.contains(&target) {
                visited.insert(target);
                on_stack.insert(target);
                stack.push((target, 0));
            } else if on_stack.contains(&target) {
                // `target` is an ancestor on the current path → back-edge.
                back.insert((node, target));
            }
        }
    }

    back
}

// ===================================================================
// 5. Companion detection
// ===================================================================

/// Detect bypass caps and pull-up / pull-down resistors and pair each with its
/// parent IC.
///
/// * **BypassPassive** → [`AttachmentType::Bypass`].  Parent is the first
///   non-passive component that shares a power net (non-ground rails are tried
///   first since they tend to be more selective).
/// * **ShuntPassive** → [`AttachmentType::PullUp`] or
///   [`AttachmentType::PullDown`] depending on whether the power pin goes to a
///   VCC-family or GND-family net.  Parent is the non-passive component on the
///   shared signal net.
pub fn detect_companions(
    circuit: &Circuit,
    comp_roles: &HashMap<Uuid, ComponentRole>,
    net_classes: &HashMap<Uuid, NetClass>,
    net_members: &HashMap<Uuid, Vec<Uuid>>,
    _lib_comps: &HashMap<Uuid, Component>,
) -> Vec<Companion> {
    let comp_by_id: HashMap<Uuid, &ComponentInstance> =
        circuit.components.iter().map(|c| (c.uuid, c)).collect();

    let net_name: HashMap<Uuid, &str> = circuit
        .nets
        .iter()
        .map(|n| (n.uuid, n.name.as_str()))
        .collect();

    let mut out = Vec::new();

    for inst in &circuit.components {
        let role = match comp_roles.get(&inst.uuid) {
            Some(r) => *r,
            None => continue,
        };

        let companion = match role {
            ComponentRole::BypassPassive => find_bypass_parent(
                inst,
                &comp_by_id,
                comp_roles,
                net_classes,
                net_members,
                &net_name,
            ),
            ComponentRole::ShuntPassive => find_shunt_parent(
                inst,
                &comp_by_id,
                comp_roles,
                net_classes,
                net_members,
                &net_name,
            ),
            _ => None,
        };

        if let Some(c) = companion {
            out.push(c);
        }
    }

    out
}

// -------------------------------------------------------------------
// Internal companion helpers
// -------------------------------------------------------------------

/// Pair a [`ComponentRole::BypassPassive`] component with a non-passive IC that
/// shares a power net.
fn find_bypass_parent(
    inst: &ComponentInstance,
    comp_by_id: &HashMap<Uuid, &ComponentInstance>,
    comp_roles: &HashMap<Uuid, ComponentRole>,
    net_classes: &HashMap<Uuid, NetClass>,
    net_members: &HashMap<Uuid, Vec<Uuid>>,
    net_name: &HashMap<Uuid, &str>,
) -> Option<Companion> {
    // Collect power nets this cap connects to.
    let mut power_nets: Vec<Uuid> = inst
        .signal_connections
        .iter()
        .filter_map(|sc| {
            let nid = sc.net?;
            if net_classes.get(&nid).copied() == Some(NetClass::Power) {
                Some(nid)
            } else {
                None
            }
        })
        .collect();

    // Try non-ground nets first — they're more selective for parent finding.
    power_nets.sort_by_key(|nid| is_ground_net_name(net_name.get(nid).unwrap_or(&"")));

    for &pn in &power_nets {
        let members = match net_members.get(&pn) {
            Some(m) => m,
            None => continue,
        };

        // First non-passive component on this power net is the parent.
        if let Some(&parent) = members
            .iter()
            .find(|&&m| m != inst.uuid && is_active_role(comp_roles.get(&m).copied()))
        {
            let parent_signal = comp_by_id.get(&parent).and_then(|p| {
                p.signal_connections
                    .iter()
                    .find(|sc| sc.net == Some(pn))
                    .map(|sc| sc.signal)
            });

            return Some(Companion {
                component: inst.uuid,
                parent,
                attachment: AttachmentType::Bypass,
                parent_pin_signal: parent_signal,
            });
        }
    }

    None
}

/// Pair a [`ComponentRole::ShuntPassive`] component (pull-up / pull-down) with
/// the IC on its signal net.
fn find_shunt_parent(
    inst: &ComponentInstance,
    comp_by_id: &HashMap<Uuid, &ComponentInstance>,
    comp_roles: &HashMap<Uuid, ComponentRole>,
    net_classes: &HashMap<Uuid, NetClass>,
    net_members: &HashMap<Uuid, Vec<Uuid>>,
    net_name: &HashMap<Uuid, &str>,
) -> Option<Companion> {
    // Identify the signal net and power net this resistor bridges.
    let signal_net = inst.signal_connections.iter().find_map(|sc| {
        let nid = sc.net?;
        match net_classes.get(&nid).copied()? {
            NetClass::Signal | NetClass::HighFanout => Some(nid),
            _ => None,
        }
    })?;

    let power_net = inst.signal_connections.iter().find_map(|sc| {
        let nid = sc.net?;
        if net_classes.get(&nid).copied() == Some(NetClass::Power) {
            Some(nid)
        } else {
            None
        }
    })?;

    // Parent = first non-passive component on the signal net.
    let members = net_members.get(&signal_net)?;
    let &parent = members
        .iter()
        .find(|&&m| m != inst.uuid && is_active_role(comp_roles.get(&m).copied()))?;

    // Record which signal on the parent connects to the shared signal net so
    // the placement phase knows which pin to position near.
    let parent_signal = comp_by_id.get(&parent).and_then(|p| {
        p.signal_connections
            .iter()
            .find(|sc| sc.net == Some(signal_net))
            .map(|sc| sc.signal)
    });

    // Pull-Up when the power net is VCC-family, Pull-Down when GND-family.
    let pname = net_name.get(&power_net).unwrap_or(&"");
    let attachment = if is_ground_net_name(pname) {
        AttachmentType::PullDown
    } else {
        AttachmentType::PullUp
    };

    Some(Companion {
        component: inst.uuid,
        parent,
        attachment,
        parent_pin_signal: parent_signal,
    })
}

/// Returns `true` when the component role represents an active part (IC,
/// connector, voltage regulator) rather than a passive.
fn is_active_role(role: Option<ComponentRole>) -> bool {
    matches!(
        role,
        Some(
            ComponentRole::Source
                | ComponentRole::Sink
                | ComponentRole::Processor
                | ComponentRole::PowerChain
        )
    )
}
