//! Phase 2 (Ranking) and Phase 3 (Ordering) for the autoplace algorithm.
//!
//! **Ranking** assigns each main component to a horizontal rank (column) based
//! on signal-flow depth using a longest-path-forward relaxation from source and
//! power-chain components, then adjusts for sinks and power chains.
//!
//! **Ordering** arranges components within each rank to minimize edge crossings
//! using initial connectivity sorting followed by a multi-pass barycenter
//! heuristic (Sugiyama-style).

use std::collections::{HashMap, HashSet, VecDeque};
use uuid::Uuid;

use super::types::*;

// ===========================================================================
// Phase 2 — Ranking
// ===========================================================================

/// Assign each component to a horizontal rank (column) based on signal flow.
///
/// Algorithm:
/// 1. All `Source` and `PowerChain` components seed rank 0.
/// 2. Ranks propagate forward via longest-path relaxation:
///    `rank[v] = max(rank[u] + 1)` over all predecessors `u` of `v`.
/// 3. If no sources exist, the most-connected component becomes rank 0
///    and BFS proceeds outward in both directions.
/// 4. `Sink` components are promoted to the maximum rank (rightmost).
/// 5. `PowerChain` components are moved to rank −1 (above the main flow).
/// 6. Empty ranks are compacted (shifted left to fill gaps).
/// 7. Any unconnected component defaults to rank 0.
pub fn assign_ranks(
    main_components: &[Uuid],
    flow_dag: &FlowGraph,
    comp_roles: &HashMap<Uuid, ComponentRole>,
) -> HashMap<Uuid, i32> {
    if main_components.is_empty() {
        return HashMap::new();
    }

    let comp_set: HashSet<Uuid> = main_components.iter().copied().collect();
    let mut ranks: HashMap<Uuid, i32> = HashMap::new();

    // ── Step 1: seed from Sources / PowerChain ──────────────────────────

    let sources: Vec<Uuid> = main_components
        .iter()
        .filter(|&&id| {
            matches!(
                comp_roles.get(&id),
                Some(ComponentRole::Source) | Some(ComponentRole::PowerChain)
            )
        })
        .copied()
        .collect();

    if !sources.is_empty() {
        for &s in &sources {
            ranks.insert(s, 0);
        }
        longest_path_forward(&comp_set, flow_dag, &mut ranks);
    } else {
        // Fallback: use the most-connected component as the hub.
        let hub = most_connected_node(main_components, flow_dag);
        ranks.insert(hub, 0);
        bfs_outward(&comp_set, hub, flow_dag, &mut ranks);
    }

    // ── Step 2: default unranked (disconnected) components to 0 ─────────

    for &id in main_components {
        ranks.entry(id).or_insert(0);
    }

    // ── Step 3: promote Sinks to max rank ───────────────────────────────

    let max_rank = ranks.values().copied().max().unwrap_or(0);
    for &id in main_components {
        if matches!(comp_roles.get(&id), Some(ComponentRole::Sink)) {
            ranks.insert(id, max_rank);
        }
    }

    // ── Step 4: PowerChain → rank −1 ───────────────────────────────────

    for &id in main_components {
        if matches!(comp_roles.get(&id), Some(ComponentRole::PowerChain)) {
            ranks.insert(id, -1);
        }
    }

    // ── Step 5: compact empty ranks ─────────────────────────────────────

    compact_ranks(&mut ranks, main_components);

    ranks
}

// ===========================================================================
// Phase 3 — Ordering
// ===========================================================================

