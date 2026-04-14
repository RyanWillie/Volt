//! Run CAD jobs by invoking `freecadcmd` with an embedded Python driver.

use std::ffi::OsStr;
use std::path::PathBuf;
use std::process::Command;
use tempfile::TempDir;
use volt_cad_core::{CadJob, EngineResult};

const DRIVER_PY: &str = include_str!("../assets/volt_cad_driver.py");

fn find_executable_in_path(names: &[&str]) -> Option<PathBuf> {
    let path_var = std::env::var_os("PATH")?;
    for dir in std::env::split_paths(&path_var) {
        for name in names {
            let candidate = dir.join(name);
            if candidate.is_file() {
                return Some(candidate);
            }
        }
    }
    None
}

#[derive(Debug, thiserror::Error)]
pub enum FreecadError {
    #[error("could not locate a FreeCAD headless executable; set VOLT_CAD_FREECAD_CMD or install freecadcmd / FreeCADCmd")]
    ExecutableNotFound,
    #[error("failed to write job payload: {0}")]
    Io(#[from] std::io::Error),
    #[error("freecad process failed: {0}")]
    Process(String),
    #[error("could not read engine result: {0}")]
    ResultParse(#[from] serde_json::Error),
}

fn resolve_freecad_executable() -> Result<PathBuf, FreecadError> {
    if let Ok(p) = std::env::var("VOLT_CAD_FREECAD_CMD") {
        let path = PathBuf::from(p);
        if path.exists() {
            return Ok(path);
        }
    }
    if let Some(path) = find_executable_in_path(&["freecadcmd", "FreeCADCmd", "FreeCADcmd"]) {
        return Ok(path);
    }
    Err(FreecadError::ExecutableNotFound)
}

/// Run a CAD job through FreeCAD and return the structured engine result.
pub fn run_job(job: &CadJob) -> Result<EngineResult, FreecadError> {
    let exe = resolve_freecad_executable()?;
    let dir = TempDir::new()?;
    let job_path = dir.path().join("job.json");
    let result_path = dir.path().join("result.json");
    let driver_path = dir.path().join("volt_cad_driver.py");
    std::fs::write(&job_path, serde_json::to_string_pretty(job)?)?;
    std::fs::write(&driver_path, DRIVER_PY)?;

    let status = Command::new(&exe)
        .arg(driver_path.as_os_str())
        .arg(job_path.as_os_str())
        .arg(result_path.as_os_str())
        .status()
        .map_err(|e| FreecadError::Process(e.to_string()))?;

    if !status.success() {
        return Err(FreecadError::Process(format!(
            "freecad exited with {status}"
        )));
    }

    let bytes = std::fs::read(&result_path).map_err(|e| {
        FreecadError::Process(format!("missing result file after freecad run: {e}"))
    })?;

    let parsed: EngineResult = serde_json::from_slice(&bytes)?;
    Ok(parsed)
}

/// Run a CAD job using a custom FreeCAD executable path (for tests).
pub fn run_job_with_executable(
    job: &CadJob,
    freecad: impl AsRef<OsStr>,
) -> Result<EngineResult, FreecadError> {
    let exe = freecad.as_ref();
    let dir = TempDir::new()?;
    let job_path = dir.path().join("job.json");
    let result_path = dir.path().join("result.json");
    let driver_path = dir.path().join("volt_cad_driver.py");
    std::fs::write(&job_path, serde_json::to_string_pretty(job)?)?;
    std::fs::write(&driver_path, DRIVER_PY)?;

    let status = Command::new(exe)
        .arg(driver_path.as_os_str())
        .arg(job_path.as_os_str())
        .arg(result_path.as_os_str())
        .status()
        .map_err(|e| FreecadError::Process(e.to_string()))?;

    if !status.success() {
        return Err(FreecadError::Process(format!(
            "freecad exited with {status}"
        )));
    }

    let bytes = std::fs::read(&result_path).map_err(|e| {
        FreecadError::Process(format!("missing result file after freecad run: {e}"))
    })?;

    let parsed: EngineResult = serde_json::from_slice(&bytes)?;
    Ok(parsed)
}
