//! FreeCAD document lifecycle.

use std::path::PathBuf;
use volt_cad_core::Operation;

use super::{execute, Globals};
use crate::parse::{expect_flag, optional_flag};

pub fn run(globals: &Globals, tail: &[String]) -> Result<(), String> {
    if tail.is_empty() {
        return Err("document: missing subcommand (new | open | save)".to_string());
    }
    let mut ops = Vec::new();
    match tail[0].as_str() {
        "new" => {
            let name = optional_flag(&tail[1..], "--name").unwrap_or_else(|| "VoltCAD".to_string());
            ops.push(Operation::DocumentNew { name });
            if let Some(p) = optional_flag(&tail[1..], "--save") {
                ops.push(Operation::DocumentSave {
                    path: PathBuf::from(p),
                });
            }
        }
        "open" => {
            let path = PathBuf::from(expect_flag(&tail[1..], "--path")?);
            ops.push(Operation::DocumentOpen { path });
            if let Some(p) = optional_flag(&tail[1..], "--save-as") {
                ops.push(Operation::DocumentSave {
                    path: PathBuf::from(p),
                });
            }
        }
        "save" => {
            let path = PathBuf::from(expect_flag(&tail[1..], "--path")?);
            ops.push(Operation::DocumentSave { path });
        }
        other => return Err(format!("document: unknown subcommand `{other}`")),
    }
    ops = super::finish_job(globals, ops);
    execute(ops)
}
