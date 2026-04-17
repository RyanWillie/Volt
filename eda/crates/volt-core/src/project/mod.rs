//! Project types: metadata, circuit (netlist), schematics, and boards.
//!
//! A Volt project is a directory containing:
//! - `volt.json` — [`ProjectMetadata`]
//! - `circuit.json` — [`Circuit`]
//! - `schematics/<name>.json` — [`Schematic`] (one per sheet)
//! - `boards/<name>.json` — [`Board`] (one per board)
//! - `library/` — embedded library elements

use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::common::*;
use crate::library::StrokeText;

// ===========================================================================
// Project metadata
// ===========================================================================

/// Top-level project metadata, stored in `volt.json`.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ProjectMetadata {
    pub uuid: Uuid,
    pub name: String,
    #[serde(default)]
    pub author: String,
    #[serde(default = "default_version")]
    pub version: String,
    pub created: chrono::DateTime<chrono::Utc>,
    #[serde(default)]
    pub settings: ProjectSettings,
}

fn default_version() -> String {
    "v1".to_string()
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Default)]
pub struct ProjectSettings {
    #[serde(default)]
    pub locale_order: Vec<String>,
    #[serde(default)]
    pub norm_order: Vec<String>,
    #[serde(default)]
    pub custom_bom_attributes: Vec<String>,
    #[serde(default)]
    pub default_lock_component_assembly: bool,
}

// ===========================================================================
// Circuit (logical netlist)
// ===========================================================================

/// The logical circuit: nets, component instances, assembly variants.
/// Stored in `circuit.json`. Contains no visual/physical information.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Default)]
pub struct Circuit {
    #[serde(default)]
    pub assembly_variants: Vec<AssemblyVariant>,
    #[serde(default)]
    pub net_classes: Vec<NetClass>,
    #[serde(default)]
    pub nets: Vec<Net>,
    #[serde(default)]
    pub components: Vec<ComponentInstance>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct AssemblyVariant {
    pub uuid: Uuid,
    pub name: String,
    #[serde(default)]
    pub description: String,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct NetClass {
    pub uuid: Uuid,
    pub name: String,
    #[serde(default)]
    pub default_trace_width: TraceWidthConfig,
    #[serde(default)]
    pub default_via_drill_diameter: TraceWidthConfig,
    #[serde(default)]
    pub min_copper_copper_clearance: f64,
    #[serde(default)]
    pub min_copper_width: f64,
    #[serde(default)]
    pub min_via_drill_diameter: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, Default)]
#[serde(rename_all = "snake_case")]
pub enum TraceWidthConfig {
    #[default]
    Inherit,
    Value(f64),
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Net {
    pub uuid: Uuid,
    pub name: String,
    #[serde(default)]
    pub auto_name: bool,
    pub net_class: Uuid,
}

/// An instance of a library component placed in the circuit.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ComponentInstance {
    pub uuid: Uuid,
    /// Reference to library [`Component`](crate::library::Component) UUID.
    pub lib_component: Uuid,
    /// Which [`ComponentVariant`](crate::library::ComponentVariant) to use.
    pub lib_variant: Uuid,
    /// Designator, e.g. "R1", "U3".
    pub name: String,
    #[serde(default)]
    pub value: String,
    #[serde(default)]
    pub lock_assembly: bool,
    #[serde(default)]
    pub device_assignments: Vec<DeviceAssignment>,
    #[serde(default)]
    pub signal_connections: Vec<SignalConnection>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DeviceAssignment {
    /// Reference to library [`Device`](crate::library::Device) UUID.
    pub device: Uuid,
    /// Which [`AssemblyVariant`] this assignment applies to.
    pub variant: Uuid,
    pub part: DevicePartRef,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Default)]
pub struct DevicePartRef {
    #[serde(default)]
    pub mpn: String,
    #[serde(default)]
    pub manufacturer: String,
    #[serde(default)]
    pub attributes: Vec<PartAttribute>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct PartAttribute {
    pub key: String,
    #[serde(rename = "type")]
    pub type_name: String,
    #[serde(default)]
    pub unit: String,
    #[serde(default)]
    pub value: String,
}

/// Connects a component signal to a net (or leaves it unconnected).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SignalConnection {
    /// Component signal UUID.
    pub signal: Uuid,
    /// Net UUID. `None` if unconnected.
    pub net: Option<Uuid>,
}

// ===========================================================================
// Schematic
// ===========================================================================

/// A schematic sheet. Stored in `schematics/<name>.json`.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Schematic {
    pub uuid: Uuid,
    pub name: String,
    pub grid: Grid,
    #[serde(default)]
    pub symbols: Vec<SchematicSymbol>,
    #[serde(default)]
    pub net_segments: Vec<SchematicNetSegment>,
}