/// Order components within each rank to minimize edge crossings.
///
/// Returns a `Vec` indexed by *adjusted* rank (offset so index 0 is the
/// leftmost column). Each inner `Vec` is the ordered list of component
/// UUIDs for that column.
///
/// Algorithm:
/// 1. Bucket components into rank layers.
/// 2. Initial sort per rank: by connection count to the previous rank
///    (descending), then by UUID string for deterministic tie-breaking.
/// 3. Multi-pass barycenter heuristic (up to 24 iterations):
///    - Forward sweep (left → right): sort each rank by the average
///      position of its predecessors in the rank to the left.
///    - Backward sweep (right → left): sort each rank by the average
///      position of its successors in the rank to the right.
///    - Stop early when a full pass yields no crossing improvement.
pub fn order_within_ranks(
    ranks: &HashMap<Uuid, i32>,
    flow_dag: &FlowGraph,
    _net_members: &HashMap<Uuid, Vec<Uuid>>,
    _net_classes: &HashMap<Uuid, NetClass>,
) -> Vec<Vec<Uuid>> {
    if ranks.is_empty() {
        return Vec::new();
    }

    let min_rank = ranks.values().copied().min().unwrap();
    let max_rank = ranks.values().copied().max().unwrap();
    let width = (max_rank - min_rank + 1) as usize;

    // ── Step 1: bucket components into layers ───────────────────────────

    let mut layers: Vec<Vec<Uuid>> = vec![Vec::new(); width];
    for (&id, &r) in ranks {
        layers[(r - min_rank) as usize].push(id);
    }

    // ── Step 2: initial ordering ────────────────────────────────────────
    // Sort each rank by (connections to previous rank DESC, UUID string ASC).

    for li in 0..width {
        let prev: HashSet<Uuid> = if li > 0 {
            layers[li - 1].iter().copied().collect()
        } else {
            HashSet::new()
        };
        layers[li].sort_by(|a, b| {
            let ca = connections_to_set(*a, &prev, flow_dag);
            let cb = connections_to_set(*b, &prev, flow_dag);
            cb.cmp(&ca).then_with(|| a.to_string().cmp(&b.to_string()))
        });
    }

    // ── Step 3: barycenter heuristic ────────────────────────────────────

    let mut best = layers.clone();
    let mut best_xings = total_crossings(&best, flow_dag);

    for _ in 0..24 {
        // Forward sweep: rank 1 → max
        for li in 1..width {
            barycenter_sort(&mut layers, li, Sweep::Forward, flow_dag);
        }
        // Backward sweep: max−1 → 0
        if width > 1 {
            for li in (0..width - 1).rev() {
                barycenter_sort(&mut layers, li, Sweep::Backward, flow_dag);
            }
        }

        let xings = total_crossings(&layers, flow_dag);
        if xings < best_xings {
            best_xings = xings;
            best = layers.clone();
        } else {
            // No improvement — stop.
            break;
        }
    }

    best
}

// ===========================================================================
// Internal — ranking helpers
// ===========================================================================

/// Longest-path forward relaxation from every node already in `ranks`.
///
/// Propagates `rank[v] = max(rank[u] + 1)` along forward edges in the DAG.
/// A per-node visit cap guards against cycles.
fn longest_path_forward(comp_set: &HashSet<Uuid>, dag: &FlowGraph, ranks: &mut HashMap<Uuid, i32>) {
    let mut queue: VecDeque<Uuid> = ranks.keys().copied().collect();
    let mut visits: HashMap<Uuid, usize> = HashMap::new();
    // Allow each node to be re-enqueued at most 2×N times (cycle guard).
    let cap = comp_set.len().max(1) * 2;

    while let Some(u) = queue.pop_front() {
        let ur = match ranks.get(&u).copied() {
            Some(r) => r,
            None => continue,
        };
        for &(v, _) in dag.successors(&u) {
            if !comp_set.contains(&v) {
                continue;
            }
            let candidate = ur + 1;
            if candidate > ranks.get(&v).copied().unwrap_or(i32::MIN) {
                ranks.insert(v, candidate);
                let cnt = visits.entry(v).or_default();
                if *cnt < cap {
                    *cnt += 1;
                    queue.push_back(v);
                }
            }
        }
    }
}

