//! Pick & place (centroid) file generation.
//!
//! Produces a placement list from a board layout suitable for automated
//! SMT assembly machines.

use uuid::Uuid;

use volt_core::project::{Board, Circuit};

use crate::bom::BomLibrary;

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

/// A single placement entry for one device on the board.
#[derive(Debug, Clone, PartialEq)]
pub struct PlacementEntry {
    /// Component designator, e.g. "R1".
    pub designator: String,
    /// Component value, e.g. "10k".
    pub value: String,
    /// Package name, e.g. "R0402".
    pub package: String,
    /// X position in mm.
    pub x: f64,
    /// Y position in mm.
    pub y: f64,
    /// Rotation in degrees.
    pub rotation: f64,
    /// Board side: "top" or "bottom".
    pub side: String,
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

/// Generate pick & place data from a board, circuit, and library.
///
/// For each `BoardDevice`, resolves the component designator, value, and
/// package name from the circuit and library. Entries are sorted by
/// designator using natural ordering.
pub fn generate_pick_place(
    board: &Board,
    circuit: &Circuit,
    library: &dyn BomLibrary,
) -> Vec<PlacementEntry> {
    let mut entries = Vec::new();

    for board_dev in &board.devices {
        // Find the component instance in the circuit
        let comp = match circuit
            .components
            .iter()
            .find(|c| c.uuid == board_dev.component)
        {
            Some(c) => c,
            None => continue,
        };

        // Resolve package name via device → package chain
        let package_name = resolve_package_name(board_dev.lib_device, library);

        let side = if board_dev.flip { "bottom" } else { "top" };

        entries.push(PlacementEntry {
            designator: comp.name.clone(),
            value: comp.value.clone(),
            package: package_name,
            x: board_dev.position.x,
            y: board_dev.position.y,
            rotation: board_dev.rotation.0,
            side: side.to_string(),
        });
    }

    // Sort by designator using natural ordering
    entries.sort_by(|a, b| crate::bom::natural_cmp(&a.designator, &b.designator));

    entries
}

/// Look up the package name for a device UUID.
fn resolve_package_name(device_uuid: Uuid, library: &dyn BomLibrary) -> String {
    if let Some(device) = library.get_device(&device_uuid) {
        if let Some(pkg) = library.get_package(&device.package) {
            return pkg.meta.name.clone();
        }
    }
    String::new()
}

// ---------------------------------------------------------------------------
// CSV export
// ---------------------------------------------------------------------------

/// Export pick & place data as CSV.
///
/// Headers: `Designator,Value,Package,X(mm),Y(mm),Rotation,Side`
pub fn export_pick_place_csv(entries: &[PlacementEntry]) -> String {
    let mut out = String::new();
    out.push_str("Designator,Value,Package,X(mm),Y(mm),Rotation,Side\n");

    for entry in entries {
        out.push_str(&crate::bom::csv_field(&entry.designator));
        out.push(',');
        out.push_str(&crate::bom::csv_field(&entry.value));
        out.push(',');
        out.push_str(&crate::bom::csv_field(&entry.package));
        out.push(',');
        out.push_str(&format!("{:.4}", entry.x));
        out.push(',');
        out.push_str(&format!("{:.4}", entry.y));
        out.push(',');
        out.push_str(&format!("{:.1}", entry.rotation));
        out.push(',');
        out.push_str(&entry.side);
        out.push('\n');
    }

    out
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use uuid::Uuid;
    use volt_core::common::*;
    use volt_core::library::{Component, Device, LibraryMeta, Package};
    use volt_core::project::*;

    struct TestLibrary {
        components: HashMap<Uuid, Component>,
        devices: HashMap<Uuid, Device>,
        packages: HashMap<Uuid, Package>,
    }

    impl BomLibrary for TestLibrary {
        fn get_component(&self, uuid: &Uuid) -> Option<&Component> {
            self.components.get(uuid)
        }
        fn get_device(&self, uuid: &Uuid) -> Option<&Device> {
            self.devices.get(uuid)
        }
        fn get_package(&self, uuid: &Uuid) -> Option<&Package> {
            self.packages.get(uuid)
        }
    }

    fn make_meta(uuid: Uuid, name: &str) -> LibraryMeta {
        LibraryMeta {
            uuid,
            name: name.to_string(),
            description: String::new(),
            keywords: String::new(),
            author: String::new(),
            version: String::new(),
            created: chrono::Utc::now(),
            deprecated: false,
            category: None,
        }
    }

    #[test]
    fn test_pick_place_csv_output() {
        let comp_uuid = Uuid::new_v4();
        let device_uuid = Uuid::new_v4();
        let package_uuid = Uuid::new_v4();
        let footprint_uuid = Uuid::new_v4();

        let device = Device {
            meta: make_meta(device_uuid, "R0402"),
            component: comp_uuid,
            package: package_uuid,
            pad_mappings: vec![],
            parts: vec![],
        };

        let package = Package {
            meta: make_meta(package_uuid, "R0402"),
            assembly_type: AssemblyType::Smt,
            grid_interval: 2.54,
            min_copper_clearance: 0.2,
            pads: vec![],
            footprints: vec![],
        };

        let mut devices = HashMap::new();
        devices.insert(device_uuid, device);
        let mut packages = HashMap::new();
        packages.insert(package_uuid, package);

        let library = TestLibrary {
            components: HashMap::new(),
            devices,
            packages,
        };

        let circuit = Circuit {
            components: vec![ComponentInstance {
                uuid: comp_uuid,
                lib_component: Uuid::new_v4(),
                lib_variant: Uuid::new_v4(),
                name: "R1".to_string(),
                value: "10k".to_string(),
                lock_assembly: false,
                device_assignments: vec![],
                signal_connections: vec![],
            }],
            differential_pairs: vec![],
            nets: vec![],
            net_classes: vec![],
            assembly_variants: vec![],
        };

        let board = Board {
            uuid: Uuid::new_v4(),
            name: "test".to_string(),
            grid: Grid {
                interval: 1.0,
                unit: GridUnit::Millimeters,
            },
            inner_layers: 0,
            thickness: 1.6,
            solder_resist: SolderResistColor::Green,
            silkscreen: SilkscreenColor::White,
            default_font: "newstroke.bene".to_string(),
            design_rules: serde_json::from_str("{}").unwrap(),
            drc_settings: serde_json::from_str("{}").unwrap(),
            fabrication_output_settings: FabricationOutputSettings::default(),
            devices: vec![BoardDevice {
                component: comp_uuid,
                lib_device: device_uuid,
                lib_footprint: footprint_uuid,
                position: Position::new(25.4, 12.7),
                rotation: Angle(90.0),
                flip: false,
                lock: false,
                texts: vec![],
            }],
            net_segments: vec![],
            planes: vec![],
            polygons: vec![],
            holes: vec![],
        };

        let entries = generate_pick_place(&board, &circuit, &library);
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].designator, "R1");
        assert_eq!(entries[0].side, "top");

