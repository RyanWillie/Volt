//! Export solids to STEP or STL.

use std::path::PathBuf;
use volt_cad_core::Operation;

use super::{begin_job, execute, finish_job, Globals};
use crate::parse::{expect_flag, parse_f64_flag};

pub fn run(globals: &Globals, tail: &[String]) -> Result<(), String> {
    if tail.is_empty() {
        return Err("export: missing subcommand (step | stl)".to_string());
    }
    let mut ops = begin_job(globals);
    match tail[0].as_str() {
        "step" => {
            let id = expect_flag(&tail[1..], "--id")?;
            let output = PathBuf::from(expect_flag(&tail[1..], "--output")?);
            ops.push(Operation::ExportStep { id, path: output });
        }
        "stl" => {
            let id = expect_flag(&tail[1..], "--id")?;
            let output = PathBuf::from(expect_flag(&tail[1..], "--output")?);
            let linear_deflection = parse_f64_flag(&tail[1..], "--linear-deflection", 0.2)?;
            ops.push(Operation::ExportStl {
                id,
                path: output,
                linear_deflection,
            });
        }
        other => return Err(format!("export: unknown subcommand `{other}`")),
    }
    ops = finish_job(globals, ops);
    execute(ops)
}
