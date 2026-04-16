//! `volt-eda drc` — run design rule checking on a board.

use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

use volt_core::library::{Device, Package};
use volt_drc::{MapBoardLibrary, run_drc};

use super::project_io::{self, Result};

pub fn drc_command(project: PathBuf, board_name: String) -> Result<()> {
    project_io::ensure_project(&project)?;

    let board = project_io::read_board(&project, &board_name)?;
    let circuit = project_io::read_circuit(&project)?;

    // Load library devices and packages
    let mut devices = HashMap::new();
    let mut packages = HashMap::new();

    let devices_dir = project.join("library/devices");
    if devices_dir.exists() {
        for entry in fs::read_dir(&devices_dir)?.flatten() {
            let path = entry.path();
            if path.extension().and_then(|e| e.to_str()) == Some("json") {
                if let Ok(dev) = project_io::read_json::<Device>(&path) {
                    // Load associated package
                    if !packages.contains_key(&dev.package) {
                        if let Ok(pkg) = project_io::read_library_element::<Package>(
                            &project, "packages", &dev.package,
                        ) {
                            packages.insert(dev.package, pkg);
                        }
                    }
                    devices.insert(dev.meta.uuid, dev);
                }
            }
        }
    }

    let library = MapBoardLibrary { devices, packages };
    let drc_result = run_drc(&board, &circuit, &library);

    let result = serde_json::json!({
        "status": if drc_result.passed { "ok" } else { "fail" },
        "board": board_name,
        "passed": drc_result.passed,
        "errors": drc_result.errors,
        "warnings": drc_result.warnings,
        "diagnostics": drc_result.diagnostics,
    });

    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}