/// BFS outward from `hub` in both directions, assigning distance-from-hub as
/// rank (positive forward, negative backward).
fn bfs_outward(
    comp_set: &HashSet<Uuid>,
    hub: Uuid,
    dag: &FlowGraph,
    ranks: &mut HashMap<Uuid, i32>,
) {
    let mut queue = VecDeque::new();
    queue.push_back(hub);

    while let Some(u) = queue.pop_front() {
        let ur = ranks[&u];
        // Forward edges → rank + 1
        for &(v, _) in dag.successors(&u) {
            if comp_set.contains(&v) && !ranks.contains_key(&v) {
                ranks.insert(v, ur + 1);
                queue.push_back(v);
            }
        }
        // Backward edges → rank − 1
        for &(v, _) in dag.predecessors(&u) {
            if comp_set.contains(&v) && !ranks.contains_key(&v) {
                ranks.insert(v, ur - 1);
                queue.push_back(v);
            }
        }
    }
}

/// Return the component with the most total edges (in-degree + out-degree)
/// in the flow graph. Falls back to the first element.
fn most_connected_node(components: &[Uuid], dag: &FlowGraph) -> Uuid {
    components
        .iter()
        .copied()
        .max_by_key(|id| dag.successors(id).len() + dag.predecessors(id).len())
        .unwrap_or(components[0])
}

/// Remove gaps in the rank numbering while preserving relative order.
///
/// If ranks in use are `[-1, 0, 2, 4]` they become `[-1, 0, 1, 2]`.
fn compact_ranks(ranks: &mut HashMap<Uuid, i32>, main_components: &[Uuid]) {
    let comp_set: HashSet<Uuid> = main_components.iter().copied().collect();

    // Collect the distinct rank values that are actually occupied.
    let mut used: Vec<i32> = ranks
        .iter()
        .filter(|(id, _)| comp_set.contains(id))
        .map(|(_, &r)| r)
        .collect::<HashSet<i32>>()
        .into_iter()
        .collect();
    used.sort();

    if used.is_empty() {
        return;
    }

    // Map each old rank to a dense sequence starting at the original minimum.
    let base = used[0];
    let map: HashMap<i32, i32> = used
        .iter()
        .enumerate()
        .map(|(i, &old)| (old, base + i as i32))
        .collect();

    for (id, rank) in ranks.iter_mut() {
        if comp_set.contains(id) {
            if let Some(&new) = map.get(rank) {
                *rank = new;
            }
        }
    }
}

// ===========================================================================
// Internal — ordering helpers
// ===========================================================================

/// Sweep direction for the barycenter pass.
#[derive(Clone, Copy)]
enum Sweep {
    /// Sort by predecessor positions (rank r−1 → reference layer is to the left).
    Forward,
    /// Sort by successor positions (rank r+1 → reference layer is to the right).
    Backward,
}

/// Sort a single layer by barycenter position relative to its reference layer.
///
/// For a **Forward** sweep the reference is the layer to the left (rank r−1)
/// and we use `flow_dag.predecessors` to find neighbours. For a **Backward**
/// sweep the reference is the layer to the right (rank r+1) and we use
/// `flow_dag.successors`.
///
/// Components with no neighbours in the reference layer keep their current
/// position index as a fallback so they remain stable.
fn barycenter_sort(layers: &mut [Vec<Uuid>], li: usize, sweep: Sweep, dag: &FlowGraph) {
    let ref_li = match sweep {
        Sweep::Forward => {
            if li == 0 {
                return;
            }
            li - 1
        }
        Sweep::Backward => {
            if li + 1 >= layers.len() {
                return;
            }
            li + 1
        }
    };

    // Position lookup for the reference layer.
    let ref_pos: HashMap<Uuid, usize> = layers[ref_li]
        .iter()
        .enumerate()
        .map(|(i, &id)| (id, i))
        .collect();

    // Compute barycenters (average position of connected neighbours).
    let mut bary: HashMap<Uuid, f64> = HashMap::new();
    for &id in &layers[li] {
        let adj = match sweep {
            Sweep::Forward => dag.predecessors(&id),
            Sweep::Backward => dag.successors(&id),
        };
        let positions: Vec<f64> = adj
            .iter()
            .filter_map(|(n, _)| ref_pos.get(n).map(|&p| p as f64))
            .collect();
        if !positions.is_empty() {
            bary.insert(id, positions.iter().sum::<f64>() / positions.len() as f64);
        }
    }

    // Snapshot current indices as fallback for unconnected nodes.
    let cur: HashMap<Uuid, f64> = layers[li]
        .iter()
        .enumerate()
        .map(|(i, &id)| (id, i as f64))
        .collect();

    layers[li].sort_by(|a, b| {
        let ba = bary.get(a).copied().unwrap_or(cur[a]);
        let bb = bary.get(b).copied().unwrap_or(cur[b]);
        ba.partial_cmp(&bb)
            .unwrap_or(std::cmp::Ordering::Equal)
            .then_with(|| a.to_string().cmp(&b.to_string()))
    });
}

