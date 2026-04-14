//! Shared types used across the Volt EDA data model.
//!
//! All lengths are in millimeters. Angles are in degrees.

use serde::{Deserialize, Serialize};
use std::fmt;
use std::ops::{Add, Neg, Sub};

// ---------------------------------------------------------------------------
// Geometry primitives
// ---------------------------------------------------------------------------

/// A length in millimeters.
#[derive(Debug, Clone, Copy, PartialEq, PartialOrd, Serialize, Deserialize, Default)]
pub struct Length(pub f64);

impl fmt::Display for Length {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}mm", self.0)
    }
}

impl Add for Length {
    type Output = Self;
    fn add(self, rhs: Self) -> Self {
        Length(self.0 + rhs.0)
    }
}

impl Sub for Length {
    type Output = Self;
    fn sub(self, rhs: Self) -> Self {
        Length(self.0 - rhs.0)
    }
}

impl Neg for Length {
    type Output = Self;
    fn neg(self) -> Self {
        Length(-self.0)
    }
}

/// An angle in degrees.
#[derive(Debug, Clone, Copy, PartialEq, PartialOrd, Serialize, Deserialize, Default)]
pub struct Angle(pub f64);

impl fmt::Display for Angle {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}°", self.0)
    }
}

/// A 2D position in millimeters.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, Default)]
pub struct Position {
    pub x: f64,
    pub y: f64,
}

impl Position {
    pub fn new(x: f64, y: f64) -> Self {
        Self { x, y }
    }
}

/// A vertex in a polygon or path. When `angle` is 0.0, the segment from the
/// previous vertex to this one is a straight line. Non-zero angles produce arcs.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Vertex {
    pub position: Position,
    #[serde(default)]
    pub angle: Angle,
}

// ---------------------------------------------------------------------------
// Alignment
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HAlign {
    Left,
    Center,
    Right,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum VAlign {
    Top,
    Center,
    Bottom,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Alignment {
    pub h: HAlign,
    pub v: VAlign,
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

/// All available layers in the Volt EDA system.
///
/// Schematic layers are prefixed `Sch`, board layers use their physical name.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Layer {
    // -- Schematic layers --
    SchOutlines,
    SchNames,
    SchValues,
    SchDocumentation,
    SchComments,
    SchGuide,
    SchHiddenGrabAreas,
    SchSheetFrames,

    // -- Board copper layers --
    TopCopper,
    InnerCopper(u8),
    BottomCopper,

    // -- Board top/bottom paired layers --
    TopLegend,
    BottomLegend,
    TopDocumentation,
    BottomDocumentation,
    TopNames,
    BottomNames,
    TopValues,
    BottomValues,
    TopStopMask,
    BottomStopMask,
    TopSolderPaste,
    BottomSolderPaste,
    TopCourtyard,
    BottomCourtyard,
    TopPackageOutlines,
    BottomPackageOutlines,
    TopHiddenGrabAreas,
    BottomHiddenGrabAreas,
    TopGlue,
    BottomGlue,
    TopFinish,
    BottomFinish,

    // -- Board structural layers --
    BoardOutlines,
    BoardCutouts,
    PlatedBoardCutouts,
    Measures,
    Alignment,

    // -- Board annotation layers --
    BrdDocumentation,
    BrdComments,
    BrdGuide,
    BrdSheetFrames,
}

// ---------------------------------------------------------------------------
// Pad types
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PadShape {
    RoundRect,
    Round,
    Custom,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PadSide {
    Top,
    Bottom,
    ThroughHole,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PadFunction {
    Standard,
    ThermalPad,
    Bga,
    Edge,
    Unspecified,
}

// ---------------------------------------------------------------------------
// Mask / paste configuration
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum StopMaskConfig {
    Auto,
    Manual(f64),
    Off,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SolderPasteConfig {
    Auto,
    Manual(f64),
    Off,
}

// ---------------------------------------------------------------------------
// Board appearance
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SolderResistColor {
    Green,
    Red,
    Blue,
    Black,
    White,
    Yellow,
    Purple,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SilkscreenColor {
    White,
    Black,
    Yellow,
}

// ---------------------------------------------------------------------------
// Signal / component roles
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SignalRole {
    Passive,
    Power,
    Input,
    Output,
    Bidirectional,
    OpenDrain,
    OpenCollector,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, Default)]
#[serde(rename_all = "snake_case")]
pub enum AssemblyType {
    #[default]
    Tht,
    Smt,
    Mixed,
    Auto,
    None,
}

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum GridUnit {
    Millimeters,
    Mils,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Grid {
    pub interval: f64,
    pub unit: GridUnit,
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Generate a new random v4 UUID.
pub fn new_uuid() -> uuid::Uuid {
    uuid::Uuid::new_v4()
}