        let csv = export_pick_place_csv(&entries);
        let lines: Vec<&str> = csv.lines().collect();
        assert_eq!(
            lines[0],
            "Designator,Value,Package,X(mm),Y(mm),Rotation,Side"
        );
        assert_eq!(lines[1], "R1,10k,R0402,25.4000,12.7000,90.0,top");
    }

    #[test]
    fn test_pick_place_bottom_side() {
        let comp_uuid = Uuid::new_v4();
        let device_uuid = Uuid::new_v4();

        let library = TestLibrary {
            components: HashMap::new(),
            devices: HashMap::new(),
            packages: HashMap::new(),
        };

        let circuit = Circuit {
            components: vec![ComponentInstance {
                uuid: comp_uuid,
                lib_component: Uuid::new_v4(),
                lib_variant: Uuid::new_v4(),
                name: "C1".to_string(),
                value: "100nF".to_string(),
                lock_assembly: false,
                device_assignments: vec![],
                signal_connections: vec![],
            }],
            differential_pairs: vec![],
            nets: vec![],
            net_classes: vec![],
            assembly_variants: vec![],
        };

        let board = Board {
            uuid: Uuid::new_v4(),
            name: "test".to_string(),
            grid: Grid {
                interval: 1.0,
                unit: GridUnit::Millimeters,
            },
            inner_layers: 0,
            thickness: 1.6,
            solder_resist: SolderResistColor::Green,
            silkscreen: SilkscreenColor::White,
            default_font: "newstroke.bene".to_string(),
            design_rules: serde_json::from_str("{}").unwrap(),
            drc_settings: serde_json::from_str("{}").unwrap(),
            fabrication_output_settings: FabricationOutputSettings::default(),
            devices: vec![BoardDevice {
                component: comp_uuid,
                lib_device: device_uuid,
                lib_footprint: Uuid::new_v4(),
                position: Position::new(10.0, 20.0),
                rotation: Angle(0.0),
                flip: true,
                lock: false,
                texts: vec![],
            }],
            net_segments: vec![],
            planes: vec![],
            polygons: vec![],
            holes: vec![],
        };

        let entries = generate_pick_place(&board, &circuit, &library);
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].side, "bottom");
        assert_eq!(entries[0].designator, "C1");
    }
}
