//! Core types for CAD job descriptions and engine results.
//!
//! The CLI and the FreeCAD driver exchange JSON using these shapes.

use serde::{Deserialize, Serialize};
use std::path::PathBuf;

/// Top-level payload written for the FreeCAD driver (`volt_cad_driver.py`).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CadJob {
    pub version: u32,
    pub operations: Vec<Operation>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "op", rename_all = "snake_case")]
pub enum Operation {
    /// Create a new document (closes previous active document in the driver).
    DocumentNew {
        #[serde(default = "default_document_name")]
        name: String,
    },
    /// Open an existing FreeCAD document (`.FCStd`).
    DocumentOpen { path: PathBuf },
    /// Save the active document.
    DocumentSave { path: PathBuf },

    SolidBox {
        id: String,
        #[serde(default)]
        origin: [f64; 3],
        size: [f64; 3],
    },
    SolidCylinder {
        id: String,
        #[serde(default)]
        base: [f64; 3],
        #[serde(default = "default_axis_z")]
        axis: [f64; 3],
        radius: f64,
        height: f64,
    },
    SolidSphere {
        id: String,
        #[serde(default)]
        center: [f64; 3],
        radius: f64,
    },
    SolidCone {
        id: String,
        #[serde(default)]
        base: [f64; 3],
        #[serde(default = "default_axis_z")]
        axis: [f64; 3],
        r1: f64,
        r2: f64,
        height: f64,
    },

    TransformTranslate {
        id: String,
        dx: f64,
        dy: f64,
        dz: f64,
    },
    TransformRotate {
        id: String,
        #[serde(default)]
        origin: [f64; 3],
        axis: [f64; 3],
        angle_deg: f64,
    },
    TransformScale {
        id: String,
        #[serde(default = "default_scale_uniform")]
        uniform: f64,
        #[serde(default)]
        center: [f64; 3],
    },

    BooleanUnion {
        out: String,
        a: String,
        b: String,
    },
    BooleanCut {
        out: String,
        base: String,
        tool: String,
    },
    BooleanCommon {
        out: String,
        a: String,
        b: String,
    },

    ImportStep {
        id: String,
        path: PathBuf,
    },
    ImportStl {
        id: String,
        path: PathBuf,
    },
    ExportStep {
        id: String,
        path: PathBuf,
    },
    ExportStl {
        id: String,
        path: PathBuf,
        #[serde(default = "default_mesh_linear_deflection")]
        linear_deflection: f64,
    },

    MeshFromShape {
        mesh_id: String,
        solid_id: String,
        #[serde(default = "default_mesh_linear_deflection")]
        linear_deflection: f64,
    },
    ExportMeshStl {
        mesh_id: String,
        path: PathBuf,
    },

    CheckClearance {
        a: String,
        b: String,
    },
    CheckBoundingBoxes {
        a: String,
        b: String,
    },
}

fn default_document_name() -> String {
    "Unnamed".to_string()
}

fn default_axis_z() -> [f64; 3] {
    [0.0, 0.0, 1.0]
}

fn default_scale_uniform() -> f64 {
    1.0
}

fn default_mesh_linear_deflection() -> f64 {
    0.2
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EngineResult {
    pub status: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
    #[serde(default, skip_serializing_if = "serde_json::Map::is_empty")]
    pub data: serde_json::Map<String, serde_json::Value>,
}

impl EngineResult {
    pub fn ok() -> Self {
        Self {
            status: "ok".to_string(),
            message: None,
            data: serde_json::Map::new(),
        }
    }

    pub fn err(message: impl Into<String>) -> Self {
        Self {
            status: "error".to_string(),
            message: Some(message.into()),
            data: serde_json::Map::new(),
        }
    }

    pub fn with_data(mut self, key: impl Into<String>, value: serde_json::Value) -> Self {
        self.data.insert(key.into(), value);
        self
    }
}

#[derive(Debug, thiserror::Error)]
pub enum CadCoreError {
    #[error("invalid job: {0}")]
    InvalidJob(String),
    #[error("json error: {0}")]
    Json(#[from] serde_json::Error),
}
