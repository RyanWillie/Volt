//! Schema migration framework for Volt projects.
//!
//! When loading a project, if `schema_version` < `CURRENT_SCHEMA_VERSION`,
//! chained migration functions are applied to upgrade the project files.
//! If `schema_version` > `CURRENT_SCHEMA_VERSION`, the project is too new
//! and an error is returned.

use std::fs;
use std::path::Path;

use crate::project::CURRENT_SCHEMA_VERSION;

/// Result of a migration attempt.
#[derive(Debug)]
pub struct MigrationResult {
    /// Starting schema version.
    pub from_version: u32,
    /// Final schema version after migration.
    pub to_version: u32,
    /// Descriptions of applied migrations.
    pub applied: Vec<String>,
}

/// Check and migrate a project directory if needed.
///
/// Returns `Ok(None)` if no migration was needed.
/// Returns `Ok(Some(result))` if migrations were applied.
/// Returns `Err` if the project is too new or migration fails.
pub fn migrate_project(project_dir: &Path) -> Result<Option<MigrationResult>, String> {
    let volt_path = project_dir.join("volt.json");
    if !volt_path.exists() {
        return Err("Not a Volt project: no volt.json found".into());
    }

    let content =
        fs::read_to_string(&volt_path).map_err(|e| format!("Failed to read volt.json: {e}"))?;
    let mut value: serde_json::Value =
        serde_json::from_str(&content).map_err(|e| format!("Failed to parse volt.json: {e}"))?;

    let current_version = value
        .get("schema_version")
        .and_then(|v| v.as_u64())
        .unwrap_or(1) as u32;

    if current_version > CURRENT_SCHEMA_VERSION {
        return Err(format!(
            "Project schema version {} is newer than supported version {}. \
             Please update Volt EDA.",
            current_version, CURRENT_SCHEMA_VERSION
        ));
    }

    if current_version == CURRENT_SCHEMA_VERSION {
        return Ok(None);
    }

    let mut applied = Vec::new();
    let mut version = current_version;

    while version < CURRENT_SCHEMA_VERSION {
        match version {
            1 => {
                // v1→v2: Add net scope fields, hierarchy, bus, diff-pair support.
                // All new fields use serde defaults, so no JSON mutation is needed
                // for existing files — they deserialize correctly as-is.
                applied.push(
                    "v1→v2: advanced schematic features (hierarchy, buses, diff pairs)".into(),
                );
            }
            _ => {
                applied.push(format!("v{}→v{}: no-op (compatible)", version, version + 1));
            }
        }
        version += 1;
    }

    // Update schema_version in the JSON and write back
    value["schema_version"] = serde_json::Value::from(CURRENT_SCHEMA_VERSION);
    let updated = serde_json::to_string_pretty(&value)
        .map_err(|e| format!("Failed to serialize volt.json: {e}"))?;
    fs::write(&volt_path, updated + "\n").map_err(|e| format!("Failed to write volt.json: {e}"))?;

    Ok(Some(MigrationResult {
        from_version: current_version,
        to_version: CURRENT_SCHEMA_VERSION,
        applied,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    #[test]
    fn migration_returns_none_for_current_version() {
        let dir = tempfile::tempdir().unwrap();
        let volt_json = serde_json::json!({
            "uuid": "00000000-0000-0000-0000-000000000000",
            "name": "test",
            "version": "v1",
            "schema_version": CURRENT_SCHEMA_VERSION,
            "created": "2026-01-01T00:00:00Z",
        });
        fs::write(
            dir.path().join("volt.json"),
            serde_json::to_string_pretty(&volt_json).unwrap(),
        )
        .unwrap();

        let result = migrate_project(dir.path()).unwrap();
        assert!(result.is_none());
    }

    #[test]
    fn migration_returns_error_for_future_version() {
        let dir = tempfile::tempdir().unwrap();
        let volt_json = serde_json::json!({
            "uuid": "00000000-0000-0000-0000-000000000000",
            "name": "test",
            "version": "v1",
            "schema_version": CURRENT_SCHEMA_VERSION + 5,
            "created": "2026-01-01T00:00:00Z",
        });
        fs::write(
            dir.path().join("volt.json"),
            serde_json::to_string_pretty(&volt_json).unwrap(),
        )
        .unwrap();

        let result = migrate_project(dir.path());
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("newer than supported"));
    }

    #[test]
    fn migration_adds_schema_version_to_legacy_project() {
        let dir = tempfile::tempdir().unwrap();
        // Legacy project with no schema_version field
        let volt_json = serde_json::json!({
            "uuid": "00000000-0000-0000-0000-000000000000",
            "name": "test",
            "version": "v1",
            "created": "2026-01-01T00:00:00Z",
        });
        fs::write(
            dir.path().join("volt.json"),
            serde_json::to_string_pretty(&volt_json).unwrap(),
        )
        .unwrap();

        // Legacy project defaults to v1; should migrate to CURRENT
        let result = migrate_project(dir.path()).unwrap();
        if CURRENT_SCHEMA_VERSION > 1 {
            assert!(result.is_some(), "legacy project should be migrated");
            let r = result.unwrap();
            assert_eq!(r.from_version, 1);
            assert_eq!(r.to_version, CURRENT_SCHEMA_VERSION);
        } else {
            assert!(result.is_none());
        }
    }
}
