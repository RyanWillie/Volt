//! Net-segment splitter for schematics and boards.
//!
//! When a wire or trace is deleted from a net segment, the remaining elements
//! may form disconnected groups. This module detects that and splits the
//! segment into multiple segments, one per connected component.

use std::collections::HashMap;
use std::hash::Hash;

use uuid::Uuid;

use crate::common::*;
use crate::project::*;

// ============================================================================
// Public API
// ============================================================================

/// Split any disconnected schematic net segments.
/// Returns the number of new segments created (0 if no splits needed).
pub fn split_schematic_net_segments(schematic: &mut Schematic) -> usize {
    let mut new_segments: Vec<SchematicNetSegment> = Vec::new();
    let mut splits = 0;

    let original_segments: Vec<SchematicNetSegment> = schematic.net_segments.drain(..).collect();

    for segment in original_segments {
        let components = schematic_connected_components(&segment);
        if components.len() <= 1 {
            // No split needed — keep as-is
            new_segments.push(segment);
        } else {
            // Split into multiple segments
            for (i, component_indices) in components.iter().enumerate() {
                let mut new_seg = SchematicNetSegment {
                    uuid: if i == 0 { segment.uuid } else { new_uuid() },
                    net: segment.net,
                    junctions: vec![],
                    lines: vec![],
                    labels: vec![],
                };

                // Collect junction UUIDs in this component
                let junction_uuids: Vec<Uuid> = component_indices
                    .iter()
                    .filter_map(|idx| {
                        if let SchematicNode::Junction(uuid) = idx {
                            Some(*uuid)
                        } else {
                            None
                        }
                    })
                    .collect();

                // Add junctions
                for j in &segment.junctions {
                    if junction_uuids.contains(&j.uuid) {
                        new_seg.junctions.push(j.clone());
                    }
                }

                // Add lines that have both endpoints in this component
                for line in &segment.lines {
                    let from_in = endpoint_in_component(&line.from, component_indices);
                    let to_in = endpoint_in_component(&line.to, component_indices);
                    if from_in || to_in {
                        new_seg.lines.push(line.clone());
                    }
                }

                // Assign labels by proximity to junctions in this component
                for label in &segment.labels {
                    let closest_component =
                        find_closest_component_for_label(&label.position, &segment, &components);
                    if closest_component == i {
                        new_seg.labels.push(label.clone());
                    }
                }

                new_segments.push(new_seg);
            }
            splits += components.len() - 1;
        }
    }

    schematic.net_segments = new_segments;
    splits
}

/// Split any disconnected board net segments.
/// Returns the number of new segments created (0 if no splits needed).
pub fn split_board_net_segments(board: &mut Board) -> usize {
    let mut new_segments: Vec<BoardNetSegment> = Vec::new();
    let mut splits = 0;

    let original_segments: Vec<BoardNetSegment> = board.net_segments.drain(..).collect();

    for segment in original_segments {
        let components = board_connected_components(&segment);
        if components.len() <= 1 {
            new_segments.push(segment);
        } else {
            for (i, component_nodes) in components.iter().enumerate() {
                let mut new_seg = BoardNetSegment {
                    uuid: if i == 0 { segment.uuid } else { new_uuid() },
                    net: segment.net,
                    traces: vec![],
                    vias: vec![],
                    junctions: vec![],
                    pads: vec![],
                };

                let junction_uuids: Vec<Uuid> = component_nodes
                    .iter()
                    .filter_map(|n| {
                        if let BoardNode::Junction(u) = n {
                            Some(*u)
                        } else {
                            None
                        }
                    })
                    .collect();
                let via_uuids: Vec<Uuid> = component_nodes
                    .iter()
                    .filter_map(|n| {
                        if let BoardNode::Via(u) = n {
                            Some(*u)
                        } else {
                            None
                        }
                    })
                    .collect();
                for j in &segment.junctions {
                    if junction_uuids.contains(&j.uuid) {
                        new_seg.junctions.push(j.clone());
                    }
                }
                for v in &segment.vias {
                    if via_uuids.contains(&v.uuid) {
                        new_seg.vias.push(v.clone());
                    }
                }

                for trace in &segment.traces {
                    let from_in = board_endpoint_in_component(&trace.from, component_nodes);
                    let to_in = board_endpoint_in_component(&trace.to, component_nodes);
                    if from_in || to_in {
                        new_seg.traces.push(trace.clone());
                    }
                }

                // Assign standalone pads by proximity
                for pad in &segment.pads {
                    let closest = find_closest_board_component_for_position(
                        &pad.position,
                        &segment,
                        &components,
                    );
                    if closest == i {
                        new_seg.pads.push(pad.clone());
                    }
                }

                new_segments.push(new_seg);
            }
            splits += components.len() - 1;
        }
    }

    board.net_segments = new_segments;
    splits
}