/// A symbol placement on a schematic sheet.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SchematicSymbol {
    pub uuid: Uuid,
    /// Reference to [`ComponentInstance`] UUID.
    pub component: Uuid,
    /// Reference to the [`Gate`](crate::library::Gate) UUID.
    pub lib_gate: Uuid,
    pub position: Position,
    #[serde(default)]
    pub rotation: Angle,
    #[serde(default)]
    pub mirror: bool,
    #[serde(default)]
    pub texts: Vec<SchematicText>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SchematicText {
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

/// A connected group of wires on a schematic, belonging to a single net.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SchematicNetSegment {
    pub uuid: Uuid,
    pub net: Uuid,
    #[serde(default)]
    pub junctions: Vec<Junction>,
    #[serde(default)]
    pub lines: Vec<SchematicLine>,
    #[serde(default)]
    pub labels: Vec<NetLabel>,
}

/// A junction point where multiple wires meet.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Junction {
    pub uuid: Uuid,
    pub position: Position,
}

/// A wire segment in a schematic net segment.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SchematicLine {
    pub uuid: Uuid,
    #[serde(default = "default_wire_width")]
    pub width: f64,
    pub from: LineEndpoint,
    pub to: LineEndpoint,
}

fn default_wire_width() -> f64 {
    0.15875
}

/// An endpoint of a schematic wire: either a symbol pin or a junction.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum LineEndpoint {
    Symbol { symbol: Uuid, pin: Uuid },
    Junction { junction: Uuid },
}

/// A net name label on the schematic.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct NetLabel {
    pub uuid: Uuid,
    pub position: Position,
    #[serde(default)]
    pub rotation: Angle,
    #[serde(default)]
    pub mirror: bool,
}

// ===========================================================================
// Board
// ===========================================================================

/// A PCB board layout. Stored in `boards/<name>.json`.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Board {
    pub uuid: Uuid,
    pub name: String,
    pub grid: Grid,
    #[serde(default)]
    pub inner_layers: u8,
    #[serde(default = "default_thickness")]
    pub thickness: f64,
    #[serde(default = "default_solder_resist")]
    pub solder_resist: SolderResistColor,
    #[serde(default = "default_silkscreen")]
    pub silkscreen: SilkscreenColor,
    #[serde(default = "default_font")]
    pub default_font: String,
    pub design_rules: DesignRules,
    pub drc_settings: DrcSettings,
    #[serde(default)]
    pub fabrication_output_settings: FabricationOutputSettings,
    #[serde(default)]
    pub devices: Vec<BoardDevice>,
    #[serde(default)]
    pub net_segments: Vec<BoardNetSegment>,
    #[serde(default)]
    pub planes: Vec<Plane>,
    #[serde(default)]
    pub polygons: Vec<BoardPolygon>,
    #[serde(default)]
    pub holes: Vec<BoardHole>,
}

fn default_thickness() -> f64 {
    1.6
}
fn default_solder_resist() -> SolderResistColor {
    SolderResistColor::Green
}
fn default_silkscreen() -> SilkscreenColor {
    SilkscreenColor::White
}
fn default_font() -> String {
    "newstroke.bene".to_string()
}

