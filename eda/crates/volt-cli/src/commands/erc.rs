//! `volt-eda erc` — run electrical rule checking.

use std::collections::HashMap;
use std::path::PathBuf;

use volt_core::library::Component;
use volt_erc::{MapResolver, run_erc_with_schematics};

use super::project_io::{self, Result};

pub fn erc_command(project: PathBuf) -> Result<()> {
    project_io::ensure_project(&project)?;
    let circuit = project_io::read_circuit(&project)?;
    let schematics = project_io::read_all_schematics(&project)?;

    // Load all library components referenced by circuit
    let mut components = HashMap::new();
    for comp_instance in &circuit.components {
        if !components.contains_key(&comp_instance.lib_component) {
            if let Ok(lib_comp) = project_io::read_library_element::<Component>(
                &project,
                "components",
                &comp_instance.lib_component,
            ) {
                components.insert(comp_instance.lib_component, lib_comp);
            }
        }
    }

    let resolver = MapResolver { components };
    let result = run_erc_with_schematics(&circuit, &schematics, &resolver);

    println!("{}", serde_json::to_string_pretty(&result)?);

    if !result.passed {
        std::process::exit(1);
    }
    Ok(())
}
