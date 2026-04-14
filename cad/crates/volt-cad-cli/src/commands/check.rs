//! Clearance and bounding-box checks.

use volt_cad_core::Operation;

use super::{begin_job, execute, finish_job, Globals};
use crate::parse::expect_flag;

pub fn run(globals: &Globals, tail: &[String]) -> Result<(), String> {
    if tail.is_empty() {
        return Err("check: missing subcommand (clearance | bbox)".to_string());
    }
    let mut ops = begin_job(globals);
    match tail[0].as_str() {
        "clearance" => {
            let a = expect_flag(&tail[1..], "--a")?;
            let b = expect_flag(&tail[1..], "--b")?;
            ops.push(Operation::CheckClearance { a, b });
        }
        "bbox" => {
            let a = expect_flag(&tail[1..], "--a")?;
            let b = expect_flag(&tail[1..], "--b")?;
            ops.push(Operation::CheckBoundingBoxes { a, b });
        }
        other => return Err(format!("check: unknown subcommand `{other}`")),
    }
    ops = finish_job(globals, ops);
    execute(ops)
}
