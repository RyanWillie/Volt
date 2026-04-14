//! Mesh tessellation and STL export from meshes.

use std::path::PathBuf;
use volt_cad_core::Operation;

use super::{begin_job, execute, finish_job, Globals};
use crate::parse::{expect_flag, parse_f64_flag};

pub fn run(globals: &Globals, tail: &[String]) -> Result<(), String> {
    if tail.is_empty() {
        return Err("mesh: missing subcommand (from-shape | export-stl)".to_string());
    }
    let mut ops = begin_job(globals);
    match tail[0].as_str() {
        "from-shape" => {
            let mesh_id = expect_flag(&tail[1..], "--mesh-id")?;
            let solid_id = expect_flag(&tail[1..], "--solid-id")?;
            let linear_deflection =
                parse_f64_flag(&tail[1..], "--linear-deflection", 0.2)?;
            ops.push(Operation::MeshFromShape {
                mesh_id,
                solid_id,
                linear_deflection,
            });
        }
        "export-stl" => {
            let mesh_id = expect_flag(&tail[1..], "--mesh-id")?;
            let output = PathBuf::from(expect_flag(&tail[1..], "--output")?);
            ops.push(Operation::ExportMeshStl {
                mesh_id,
                path: output,
            });
        }
        other => return Err(format!("mesh: unknown subcommand `{other}`")),
    }
    ops = finish_job(globals, ops);
    execute(ops)
}
