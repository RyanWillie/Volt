//! Library element types: Symbol, Component, Package, Device.
//!
//! The 4-layer hierarchy: Symbol → Component → Package → Device.
//! - **Symbol** — schematic visual representation (pins + polygons + text)
//! - **Component** — abstract electrical part (signals, gates, variants)
//! - **Package** — physical footprint (pads, footprint variants, 3D models)
//! - **Device** — binds a Component to a Package with a specific pinout

use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::common::*;

// ===========================================================================
// Shared metadata
// ===========================================================================

/// Metadata common to all library elements.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct LibraryMeta {
    pub uuid: Uuid,
    pub name: String,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub keywords: String,
    #[serde(default)]
    pub author: String,
    #[serde(default)]
    pub version: String,
    pub created: chrono::DateTime<chrono::Utc>,
    #[serde(default)]
    pub deprecated: bool,
    #[serde(default)]
    pub category: Option<Uuid>,
}

// ===========================================================================
// Shared polygon / text types (used by symbols, footprints, boards)
// ===========================================================================

/// A polygon on a specific layer, used in symbols, footprints, and boards.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Polygon {
    pub uuid: Uuid,
    pub layer: Layer,
    pub width: f64,
    #[serde(default)]
    pub fill: bool,
    #[serde(default)]
    pub grab_area: bool,
    pub vertices: Vec<Vertex>,
}

/// A text element rendered with strokes (used in footprints and boards).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct StrokeText {
    pub uuid: Uuid,
    pub layer: Layer,
    pub value: String,
    pub position: Position,
    #[serde(default)]
    pub rotation: Angle,
    pub height: f64,
    pub stroke_width: f64,
    /// `None` = auto spacing.
    pub letter_spacing: Option<f64>,
    /// `None` = auto spacing.
    pub line_spacing: Option<f64>,
    pub align: Alignment,
    #[serde(default)]
    pub mirror: bool,
    #[serde(default = "default_true")]
    pub auto_rotate: bool,
    #[serde(default)]
    pub lock: bool,
}

fn default_true() -> bool {
    true
}

// ===========================================================================
// Symbol
// ===========================================================================

/// A schematic symbol: the graphical representation of a component (or gate).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Symbol {
    pub meta: LibraryMeta,
    pub pins: Vec<SymbolPin>,
    #[serde(default)]
    pub polygons: Vec<Polygon>,
    #[serde(default)]
    pub texts: Vec<SymbolText>,
    #[serde(default = "default_grid_interval")]
    pub grid_interval: f64,
}

fn default_grid_interval() -> f64 {
    2.54
}

/// A pin on a schematic symbol.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SymbolPin {
    pub uuid: Uuid,
    pub name: String,
    pub position: Position,
    #[serde(default)]
    pub rotation: Angle,
    pub length: f64,
    #[serde(default)]
    pub name_position: Position,
    #[serde(default)]
    pub name_rotation: Angle,
    #[serde(default = "default_name_height")]
    pub name_height: f64,
    #[serde(default = "default_name_align")]
    pub name_align: Alignment,
}

fn default_name_height() -> f64 {
    2.5
}

fn default_name_align() -> Alignment {
    Alignment {
        h: HAlign::Left,
        v: VAlign::Center,
    }
}

/// A text element on a schematic symbol (simpler than StrokeText).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SymbolText {
    pub uuid: Uuid,
    pub layer: Layer,
    pub value: String,
    pub position: Position,
    #[serde(default)]
    pub rotation: Angle,
    pub height: f64,
    pub align: Alignment,
    #[serde(default)]
    pub lock: bool,
}

// ===========================================================================
// Component
// ===========================================================================

/// An abstract electrical component (generic, reusable).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Component {
    pub meta: LibraryMeta,
    pub prefix: String,
    #[serde(default)]
    pub default_value: String,
    #[serde(default)]
    pub schematic_only: bool,
    #[serde(default)]
    pub attributes: Vec<ComponentAttribute>,
    pub signals: Vec<Signal>,
    pub variants: Vec<ComponentVariant>,
}

/// A typed attribute on a component or part (e.g. resistance, capacitance).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ComponentAttribute {
    pub key: String,
    #[serde(rename = "type")]
    pub type_name: String,
    #[serde(default)]
    pub unit: String,
    #[serde(default)]
    pub value: String,
}

/// An electrical signal of a component.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Signal {
    pub uuid: Uuid,
    pub name: String,
    pub role: SignalRole,
    #[serde(default)]
    pub required: bool,
    #[serde(default)]
    pub negated: bool,
    #[serde(default)]
    pub clock: bool,
    #[serde(default)]
    pub forced_net: String,
}

