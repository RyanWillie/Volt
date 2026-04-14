//! Boolean solid operations.

use volt_cad_core::Operation;

use super::{begin_job, execute, finish_job, Globals};
use crate::parse::expect_flag;

pub fn run(globals: &Globals, tail: &[String]) -> Result<(), String> {
    if tail.is_empty() {
        return Err("boolean: missing subcommand (union | cut | common)".to_string());
    }
    let mut ops = begin_job(globals);
    match tail[0].as_str() {
        "union" => {
            let out = expect_flag(&tail[1..], "--out")?;
            let a = expect_flag(&tail[1..], "--a")?;
            let b = expect_flag(&tail[1..], "--b")?;
            ops.push(Operation::BooleanUnion { out, a, b });
        }
        "cut" => {
            let out = expect_flag(&tail[1..], "--out")?;
            let base = expect_flag(&tail[1..], "--base")?;
            let tool = expect_flag(&tail[1..], "--tool")?;
            ops.push(Operation::BooleanCut { out, base, tool });
        }
        "common" => {
            let out = expect_flag(&tail[1..], "--out")?;
            let a = expect_flag(&tail[1..], "--a")?;
            let b = expect_flag(&tail[1..], "--b")?;
            ops.push(Operation::BooleanCommon { out, a, b });
        }
        other => return Err(format!("boolean: unknown subcommand `{other}`")),
    }
    ops = finish_job(globals, ops);
    execute(ops)
}
