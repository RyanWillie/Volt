//! Shared types for the autoplace algorithm.

use std::collections::{HashMap, HashSet};
use uuid::Uuid;

// ---------------------------------------------------------------------------
// Net classification
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum NetClass {
    /// Power rail nets (VCC, GND, 3V3, etc.)
    Power,
    /// Normal signal nets
    Signal,
    /// Signal nets with > threshold connections — treated like power for wiring
    HighFanout,
}

// ---------------------------------------------------------------------------
// Component classification
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ComponentRole {
    /// Has only output signals on signal nets (voltage regulator output, oscillator, sensor)
    Source,
    /// Has only input signals on signal nets (LED, motor driver, display)
    Sink,
    /// Has both input and output signals (MCU, op-amp, logic gate)
    Processor,
    /// 2-pin passive in a signal path between two other components (series R, coupling C)
    SeriesPassive,
    /// 2-pin passive between power and ground (bypass/decoupling cap, bulk cap)
    BypassPassive,
    /// 2-pin passive between power and a signal net (pull-up/pull-down resistor)
    ShuntPassive,
    /// Connected only to power nets (battery, power switch, regulator with all-power pins)
    PowerChain,
}

// ---------------------------------------------------------------------------
// Companion (attached) components
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AttachmentType {
    /// Bypass/decoupling cap — place near parent, close to power pin
    Bypass,
    /// Pull-up resistor — place above the signal pin, vertical orientation
    PullUp,
    /// Pull-down resistor — place below the signal pin, vertical orientation
    PullDown,
}

#[derive(Debug, Clone)]
pub struct Companion {
    /// The companion component (bypass cap, pull-up, etc.)
    pub component: Uuid,
    /// The parent IC it's attached to
    pub parent: Uuid,
    /// How to attach it
    pub attachment: AttachmentType,
    /// The signal pin on the parent where this companion connects (for positioning)
    pub parent_pin_signal: Option<Uuid>,
}

// ---------------------------------------------------------------------------
// Pin side classification
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PinSide {
    /// Wire extends leftward from component body
    Left,
    /// Wire extends rightward from component body
    Right,
    /// Wire extends upward from component body
    Top,
    /// Wire extends downward from component body
    Bottom,
}

/// Classify which side of the symbol body a pin's wire extends from.
/// Pin rotation is the direction the stub goes TOWARD the body.
///  - rotation 0°   → stub goes right toward body → wire extends LEFT
///  - rotation 90°  → stub goes down toward body  → wire extends UP  (y-axis inverted in screen coords)
///  - rotation 180° → stub goes left toward body  → wire extends RIGHT
///  - rotation 270° → stub goes up toward body    → wire extends DOWN
pub fn classify_pin_side(pin_rotation_deg: f64) -> PinSide {
    let angle = normalize_angle(pin_rotation_deg);
    if angle < 45.0 || angle >= 315.0 {
        PinSide::Left
    } else if angle >= 45.0 && angle < 135.0 {
        PinSide::Top
    } else if angle >= 135.0 && angle < 225.0 {
        PinSide::Right
    } else {
        PinSide::Bottom
    }
}

/// Pin profile: which pins are on which side of the symbol at 0° rotation.
#[derive(Debug, Clone, Default)]
pub struct SymbolPinProfile {
    pub left: Vec<Uuid>,
    pub right: Vec<Uuid>,
    pub top: Vec<Uuid>,
    pub bottom: Vec<Uuid>,
}

// ---------------------------------------------------------------------------
// Directed graph for signal flow
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Default)]
pub struct FlowGraph {
    /// Adjacency list: node → list of (target, net_uuid)
    pub forward: HashMap<Uuid, Vec<(Uuid, Uuid)>>,
    /// Reverse adjacency: node → list of (source, net_uuid)
    pub backward: HashMap<Uuid, Vec<(Uuid, Uuid)>>,
    /// All nodes
    pub nodes: HashSet<Uuid>,
}

impl FlowGraph {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn add_node(&mut self, node: Uuid) {
        self.nodes.insert(node);
        self.forward.entry(node).or_default();
        self.backward.entry(node).or_default();
    }

    pub fn add_edge(&mut self, from: Uuid, to: Uuid, net: Uuid) {
        self.add_node(from);
        self.add_node(to);
        self.forward.entry(from).or_default().push((to, net));
        self.backward.entry(to).or_default().push((from, net));
    }