/// A norm-specific variant of a component (e.g. IEC vs IEEE symbols).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ComponentVariant {
    pub uuid: Uuid,
    #[serde(default)]
    pub norm: String,
    pub name: String,
    #[serde(default)]
    pub description: String,
    pub gates: Vec<Gate>,
}

/// A gate within a component variant, referencing a symbol.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Gate {
    pub uuid: Uuid,
    /// Reference to a [`Symbol`] UUID.
    pub symbol: Uuid,
    #[serde(default)]
    pub position: Position,
    #[serde(default)]
    pub rotation: Angle,
    #[serde(default = "default_true")]
    pub required: bool,
    #[serde(default)]
    pub suffix: String,
    pub pin_mappings: Vec<PinMapping>,
}

/// Maps a symbol pin to a component signal.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct PinMapping {
    /// Symbol pin UUID.
    pub pin: Uuid,
    /// Component signal UUID.
    pub signal: Uuid,
}

// ===========================================================================
// Package
// ===========================================================================

/// A physical package containing one or more footprint variants.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Package {
    pub meta: LibraryMeta,
    #[serde(default)]
    pub assembly_type: AssemblyType,
    #[serde(default = "default_grid_interval")]
    pub grid_interval: f64,
    #[serde(default = "default_min_copper_clearance")]
    pub min_copper_clearance: f64,
    pub pads: Vec<PackagePad>,
    pub footprints: Vec<Footprint>,
}

fn default_min_copper_clearance() -> f64 {
    0.2
}

/// A named pad in a package (the logical pad, not its physical placement).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct PackagePad {
    pub uuid: Uuid,
    pub name: String,
}

/// A footprint variant within a package.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Footprint {
    pub uuid: Uuid,
    pub name: String,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub model_position: Position3D,
    #[serde(default)]
    pub model_rotation: Position3D,
    pub pads: Vec<FootprintPad>,
    #[serde(default)]
    pub polygons: Vec<Polygon>,
    #[serde(default)]
    pub texts: Vec<StrokeText>,
}

/// A 3D position/rotation (x, y, z).
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, Default)]
pub struct Position3D {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

/// A pad placement within a footprint.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct FootprintPad {
    pub uuid: Uuid,
    /// Reference to [`PackagePad`] UUID.
    pub package_pad: Uuid,
    pub side: PadSide,
    pub shape: PadShape,
    pub position: Position,
    #[serde(default)]
    pub rotation: Angle,
    pub width: f64,
    pub height: f64,
    #[serde(default)]
    pub radius: f64,
    #[serde(default = "default_stop_mask")]
    pub stop_mask: StopMaskConfig,
    #[serde(default = "default_solder_paste")]
    pub solder_paste: SolderPasteConfig,
    #[serde(default)]
    pub clearance: f64,
    #[serde(default = "default_pad_function")]
    pub function: PadFunction,
    #[serde(default)]
    pub holes: Vec<PadHole>,
}

fn default_stop_mask() -> StopMaskConfig {
    StopMaskConfig::Auto
}

fn default_solder_paste() -> SolderPasteConfig {
    SolderPasteConfig::Auto
}

fn default_pad_function() -> PadFunction {
    PadFunction::Unspecified
}

/// A drill hole within a pad (for THT or via-in-pad).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct PadHole {
    pub uuid: Uuid,
    pub diameter: f64,
    #[serde(default)]
    pub path: Vec<Vertex>,
}

// ===========================================================================
// Device
// ===========================================================================

/// A device: binds a [`Component`] to a [`Package`] with a specific pinout.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Device {
    pub meta: LibraryMeta,
    /// Reference to a [`Component`] UUID.
    pub component: Uuid,
    /// Reference to a [`Package`] UUID.
    pub package: Uuid,
    pub pad_mappings: Vec<DevicePadMapping>,
    #[serde(default)]
    pub parts: Vec<DevicePart>,
}

/// Maps a package pad to a component signal.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DevicePadMapping {
    /// Package pad UUID.
    pub pad: Uuid,
    /// Component signal UUID.
    pub signal: Uuid,
    #[serde(default)]
    pub optional: bool,
}

/// A specific orderable part (MPN + manufacturer).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DevicePart {
    pub mpn: String,
    #[serde(default)]
    pub manufacturer: String,
    #[serde(default)]
    pub attributes: Vec<ComponentAttribute>,
}