// ============================================================================
// Schematic connectivity
// ============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SchematicNode {
    Junction(Uuid),
    SymbolPin(Uuid, Uuid), // symbol, pin
    SheetPin(Uuid, Uuid),  // sheet_ref, pin
    HierPort(Uuid),        // port
}

pub fn schematic_connected_components(segment: &SchematicNetSegment) -> Vec<Vec<SchematicNode>> {
    // Collect all nodes
    let mut nodes: Vec<SchematicNode> = Vec::new();
    let mut edges = Vec::new();
    for j in &segment.junctions {
        nodes.push(SchematicNode::Junction(j.uuid));
    }
    for line in &segment.lines {
        let from_node = schematic_node_for_endpoint(&line.from);
        let to_node = schematic_node_for_endpoint(&line.to);
        if !nodes.contains(&from_node) {
            nodes.push(from_node.clone());
        }
        if !nodes.contains(&to_node) {
            nodes.push(to_node.clone());
        }
        edges.push((from_node, to_node));
    }

    connected_components(nodes, edges)
}

pub fn schematic_node_for_endpoint(ep: &LineEndpoint) -> SchematicNode {
    match ep {
        LineEndpoint::Junction { junction } => SchematicNode::Junction(*junction),
        LineEndpoint::Symbol { symbol, pin } => SchematicNode::SymbolPin(*symbol, *pin),
        LineEndpoint::SheetPin { sheet_ref, pin } => SchematicNode::SheetPin(*sheet_ref, *pin),
        LineEndpoint::HierPort { port } => SchematicNode::HierPort(*port),
    }
}

fn endpoint_in_component(ep: &LineEndpoint, component: &[SchematicNode]) -> bool {
    let node = schematic_node_for_endpoint(ep);
    component.contains(&node)
}

fn find_closest_component_for_label(
    label_pos: &Position,
    segment: &SchematicNetSegment,
    components: &[Vec<SchematicNode>],
) -> usize {
    let mut best_idx = 0;
    let mut best_dist = f64::MAX;

    for (i, comp) in components.iter().enumerate() {
        for node in comp {
            let pos = match node {
                SchematicNode::Junction(uuid) => segment
                    .junctions
                    .iter()
                    .find(|j| j.uuid == *uuid)
                    .map(|j| j.position),
                SchematicNode::SymbolPin(_, _) => None, // can't resolve position without library
                SchematicNode::SheetPin(_, _) => None,
                SchematicNode::HierPort(_) => None,
            };
            if let Some(p) = pos {
                let dx = p.x - label_pos.x;
                let dy = p.y - label_pos.y;
                let dist = dx * dx + dy * dy;
                if dist < best_dist {
                    best_dist = dist;
                    best_idx = i;
                }
            }
        }
    }
    best_idx
}

// ============================================================================
// Board connectivity
// ============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum BoardNode {
    Junction(Uuid),
    Via(Uuid),
    DevicePad(Uuid, Uuid), // device, pad
}

pub fn board_connected_components(segment: &BoardNetSegment) -> Vec<Vec<BoardNode>> {
    let mut nodes: Vec<BoardNode> = Vec::new();
    let mut edges = Vec::new();
    for j in &segment.junctions {
        nodes.push(BoardNode::Junction(j.uuid));
    }
    for v in &segment.vias {
        nodes.push(BoardNode::Via(v.uuid));
    }
    for trace in &segment.traces {
        let from_node = board_node_for_endpoint(&trace.from);
        let to_node = board_node_for_endpoint(&trace.to);
        if !nodes.contains(&from_node) {
            nodes.push(from_node.clone());
        }
        if !nodes.contains(&to_node) {
            nodes.push(to_node.clone());
        }
        edges.push((from_node, to_node));
    }

    connected_components(nodes, edges)
}

pub fn board_node_for_endpoint(ep: &TraceEndpoint) -> BoardNode {
    match ep {
        TraceEndpoint::Junction { junction } => BoardNode::Junction(*junction),
        TraceEndpoint::Via { via } => BoardNode::Via(*via),
        TraceEndpoint::Device { device, pad } => BoardNode::DevicePad(*device, *pad),
    }
}

fn board_endpoint_in_component(ep: &TraceEndpoint, component: &[BoardNode]) -> bool {
    let node = board_node_for_endpoint(ep);
    component.contains(&node)
}