    /// Get forward (downstream) neighbors of a node.
    pub fn successors(&self, node: &Uuid) -> &[(Uuid, Uuid)] {
        self.forward.get(node).map(|v| v.as_slice()).unwrap_or(&[])
    }

    /// Get backward (upstream) neighbors of a node.
    pub fn predecessors(&self, node: &Uuid) -> &[(Uuid, Uuid)] {
        self.backward.get(node).map(|v| v.as_slice()).unwrap_or(&[])
    }
}

// ---------------------------------------------------------------------------
// Placement result
// ---------------------------------------------------------------------------

/// Final placement for a component: position + rotation.
#[derive(Debug, Clone, Copy)]
pub struct Placement {
    /// X position in grid units
    pub gx: f64,
    /// Y position in grid units
    pub gy: f64,
    /// Rotation in degrees (0, 90, 180, 270)
    pub rotation: f64,
    /// Whether the symbol is mirrored
    pub mirror: bool,
}

impl Placement {
    pub fn new(gx: f64, gy: f64, rotation: f64) -> Self {
        Self {
            gx,
            gy,
            rotation,
            mirror: false,
        }
    }
}

// ---------------------------------------------------------------------------
// Bounding box
// ---------------------------------------------------------------------------

/// Axis-aligned bounding box in world coordinates (mm).
#[derive(Debug, Clone, Copy)]
pub struct BBox {
    pub x0: f64,
    pub y0: f64,
    pub x1: f64,
    pub y1: f64,
}

impl BBox {
    pub fn new(x0: f64, y0: f64, x1: f64, y1: f64) -> Self {
        Self { x0, y0, x1, y1 }
    }

    pub fn overlaps(&self, other: &BBox) -> bool {
        self.x0 < other.x1 && self.x1 > other.x0 && self.y0 < other.y1 && self.y1 > other.y0
    }

    pub fn contains(&self, x: f64, y: f64) -> bool {
        x >= self.x0 && x <= self.x1 && y >= self.y0 && y <= self.y1
    }

    pub fn width(&self) -> f64 {
        self.x1 - self.x0
    }
    pub fn height(&self) -> f64 {
        self.y1 - self.y0
    }

    pub fn center_x(&self) -> f64 {
        (self.x0 + self.x1) / 2.0
    }
    pub fn center_y(&self) -> f64 {
        (self.y0 + self.y1) / 2.0
    }

    /// Expand the box by `margin` on each side.
    pub fn expand(&self, margin: f64) -> Self {
        Self {
            x0: self.x0 - margin,
            y0: self.y0 - margin,
            x1: self.x1 + margin,
            y1: self.y1 + margin,
        }
    }

    /// Merge two bboxes into one that contains both.
    pub fn union(&self, other: &BBox) -> Self {
        Self {
            x0: self.x0.min(other.x0),
            y0: self.y0.min(other.y0),
            x1: self.x1.max(other.x1),
            y1: self.y1.max(other.y1),
        }
    }
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

pub const GRID: f64 = 2.54;

/// High-fanout threshold: nets with more connections than this are treated as high-fanout.
pub const HIGH_FANOUT_THRESHOLD: usize = 4;

/// Normalize angle to [0, 360).
pub fn normalize_angle(angle: f64) -> f64 {
    let mut a = angle % 360.0;
    if a < 0.0 {
        a += 360.0;
    }
    a
}

/// Snap a value to the nearest grid point.
pub fn snap_to_grid(value: f64) -> f64 {
    (value / GRID).round() * GRID
}

/// Snap grid units to integer grid.
pub fn snap_grid_units(value: f64) -> f64 {
    value.round()
}

/// Check if a net name is a power net.
pub fn is_power_net_name(name: &str) -> bool {
    let u = name.to_uppercase();
    matches!(
        u.as_str(),
        "VCC"
            | "VDD"
            | "V+"
            | "VBUS"
            | "VBAT"
            | "VIN"
            | "VOUT"
            | "GND"
            | "VSS"
            | "V-"
            | "AGND"
            | "DGND"
            | "PGND"
            | "GNDA"
            | "3V3"
            | "+3V3"
            | "+3.3V"
            | "5V"
            | "+5V"
            | "+12V"
            | "+24V"
            | "BAT_RAW"
    )
}

/// Check if a power net name is a ground-family net.
pub fn is_ground_net_name(name: &str) -> bool {
    let u = name.to_uppercase();
    matches!(
        u.as_str(),
        "GND" | "VSS" | "V-" | "AGND" | "DGND" | "PGND" | "GNDA"
    )
}
