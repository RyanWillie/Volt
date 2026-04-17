//! Core data model for the Volt EDA engine.
//!
//! All types derive `Serialize` and `Deserialize` — the JSON representation
//! produced by `serde_json::to_string_pretty()` *is* the file format.
//!
//! # Module structure
//!
//! - [`common`] — Shared types: lengths, positions, layers, UUIDs
//! - [`library`] — Library element types: Symbol, Component, Package, Device
//! - [`project`] — Project types: Metadata, Circuit, Schematic, Board

pub mod common;
pub mod library;
pub mod migration;
pub mod project;
pub mod split;