fn find_closest_board_component_for_position(
    pos: &Position,
    segment: &BoardNetSegment,
    components: &[Vec<BoardNode>],
) -> usize {
    let mut best_idx = 0;
    let mut best_dist = f64::MAX;

    for (i, comp) in components.iter().enumerate() {
        for node in comp {
            let node_pos = match node {
                BoardNode::Junction(uuid) => segment
                    .junctions
                    .iter()
                    .find(|j| j.uuid == *uuid)
                    .map(|j| j.position),
                BoardNode::Via(uuid) => segment
                    .vias
                    .iter()
                    .find(|v| v.uuid == *uuid)
                    .map(|v| v.position),
                BoardNode::DevicePad(_, _) => None,
            };
            if let Some(p) = node_pos {
                let dx = p.x - pos.x;
                let dy = p.y - pos.y;
                let dist = dx * dx + dy * dy;
                if dist < best_dist {
                    best_dist = dist;
                    best_idx = i;
                }
            }
        }
    }
    best_idx
}

fn connected_components<T>(nodes: Vec<T>, edges: Vec<(T, T)>) -> Vec<Vec<T>>
where
    T: Copy + Eq + Hash,
{
    if nodes.is_empty() {
        return vec![];
    }

    let mut unique_nodes = Vec::new();
    let mut index_by_node = HashMap::new();
    for node in nodes {
        if index_by_node.contains_key(&node) {
            continue;
        }
        let idx = unique_nodes.len();
        unique_nodes.push(node);
        index_by_node.insert(node, idx);
    }

    let mut parent: Vec<usize> = (0..unique_nodes.len()).collect();

    fn find(parent: &mut [usize], mut idx: usize) -> usize {
        while parent[idx] != idx {
            parent[idx] = parent[parent[idx]];
            idx = parent[idx];
        }
        idx
    }

    for (from, to) in edges {
        let (Some(&from_idx), Some(&to_idx)) = (index_by_node.get(&from), index_by_node.get(&to))
        else {
            continue;
        };
        let from_root = find(&mut parent, from_idx);
        let to_root = find(&mut parent, to_idx);
        if from_root != to_root {
            parent[from_root] = to_root;
        }
    }

    let mut groups: HashMap<usize, Vec<T>> = HashMap::new();
    for (idx, node) in unique_nodes.into_iter().enumerate() {
        let root = find(&mut parent, idx);
        groups.entry(root).or_default().push(node);
    }

    groups.into_values().collect()
}

