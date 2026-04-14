//! Import STEP/STL as named solids.

use std::path::PathBuf;
use volt_cad_core::Operation;

use super::{begin_job, execute, finish_job, Globals};
use crate::parse::expect_flag;

pub fn run(globals: &Globals, tail: &[String]) -> Result<(), String> {
    if tail.is_empty() {
        return Err("import: missing subcommand (step | stl)".to_string());
    }
    let mut ops = begin_job(globals);
    match tail[0].as_str() {
        "step" => {
            let id = expect_flag(&tail[1..], "--id")?;
            let path = PathBuf::from(expect_flag(&tail[1..], "--path")?);
            ops.push(Operation::ImportStep { id, path });
        }
        "stl" => {
            let id = expect_flag(&tail[1..], "--id")?;
            let path = PathBuf::from(expect_flag(&tail[1..], "--path")?);
            ops.push(Operation::ImportStl { id, path });
        }
        other => return Err(format!("import: unknown subcommand `{other}`")),
    }
    ops = finish_job(globals, ops);
    execute(ops)
}
