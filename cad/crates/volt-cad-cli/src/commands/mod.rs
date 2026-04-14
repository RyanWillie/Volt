pub mod boolean;
pub mod check;
pub mod document;
pub mod export_cmd;
pub mod import_cmd;
pub mod mesh;
pub mod solid;
pub mod transform;

use volt_cad_core::{CadJob, EngineResult, Operation};

use crate::parse::Globals;

pub fn run_job_cli(globals: &Globals, tail: &[String]) -> Result<(), String> {
    let job_path = crate::parse::expect_flag(tail, "--job")?;
    let mut ops = begin_job(globals);
    // `run` is advanced: only append user job operations after optional document bootstrap.
    let text = std::fs::read_to_string(&job_path).map_err(|e| e.to_string())?;
    let job: CadJob = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    ops.extend(job.operations);
    ops = finish_job(globals, ops);
    execute(ops)
}

pub(crate) fn begin_job(globals: &Globals) -> Vec<Operation> {
    let mut ops = Vec::new();
    if let Some(path) = &globals.document {
        ops.push(Operation::DocumentOpen {
            path: path.clone(),
        });
    } else {
        ops.push(Operation::DocumentNew {
            name: globals.new_doc_name.clone(),
        });
    }
    ops
}

pub(crate) fn finish_job(globals: &Globals, mut ops: Vec<Operation>) -> Vec<Operation> {
    if let Some(path) = &globals.save_document {
        ops.push(Operation::DocumentSave {
            path: path.clone(),
        });
    }
    ops
}

pub(crate) fn execute(ops: Vec<Operation>) -> Result<(), String> {
    let job = CadJob {
        version: 1,
        operations: ops,
    };
    let result = volt_cad_freecad::run_job(&job).map_err(|e| e.to_string())?;
    print_engine_result(&result)?;
    Ok(())
}

pub(crate) fn print_engine_result(result: &EngineResult) -> Result<(), String> {
    if result.status != "ok" {
        let msg = result
            .message
            .clone()
            .unwrap_or_else(|| "unknown engine error".to_string());
        return Err(msg);
    }
    let s = serde_json::to_string_pretty(result).map_err(|e| e.to_string())?;
    println!("{s}");
    Ok(())
}