// ============================================================================
// Tests
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    fn make_junction(uuid: Uuid, x: f64, y: f64) -> Junction {
        Junction {
            uuid,
            position: Position::new(x, y),
        }
    }

    fn make_line(from_j: Uuid, to_j: Uuid) -> SchematicLine {
        SchematicLine {
            uuid: new_uuid(),
            width: 0.15875,
            from: LineEndpoint::Junction { junction: from_j },
            to: LineEndpoint::Junction { junction: to_j },
        }
    }

    fn make_trace(from_j: Uuid, to_j: Uuid) -> Trace {
        Trace {
            uuid: new_uuid(),
            layer: Layer::TopCopper,
            width: 0.5,
            from: TraceEndpoint::Junction { junction: from_j },
            to: TraceEndpoint::Junction { junction: to_j },
        }
    }

    #[test]
    fn schematic_no_split_needed() {
        let j1 = new_uuid();
        let j2 = new_uuid();
        let j3 = new_uuid();
        let net = new_uuid();

        let mut schematic = Schematic {
            uuid: new_uuid(),
            name: "test".into(),
            grid: Grid {
                interval: 2.54,
                unit: GridUnit::Millimeters,
            },
            symbols: vec![],
            net_segments: vec![SchematicNetSegment {
                uuid: new_uuid(),
                net,
                junctions: vec![
                    make_junction(j1, 0.0, 0.0),
                    make_junction(j2, 10.0, 0.0),
                    make_junction(j3, 20.0, 0.0),
                ],
                lines: vec![make_line(j1, j2), make_line(j2, j3)],
                labels: vec![],
            }],
            sheet_refs: vec![],
            hierarchical_ports: vec![],
            power_ports: vec![],
            power_flags: vec![],
            bus_segments: vec![],
            bus_entries: vec![],
            bus_aliases: vec![],
        };

        let splits = split_schematic_net_segments(&mut schematic);
        assert_eq!(splits, 0);
        assert_eq!(schematic.net_segments.len(), 1);
    }

    #[test]
    fn schematic_split_into_two() {
        let j1 = new_uuid();
        let j2 = new_uuid();
        let j3 = new_uuid();
        let j4 = new_uuid();
        let net = new_uuid();

        // Two disconnected pairs: j1-j2 and j3-j4
        let mut schematic = Schematic {
            uuid: new_uuid(),
            name: "test".into(),
            grid: Grid {
                interval: 2.54,
                unit: GridUnit::Millimeters,
            },
            symbols: vec![],
            net_segments: vec![SchematicNetSegment {
                uuid: new_uuid(),
                net,
                junctions: vec![
                    make_junction(j1, 0.0, 0.0),
                    make_junction(j2, 10.0, 0.0),
                    make_junction(j3, 30.0, 0.0),
                    make_junction(j4, 40.0, 0.0),
                ],
                lines: vec![make_line(j1, j2), make_line(j3, j4)],
                labels: vec![],
            }],
            sheet_refs: vec![],
            hierarchical_ports: vec![],
            power_ports: vec![],
            power_flags: vec![],
            bus_segments: vec![],
            bus_entries: vec![],
            bus_aliases: vec![],
        };

        let splits = split_schematic_net_segments(&mut schematic);
        assert_eq!(splits, 1);
        assert_eq!(schematic.net_segments.len(), 2);
        // Both segments share the same net
        assert!(schematic.net_segments.iter().all(|s| s.net == net));
        // Each segment has 1 line
        for seg in &schematic.net_segments {
            assert_eq!(seg.lines.len(), 1);
        }
    }

    #[test]
    fn board_split_into_two() {
        let j1 = new_uuid();
        let j2 = new_uuid();
        let j3 = new_uuid();
        let j4 = new_uuid();
        let net = new_uuid();

        let mut board = Board {
            uuid: new_uuid(),
            name: "test".into(),
            grid: Grid {
                interval: 1.0,
                unit: GridUnit::Millimeters,
            },
            inner_layers: 0,
            thickness: 1.6,
            solder_resist: SolderResistColor::Green,
            silkscreen: SilkscreenColor::White,
            default_font: String::new(),
            design_rules: serde_json::from_str("{}").unwrap(),
            drc_settings: serde_json::from_str("{}").unwrap(),
            fabrication_output_settings: FabricationOutputSettings::default(),
            devices: vec![],
            net_segments: vec![BoardNetSegment {
                uuid: new_uuid(),
                net: Some(net),
                traces: vec![make_trace(j1, j2), make_trace(j3, j4)],
                vias: vec![],
                junctions: vec![
                    Junction {
                        uuid: j1,
                        position: Position::new(0.0, 0.0),
                    },
                    Junction {
                        uuid: j2,
                        position: Position::new(10.0, 0.0),
                    },
                    Junction {
                        uuid: j3,
                        position: Position::new(30.0, 0.0),
                    },
                    Junction {
                        uuid: j4,
                        position: Position::new(40.0, 0.0),
                    },
                ],
                pads: vec![],
            }],
            planes: vec![],
            polygons: vec![],
            holes: vec![],
        };

        let splits = split_board_net_segments(&mut board);
        assert_eq!(splits, 1);
        assert_eq!(board.net_segments.len(), 2);
        for seg in &board.net_segments {
            assert_eq!(seg.net, Some(net));
            assert_eq!(seg.traces.len(), 1);
        }
    }

    #[test]
    fn board_no_split_when_connected() {
        let j1 = new_uuid();
        let j2 = new_uuid();
        let j3 = new_uuid();
        let net = new_uuid();

        let mut board = Board {
            uuid: new_uuid(),
            name: "test".into(),
            grid: Grid {
                interval: 1.0,
                unit: GridUnit::Millimeters,
            },
            inner_layers: 0,
            thickness: 1.6,
            solder_resist: SolderResistColor::Green,
            silkscreen: SilkscreenColor::White,
            default_font: String::new(),
            design_rules: serde_json::from_str("{}").unwrap(),
            drc_settings: serde_json::from_str("{}").unwrap(),
            fabrication_output_settings: FabricationOutputSettings::default(),
            devices: vec![],
            net_segments: vec![BoardNetSegment {
                uuid: new_uuid(),
                net: Some(net),
                traces: vec![make_trace(j1, j2), make_trace(j2, j3)],
                vias: vec![],
                junctions: vec![
                    Junction {
                        uuid: j1,
                        position: Position::new(0.0, 0.0),
                    },
                    Junction {
                        uuid: j2,
                        position: Position::new(10.0, 0.0),
                    },
                    Junction {
                        uuid: j3,
                        position: Position::new(20.0, 0.0),
                    },
                ],
                pads: vec![],
            }],
            planes: vec![],
            polygons: vec![],
            holes: vec![],
        };

        let splits = split_board_net_segments(&mut board);
        assert_eq!(splits, 0);
        assert_eq!(board.net_segments.len(), 1);
    }
}