// ---------------------------------------------------------------------------
// Design rules & DRC
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DesignRules {
    #[serde(default = "dr_trace_width")]
    pub default_trace_width: f64,
    #[serde(default = "dr_via_drill")]
    pub default_via_drill_diameter: f64,
    #[serde(default = "dr_stopmask_via")]
    pub stopmask_max_via_drill_diameter: f64,
    #[serde(default)]
    pub stopmask_clearance_ratio: f64,
    #[serde(default = "dr_01")]
    pub stopmask_clearance_min: f64,
    #[serde(default = "dr_01")]
    pub stopmask_clearance_max: f64,
    #[serde(default = "dr_01")]
    pub solderpaste_clearance_ratio: f64,
    #[serde(default)]
    pub solderpaste_clearance_min: f64,
    #[serde(default = "dr_10")]
    pub solderpaste_clearance_max: f64,
    #[serde(default = "dr_025")]
    pub pad_annular_ring_ratio: f64,
    #[serde(default = "dr_025")]
    pub pad_annular_ring_min: f64,
    #[serde(default = "dr_20")]
    pub pad_annular_ring_max: f64,
    #[serde(default = "dr_025")]
    pub via_annular_ring_ratio: f64,
    #[serde(default = "dr_02")]
    pub via_annular_ring_min: f64,
    #[serde(default = "dr_20")]
    pub via_annular_ring_max: f64,
}

fn dr_trace_width() -> f64 { 0.5 }
fn dr_via_drill() -> f64 { 0.3 }
fn dr_stopmask_via() -> f64 { 0.5 }
fn dr_01() -> f64 { 0.1 }
fn dr_10() -> f64 { 1.0 }
fn dr_02() -> f64 { 0.2 }
fn dr_025() -> f64 { 0.25 }
fn dr_20() -> f64 { 2.0 }

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DrcSettings {
    #[serde(default = "dr_02")]
    pub min_copper_copper_clearance: f64,
    #[serde(default = "dr_03")]
    pub min_copper_board_clearance: f64,
    #[serde(default = "dr_02")]
    pub min_copper_npth_clearance: f64,
    #[serde(default = "dr_035")]
    pub min_drill_drill_clearance: f64,
    #[serde(default = "dr_05")]
    pub min_drill_board_clearance: f64,
    #[serde(default = "dr_0127")]
    pub min_silkscreen_stopmask_clearance: f64,
    #[serde(default = "dr_02")]
    pub min_copper_width: f64,
    #[serde(default = "dr_015")]
    pub min_annular_ring: f64,
    #[serde(default = "dr_025")]
    pub min_npth_drill_diameter: f64,
    #[serde(default = "dr_025")]
    pub min_pth_drill_diameter: f64,
    #[serde(default = "dr_10")]
    pub min_npth_slot_width: f64,
    #[serde(default = "dr_07")]
    pub min_pth_slot_width: f64,
    #[serde(default = "dr_05")]
    pub max_tented_via_drill_diameter: f64,
    #[serde(default = "dr_015")]
    pub min_silkscreen_width: f64,
    #[serde(default = "dr_08")]
    pub min_silkscreen_text_height: f64,
    #[serde(default = "dr_20")]
    pub min_outline_tool_diameter: f64,
    #[serde(default)]
    pub blind_vias_allowed: bool,
    #[serde(default)]
    pub buried_vias_allowed: bool,
}

fn dr_03() -> f64 { 0.3 }
fn dr_035() -> f64 { 0.35 }
fn dr_05() -> f64 { 0.5 }
fn dr_0127() -> f64 { 0.127 }
fn dr_015() -> f64 { 0.15 }
fn dr_07() -> f64 { 0.7 }
fn dr_08() -> f64 { 0.8 }