/// Count edges between `node` and any node in `targets` (both flow
/// directions count as a connection).
fn connections_to_set(node: Uuid, targets: &HashSet<Uuid>, dag: &FlowGraph) -> usize {
    let fwd = dag
        .successors(&node)
        .iter()
        .filter(|(v, _)| targets.contains(v))
        .count();
    let bwd = dag
        .predecessors(&node)
        .iter()
        .filter(|(v, _)| targets.contains(v))
        .count();
    fwd + bwd
}

/// Sum of edge crossings across all pairs of adjacent layers.
fn total_crossings(layers: &[Vec<Uuid>], dag: &FlowGraph) -> usize {
    (0..layers.len().saturating_sub(1))
        .map(|i| crossing_count(&layers[i], &layers[i + 1], dag))
        .sum()
}

/// Count crossings between two adjacent layers.
///
/// For every pair of edges `(u1,v1)` and `(u2,v2)` connecting the upper and
/// lower layers, a crossing exists when
/// `pos(u1) < pos(u2) ∧ pos(v1) > pos(v2)` (or the symmetric case).
///
/// Uses O(e²) pairwise comparison — perfectly fine for typical schematic sizes
/// (rarely more than a few dozen edges between adjacent ranks).
fn crossing_count(upper: &[Uuid], lower: &[Uuid], dag: &FlowGraph) -> usize {
    let upos: HashMap<Uuid, usize> = upper.iter().enumerate().map(|(i, &id)| (id, i)).collect();
    let lpos: HashMap<Uuid, usize> = lower.iter().enumerate().map(|(i, &id)| (id, i)).collect();

    // Collect every visual connection as (upper_index, lower_index).
    // Include both forward and backward flow-graph edges so that feedback
    // paths are also accounted for in the crossing metric.
    let mut edges: Vec<(usize, usize)> = Vec::new();
    let mut seen: HashSet<(usize, usize)> = HashSet::new();

    for &u in upper {
        let up = upos[&u];
        // Forward: u → v where v is in the lower layer.
        for &(v, _) in dag.successors(&u) {
            if let Some(&lp) = lpos.get(&v) {
                if seen.insert((up, lp)) {
                    edges.push((up, lp));
                }
            }
        }
        // Backward: w → u where w is in the lower layer (edge from lower to
        // upper still produces a visual wire between the two layers).
        for &(w, _) in dag.predecessors(&u) {
            if let Some(&lp) = lpos.get(&w) {
                if seen.insert((up, lp)) {
                    edges.push((up, lp));
                }
            }
        }
    }

    // Pairwise inversion count.
    let mut crossings: usize = 0;
    for i in 0..edges.len() {
        let (u1, v1) = edges[i];
        for j in (i + 1)..edges.len() {
            let (u2, v2) = edges[j];
            if (u1 < u2 && v1 > v2) || (u1 > u2 && v1 < v2) {
                crossings += 1;
            }
        }
    }
    crossings
}

// ===========================================================================
// Tests
// ===========================================================================

#[cfg(test)]
mod tests {
    use super::*;