// ---------------------------------------------------------------------------
// Fabrication output
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct FabricationOutputSettings {
    #[serde(default = "fab_base_path")]
    pub base_path: String,
    #[serde(default = "fab_outlines")]
    pub outlines_suffix: String,
    #[serde(default = "fab_copper_top")]
    pub copper_top_suffix: String,
    #[serde(default = "fab_copper_inner")]
    pub copper_inner_suffix: String,
    #[serde(default = "fab_copper_bot")]
    pub copper_bot_suffix: String,
    #[serde(default = "fab_soldermask_top")]
    pub soldermask_top_suffix: String,
    #[serde(default = "fab_soldermask_bot")]
    pub soldermask_bot_suffix: String,
    #[serde(default = "fab_silkscreen_top")]
    pub silkscreen_top_suffix: String,
    #[serde(default = "fab_silkscreen_bot")]
    pub silkscreen_bot_suffix: String,
    #[serde(default = "fab_drills_pth")]
    pub drills_pth_suffix: String,
    #[serde(default = "fab_drills_npth")]
    pub drills_npth_suffix: String,
    #[serde(default = "fab_drills_merged")]
    pub drills_merged_suffix: String,
    #[serde(default)]
    pub merge_drills: bool,
    #[serde(default = "fab_paste_top")]
    pub paste_top_suffix: String,
    #[serde(default = "fab_paste_bot")]
    pub paste_bot_suffix: String,
}

impl Default for FabricationOutputSettings {
    fn default() -> Self {
        serde_json::from_str("{}").unwrap()
    }
}

fn fab_base_path() -> String { "./output/{{VERSION}}/gerber/{{PROJECT}}".into() }
fn fab_outlines() -> String { "_OUTLINES.gbr".into() }
fn fab_copper_top() -> String { "_COPPER-TOP.gbr".into() }
fn fab_copper_inner() -> String { "_COPPER-IN{{CU_LAYER}}.gbr".into() }
fn fab_copper_bot() -> String { "_COPPER-BOTTOM.gbr".into() }
fn fab_soldermask_top() -> String { "_SOLDERMASK-TOP.gbr".into() }
fn fab_soldermask_bot() -> String { "_SOLDERMASK-BOTTOM.gbr".into() }
fn fab_silkscreen_top() -> String { "_SILKSCREEN-TOP.gbr".into() }
fn fab_silkscreen_bot() -> String { "_SILKSCREEN-BOTTOM.gbr".into() }
fn fab_drills_pth() -> String { "_DRILLS-PTH.drl".into() }
fn fab_drills_npth() -> String { "_DRILLS-NPTH.drl".into() }
fn fab_drills_merged() -> String { "_DRILLS.drl".into() }
fn fab_paste_top() -> String { "_PASTE-TOP.gbr".into() }
fn fab_paste_bot() -> String { "_PASTE-BOTTOM.gbr".into() }

// ---------------------------------------------------------------------------
// Board elements
// ---------------------------------------------------------------------------

/// A device placed on the board.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BoardDevice {
    /// Reference to [`ComponentInstance`] UUID.
    pub component: Uuid,
    /// Reference to library [`Device`](crate::library::Device) UUID.
    pub lib_device: Uuid,
    /// Reference to [`Footprint`](crate::library::Footprint) UUID.
    pub lib_footprint: Uuid,
    pub position: Position,
    #[serde(default)]
    pub rotation: Angle,
    #[serde(default)]
    pub flip: bool,
    #[serde(default)]
    pub lock: bool,
    #[serde(default)]
    pub texts: Vec<StrokeText>,
}

/// A net segment on the board: traces, vias, junctions, and standalone pads.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BoardNetSegment {
    pub uuid: Uuid,
    /// Net UUID. `None` for unconnected standalone pads.
    pub net: Option<Uuid>,
    #[serde(default)]
    pub traces: Vec<Trace>,
    #[serde(default)]
    pub vias: Vec<Via>,
    #[serde(default)]
    pub junctions: Vec<Junction>,
    #[serde(default)]
    pub pads: Vec<BoardPad>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Trace {
    pub uuid: Uuid,
    pub layer: Layer,
    pub width: f64,
    pub from: TraceEndpoint,
    pub to: TraceEndpoint,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TraceEndpoint {
    Device { device: Uuid, pad: Uuid },
    Via { via: Uuid },
    Junction { junction: Uuid },
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Via {
    pub uuid: Uuid,
    pub from_layer: Layer,
    pub to_layer: Layer,
    pub position: Position,
    pub drill: f64,
    #[serde(default = "default_via_size")]
    pub size: ViaSize,
    #[serde(default)]
    pub exposure: ViaExposure,
}

fn default_via_size() -> ViaSize {
    ViaSize::Auto
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, Default)]
#[serde(rename_all = "snake_case")]
pub enum ViaSize {
    #[default]
    Auto,
    Manual(f64),
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, Default)]
#[serde(rename_all = "snake_case")]
pub enum ViaExposure {
    #[default]
    Auto,
    Off,
    Manual(f64),
}