    /// Helper: deterministic UUID from a single byte.
    fn id(n: u8) -> Uuid {
        Uuid::from_bytes([n, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
    }

    // ── assign_ranks ────────────────────────────────────────────────────

    #[test]
    fn empty_components_yields_empty_ranks() {
        let dag = FlowGraph::new();
        let roles = HashMap::new();
        assert!(assign_ranks(&[], &dag, &roles).is_empty());
    }

    #[test]
    fn single_component_gets_rank_zero() {
        let mut dag = FlowGraph::new();
        dag.add_node(id(1));
        let mut roles = HashMap::new();
        roles.insert(id(1), ComponentRole::Processor);

        let ranks = assign_ranks(&[id(1)], &dag, &roles);
        assert_eq!(ranks[&id(1)], 0);
    }

    #[test]
    fn linear_chain_source_to_sink() {
        //  S(0) → P(1) → P(2) → K(3)
        let net = id(99);
        let mut dag = FlowGraph::new();
        dag.add_edge(id(1), id(2), net);
        dag.add_edge(id(2), id(3), net);
        dag.add_edge(id(3), id(4), net);

        let mut roles = HashMap::new();
        roles.insert(id(1), ComponentRole::Source);
        roles.insert(id(2), ComponentRole::Processor);
        roles.insert(id(3), ComponentRole::Processor);
        roles.insert(id(4), ComponentRole::Sink);

        let comps = vec![id(1), id(2), id(3), id(4)];
        let ranks = assign_ranks(&comps, &dag, &roles);

        // Source at leftmost, Sink promoted to max.
        assert_eq!(ranks[&id(1)], 0);
        assert!(ranks[&id(2)] > 0);
        assert!(ranks[&id(3)] > ranks[&id(2)]);
        // Sink at max rank.
        let max_r = *ranks.values().max().unwrap();
        assert_eq!(ranks[&id(4)], max_r);
    }

    #[test]
    fn power_chain_gets_rank_minus_one() {
        let mut dag = FlowGraph::new();
        dag.add_node(id(1));
        dag.add_node(id(2));
        let mut roles = HashMap::new();
        roles.insert(id(1), ComponentRole::PowerChain);
        roles.insert(id(2), ComponentRole::Processor);

        let comps = vec![id(1), id(2)];
        let ranks = assign_ranks(&comps, &dag, &roles);
        assert_eq!(ranks[&id(1)], -1);
    }

    #[test]
    fn disconnected_components_default_to_zero() {
        let mut dag = FlowGraph::new();
        dag.add_node(id(1));
        // id(2) not even in the DAG.
        let mut roles = HashMap::new();
        roles.insert(id(1), ComponentRole::Processor);
        roles.insert(id(2), ComponentRole::Processor);

        let comps = vec![id(1), id(2)];
        let ranks = assign_ranks(&comps, &dag, &roles);
        assert_eq!(ranks[&id(1)], 0);
        assert_eq!(ranks[&id(2)], 0);
    }

    #[test]
    fn no_sources_uses_most_connected_hub() {
        // All Processors: A ← B → C
        let net = id(99);
        let mut dag = FlowGraph::new();
        dag.add_edge(id(2), id(1), net);
        dag.add_edge(id(2), id(3), net);

        let mut roles = HashMap::new();
        roles.insert(id(1), ComponentRole::Processor);
        roles.insert(id(2), ComponentRole::Processor);
        roles.insert(id(3), ComponentRole::Processor);

        let comps = vec![id(1), id(2), id(3)];
        let ranks = assign_ranks(&comps, &dag, &roles);

        // B (id(2)) has 2 edges → it becomes hub at rank 0.
        assert_eq!(ranks[&id(2)], 0);
        // A and C are one step away.
        assert!((ranks[&id(1)] - ranks[&id(2)]).abs() <= 1);
        assert!((ranks[&id(3)] - ranks[&id(2)]).abs() <= 1);
    }

    #[test]
    fn empty_ranks_are_compacted() {
        // Manually create a scenario with a gap:
        //   S(0) → P(1) → P(3)   [rank 2 would be empty without compaction]
        let net = id(99);
        let mut dag = FlowGraph::new();
        dag.add_edge(id(1), id(2), net);
        dag.add_edge(id(2), id(3), net);

        let mut roles = HashMap::new();
        roles.insert(id(1), ComponentRole::Source);
        roles.insert(id(2), ComponentRole::Processor);
        roles.insert(id(3), ComponentRole::Processor);

        let comps = vec![id(1), id(2), id(3)];
        let ranks = assign_ranks(&comps, &dag, &roles);

        // Should be contiguous: 0, 1, 2.
        let mut vals: Vec<i32> = ranks.values().copied().collect();
        vals.sort();
        vals.dedup();
        for i in 1..vals.len() {
            assert_eq!(vals[i], vals[i - 1] + 1, "ranks should be contiguous");
        }
    }

    // ── order_within_ranks ──────────────────────────────────────────────

    #[test]
    fn empty_ranks_yields_empty_order() {
        let ranks = HashMap::new();
        let dag = FlowGraph::new();
        let nm = HashMap::new();
        let nc = HashMap::new();
        assert!(order_within_ranks(&ranks, &dag, &nm, &nc).is_empty());
    }

    #[test]
    fn single_rank_single_component() {
        let mut ranks = HashMap::new();
        ranks.insert(id(1), 0);
        let dag = FlowGraph::new();
        let nm = HashMap::new();
        let nc = HashMap::new();

        let order = order_within_ranks(&ranks, &dag, &nm, &nc);
        assert_eq!(order.len(), 1);
        assert_eq!(order[0], vec![id(1)]);
    }

    #[test]
    fn two_ranks_no_crossings() {
        //  A(0)→C(1),  B(0)→D(1)
        let net = id(99);
        let mut dag = FlowGraph::new();
        dag.add_edge(id(1), id(3), net);
        dag.add_edge(id(2), id(4), net);

        let mut ranks = HashMap::new();
        ranks.insert(id(1), 0);
        ranks.insert(id(2), 0);
        ranks.insert(id(3), 1);
        ranks.insert(id(4), 1);

        let nm = HashMap::new();
        let nc = HashMap::new();
        let order = order_within_ranks(&ranks, &dag, &nm, &nc);

        assert_eq!(order.len(), 2);
        // After ordering, crossings should be 0.
        assert_eq!(total_crossings(&order, &dag), 0);
    }

    #[test]
    fn barycenter_reduces_crossings() {
        // Set up a crossing scenario:
        //   A(0)→D(1),  B(0)→C(1)
        // Initial alphabetical order [A,B] × [C,D] has 1 crossing.
        // Barycenter should reorder to eliminate it.
        let net = id(99);
        let mut dag = FlowGraph::new();
        dag.add_edge(id(1), id(4), net); // A→D
        dag.add_edge(id(2), id(3), net); // B→C

        let mut ranks = HashMap::new();
        ranks.insert(id(1), 0); // A
        ranks.insert(id(2), 0); // B
        ranks.insert(id(3), 1); // C
        ranks.insert(id(4), 1); // D

        let nm = HashMap::new();
        let nc = HashMap::new();
        let order = order_within_ranks(&ranks, &dag, &nm, &nc);

        assert_eq!(total_crossings(&order, &dag), 0);
    }

    #[test]
    fn negative_ranks_produce_correct_layer_count() {
        let mut ranks = HashMap::new();
        ranks.insert(id(1), -1);
        ranks.insert(id(2), 0);
        ranks.insert(id(3), 1);

        let dag = FlowGraph::new();
        let nm = HashMap::new();
        let nc = HashMap::new();
        let order = order_within_ranks(&ranks, &dag, &nm, &nc);

        assert_eq!(order.len(), 3);
        assert!(order[0].contains(&id(1))); // rank −1 at index 0
        assert!(order[1].contains(&id(2))); // rank  0 at index 1
        assert!(order[2].contains(&id(3))); // rank  1 at index 2
    }
}