/// A standalone pad on the board (not part of a device).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BoardPad {
    pub uuid: Uuid,
    pub side: PadSide,
    pub shape: PadShape,
    pub position: Position,
    #[serde(default)]
    pub rotation: Angle,
    pub size_width: f64,
    pub size_height: f64,
    #[serde(default)]
    pub radius: f64,
    #[serde(default = "default_stop_mask_config")]
    pub stop_mask: StopMaskConfig,
    #[serde(default = "default_solder_paste_config")]
    pub solder_paste: SolderPasteConfig,
    #[serde(default)]
    pub clearance: f64,
    #[serde(default = "default_board_pad_function")]
    pub function: PadFunction,
    #[serde(default)]
    pub lock: bool,
    #[serde(default)]
    pub holes: Vec<BoardPadHole>,
}

fn default_stop_mask_config() -> StopMaskConfig { StopMaskConfig::Auto }
fn default_solder_paste_config() -> SolderPasteConfig { SolderPasteConfig::Off }
fn default_board_pad_function() -> PadFunction { PadFunction::Standard }

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BoardPadHole {
    pub uuid: Uuid,
    pub diameter: f64,
    #[serde(default)]
    pub path: Vec<Vertex>,
}

/// A copper pour / plane.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Plane {
    pub uuid: Uuid,
    pub layer: Layer,
    pub net: Uuid,
    #[serde(default)]
    pub priority: i32,
    pub min_width: f64,
    pub min_copper_clearance: f64,
    pub min_board_clearance: f64,
    pub min_npth_clearance: f64,
    #[serde(default = "default_connect_style")]
    pub connect_style: ConnectStyle,
    #[serde(default = "default_thermal")]
    pub thermal_gap: f64,
    #[serde(default = "default_thermal")]
    pub thermal_spoke: f64,
    #[serde(default)]
    pub keep_islands: bool,
    #[serde(default)]
    pub lock: bool,
    pub vertices: Vec<Vertex>,
    /// Computed fill fragments. Empty until refill is run.
    #[serde(default)]
    pub fragments: Vec<PlaneFragment>,
}

fn default_connect_style() -> ConnectStyle { ConnectStyle::Solid }
fn default_thermal() -> f64 { 0.3 }

/// A computed fill fragment from a plane refill pass.
/// First contour is the outer boundary; subsequent contours are holes.
/// Each contour is a closed ring of positions.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Default)]
pub struct PlaneFragment {
    pub contours: Vec<Vec<Position>>,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, Default)]
#[serde(rename_all = "snake_case")]
pub enum ConnectStyle {
    #[default]
    Solid,
    Thermal,
    None,
}

/// A polygon on the board (outlines, cutouts, documentation, etc.).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BoardPolygon {
    pub uuid: Uuid,
    pub layer: Layer,
    #[serde(default)]
    pub width: f64,
    #[serde(default)]
    pub fill: bool,
    #[serde(default)]
    pub grab_area: bool,
    #[serde(default)]
    pub lock: bool,
    pub vertices: Vec<Vertex>,
}

/// A non-plated hole in the board.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BoardHole {
    pub uuid: Uuid,
    pub diameter: f64,
    #[serde(default = "default_stop_mask_config")]
    pub stop_mask: StopMaskConfig,
    #[serde(default)]
    pub lock: bool,
    pub path: Vec<Vertex>,
}
