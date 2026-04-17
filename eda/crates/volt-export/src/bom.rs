//! Bill of Materials (BOM) generation and export.
//!
//! Groups component instances by (lib_component, value), resolves part
//! information from the library, and exports as CSV or JSON.

use std::collections::HashMap;

use serde::{Deserialize, Serialize};
use uuid::Uuid;

use volt_core::library::{Component, Device, Package};
use volt_core::project::Circuit;

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

/// A single line in the BOM — one unique part (same component + value).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BomEntry {
    /// Designators grouped on this line, e.g. `["R1", "R2"]`.
    pub designators: Vec<String>,
    /// Number of instances.
    pub quantity: usize,
    /// Component value (e.g. "10k", "100nF").
    pub value: String,
    /// Package / footprint name.
    pub package: String,
    /// Human-readable description.
    pub description: String,
    /// Manufacturer part number.
    pub mpn: String,
    /// Manufacturer name.
    pub manufacturer: String,
}

/// Result of BOM generation.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BomResult {
    pub entries: Vec<BomEntry>,
    pub total_components: usize,
    pub unique_parts: usize,
}

// ---------------------------------------------------------------------------
// Library trait
// ---------------------------------------------------------------------------

/// Provides access to library elements needed for BOM and pick & place export.
pub trait BomLibrary {
    fn get_component(&self, uuid: &Uuid) -> Option<&Component>;
    fn get_device(&self, uuid: &Uuid) -> Option<&Device>;
    fn get_package(&self, uuid: &Uuid) -> Option<&Package>;
}

// ---------------------------------------------------------------------------
// BOM generation
// ---------------------------------------------------------------------------

/// Generate a BOM from a circuit and library.
///
/// Groups components by `(lib_component UUID, value)` so that two "10k"
/// resistors using the same library component become one BOM line with
/// quantity 2. Designators are sorted naturally (R1, R2, R10 — not R1, R10, R2).
pub fn generate_bom(circuit: &Circuit, library: &dyn BomLibrary) -> BomResult {
    // Key: (lib_component UUID, value)
    // Value: list of designators + first-seen metadata
    let mut groups: HashMap<(Uuid, String), BomGroupData> = HashMap::new();

    for comp in &circuit.components {
        let key = (comp.lib_component, comp.value.clone());

        let entry = groups.entry(key.clone()).or_insert_with(|| {
            // Resolve library metadata once per group
            let (package_name, description, mpn, manufacturer) = resolve_part_info(comp, library);

            BomGroupData {
                designators: Vec::new(),
                value: comp.value.clone(),
                package: package_name,
                description,
                mpn,
                manufacturer,
            }
        });

        entry.designators.push(comp.name.clone());
    }

    let total_components: usize = groups.values().map(|g| g.designators.len()).sum();

    let mut entries: Vec<BomEntry> = groups
        .into_values()
        .map(|mut g| {
            // Natural sort of designators
            g.designators.sort_by(|a, b| natural_cmp(a, b));
            let quantity = g.designators.len();
            BomEntry {
                designators: g.designators,
                quantity,
                value: g.value,
                package: g.package,
                description: g.description,
                mpn: g.mpn,
                manufacturer: g.manufacturer,
            }
        })
        .collect();

    // Sort entries by first designator for deterministic output
    entries.sort_by(|a, b| {
        let a_first = a.designators.first().map(String::as_str).unwrap_or("");
        let b_first = b.designators.first().map(String::as_str).unwrap_or("");
        natural_cmp(a_first, b_first)
    });

    let unique_parts = entries.len();

    BomResult {
        entries,
        total_components,
        unique_parts,
    }
}

/// Intermediate grouping data.
struct BomGroupData {
    designators: Vec<String>,
    value: String,
    package: String,
    description: String,
    mpn: String,
    manufacturer: String,
}

/// Resolve package name, description, MPN, and manufacturer for a component instance.
fn resolve_part_info(
    comp: &volt_core::project::ComponentInstance,
    library: &dyn BomLibrary,
) -> (String, String, String, String) {
    let mut package_name = String::new();
    let mut description = String::new();
    let mut mpn = String::new();
    let mut manufacturer = String::new();

    // Get component description
    if let Some(lib_comp) = library.get_component(&comp.lib_component) {
        description = lib_comp.meta.description.clone();
    }

    // Try to resolve device → package chain
    if let Some(da) = comp.device_assignments.first() {
        if let Some(device) = library.get_device(&da.device) {
            // Package name from Device → Package
            if let Some(pkg) = library.get_package(&device.package) {
                package_name = pkg.meta.name.clone();
            }

            // MPN from DevicePart (first part, or from assignment)
            if !da.part.mpn.is_empty() {
                mpn = da.part.mpn.clone();
                manufacturer = da.part.manufacturer.clone();
            } else if let Some(part) = device.parts.first() {
                mpn = part.mpn.clone();
                manufacturer = part.manufacturer.clone();
            }
        }
    }

    (package_name, description, mpn, manufacturer)
}

// ---------------------------------------------------------------------------
// CSV export
// ---------------------------------------------------------------------------

/// Export a BOM as CSV.
///
/// Headers: `Designator,Quantity,Value,Package,Description,MPN,Manufacturer`
///
/// Fields containing commas or quotes are properly quoted per RFC 4180.
pub fn export_bom_csv(bom: &BomResult) -> String {
    let mut out = String::new();
    out.push_str("Designator,Quantity,Value,Package,Description,MPN,Manufacturer\n");

    for entry in &bom.entries {
        let designators = entry.designators.join(" ");
        out.push_str(&csv_field(&designators));
        out.push(',');
        out.push_str(&entry.quantity.to_string());
        out.push(',');
        out.push_str(&csv_field(&entry.value));
        out.push(',');
        out.push_str(&csv_field(&entry.package));
        out.push(',');
        out.push_str(&csv_field(&entry.description));
        out.push(',');
        out.push_str(&csv_field(&entry.mpn));
        out.push(',');
        out.push_str(&csv_field(&entry.manufacturer));
        out.push('\n');
    }

    out
}

/// Quote a CSV field if it contains commas, quotes, or newlines.
pub fn csv_field(s: &str) -> String {
    if s.contains(',') || s.contains('"') || s.contains('\n') {
        let escaped = s.replace('"', "\"\"");
        format!("\"{escaped}\"")
    } else {
        s.to_string()
    }
}

// ---------------------------------------------------------------------------
// JSON export
// ---------------------------------------------------------------------------

/// Export a BOM as a JSON array of objects.
pub fn export_bom_json(bom: &BomResult) -> String {
    // Use serde_json for proper escaping
    serde_json::to_string_pretty(&bom.entries).unwrap_or_else(|_| "[]".to_string())
}

// ---------------------------------------------------------------------------
// Natural sort
// ---------------------------------------------------------------------------

/// Compare two strings with natural (human-friendly) ordering.
///
/// Splits each string into alternating text and numeric segments and compares
/// them pairwise — numeric segments are compared by value so that
/// "R2" < "R10" instead of the lexicographic "R10" < "R2".
pub fn natural_cmp(a: &str, b: &str) -> std::cmp::Ordering {
    let a_parts = split_natural(a);
    let b_parts = split_natural(b);

    for (ap, bp) in a_parts.iter().zip(b_parts.iter()) {
        let ord = match (ap, bp) {
            (NatPart::Num(an), NatPart::Num(bn)) => an.cmp(bn),
            (NatPart::Text(at), NatPart::Text(bt)) => at.cmp(bt),
            (NatPart::Num(_), NatPart::Text(_)) => std::cmp::Ordering::Less,
            (NatPart::Text(_), NatPart::Num(_)) => std::cmp::Ordering::Greater,
        };
        if ord != std::cmp::Ordering::Equal {
            return ord;
        }
    }

    a_parts.len().cmp(&b_parts.len())
}

#[derive(Debug)]
enum NatPart {
    Text(String),
    Num(u64),
}

fn split_natural(s: &str) -> Vec<NatPart> {
    let mut parts = Vec::new();
    let mut chars = s.chars().peekable();

    while chars.peek().is_some() {
        if chars.peek().unwrap().is_ascii_digit() {
            let mut num_str = String::new();
            while let Some(&c) = chars.peek() {
                if c.is_ascii_digit() {
                    num_str.push(c);
                    chars.next();
                } else {
                    break;
                }
            }
            parts.push(NatPart::Num(num_str.parse().unwrap_or(0)));
        } else {
            let mut text = String::new();
            while let Some(&c) = chars.peek() {
                if !c.is_ascii_digit() {
                    text.push(c);
                    chars.next();
                } else {
                    break;
                }
            }
            parts.push(NatPart::Text(text));
        }
    }

    parts
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use uuid::Uuid;
    use volt_core::library::{Component, Device, DevicePart, LibraryMeta, Package};
    use volt_core::project::{Circuit, ComponentInstance, DeviceAssignment, DevicePartRef};

    /// A simple in-memory BomLibrary for testing.
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

    fn make_meta(uuid: Uuid, name: &str, desc: &str) -> LibraryMeta {
        LibraryMeta {
            uuid,
            name: name.to_string(),
            description: desc.to_string(),
            keywords: String::new(),
            author: String::new(),
            version: String::new(),
            created: chrono::Utc::now(),
            deprecated: false,
            category: None,
        }
    }

    #[test]
    fn test_two_same_value_resistors_grouped() {
        let comp_uuid = Uuid::new_v4();
        let device_uuid = Uuid::new_v4();
        let package_uuid = Uuid::new_v4();

        // Library component
        let lib_comp = Component {
            meta: make_meta(comp_uuid, "Resistor", "Generic resistor"),
            prefix: "R".to_string(),
            default_value: String::new(),
            schematic_only: false,
            attributes: vec![],
            signals: vec![],
            variants: vec![],
        };

        // Device
        let device = Device {
            meta: make_meta(device_uuid, "R0402", "0402 resistor"),
            component: comp_uuid,
            package: package_uuid,
            pad_mappings: vec![],
            parts: vec![DevicePart {
                mpn: "RC0402FR-0710KL".to_string(),
                manufacturer: "Yageo".to_string(),
                attributes: vec![],
            }],
        };

        // Package
        let package = Package {
            meta: make_meta(package_uuid, "R0402", "0402 package"),
            assembly_type: volt_core::common::AssemblyType::Smt,
            grid_interval: 2.54,
            min_copper_clearance: 0.2,
            pads: vec![],
            footprints: vec![],
        };

        let mut components = HashMap::new();
        components.insert(comp_uuid, lib_comp);
        let mut devices = HashMap::new();
        devices.insert(device_uuid, device);
        let mut packages = HashMap::new();
        packages.insert(package_uuid, package);

        let library = TestLibrary {
            components,
            devices,
            packages,
        };

        // Two resistors with same lib_component and value
        let circuit = Circuit {
            components: vec![
                ComponentInstance {
                    uuid: Uuid::new_v4(),
                    lib_component: comp_uuid,
                    lib_variant: Uuid::new_v4(),
                    name: "R1".to_string(),
                    value: "10k".to_string(),
                    lock_assembly: false,
                    device_assignments: vec![DeviceAssignment {
                        device: device_uuid,
                        variant: Uuid::new_v4(),
                        part: DevicePartRef {
                            mpn: "RC0402FR-0710KL".to_string(),
                            manufacturer: "Yageo".to_string(),
                            attributes: vec![],
                        },
                    }],
                    signal_connections: vec![],
                },
                ComponentInstance {
                    uuid: Uuid::new_v4(),
                    lib_component: comp_uuid,
                    lib_variant: Uuid::new_v4(),
                    name: "R2".to_string(),
                    value: "10k".to_string(),
                    lock_assembly: false,
                    device_assignments: vec![DeviceAssignment {
                        device: device_uuid,
                        variant: Uuid::new_v4(),
                        part: DevicePartRef {
                            mpn: "RC0402FR-0710KL".to_string(),
                            manufacturer: "Yageo".to_string(),
                            attributes: vec![],
                        },
                    }],
                    signal_connections: vec![],
                },
            ],
            differential_pairs: vec![],
            nets: vec![],
            net_classes: vec![],
            assembly_variants: vec![],
        };

        let bom = generate_bom(&circuit, &library);

        assert_eq!(bom.total_components, 2);
        assert_eq!(bom.unique_parts, 1);
        assert_eq!(bom.entries.len(), 1);

        let entry = &bom.entries[0];
        assert_eq!(entry.quantity, 2);
        assert_eq!(entry.designators, vec!["R1", "R2"]);
        assert_eq!(entry.value, "10k");
        assert_eq!(entry.package, "R0402");
        assert_eq!(entry.mpn, "RC0402FR-0710KL");
        assert_eq!(entry.manufacturer, "Yageo");
    }

    #[test]
    fn test_different_values_not_grouped() {
        let comp_uuid = Uuid::new_v4();

        let lib_comp = Component {
            meta: make_meta(comp_uuid, "Resistor", "Generic resistor"),
            prefix: "R".to_string(),
            default_value: String::new(),
            schematic_only: false,
            attributes: vec![],
            signals: vec![],
            variants: vec![],
        };

        let mut components = HashMap::new();
        components.insert(comp_uuid, lib_comp);

        let library = TestLibrary {
            components,
            devices: HashMap::new(),
            packages: HashMap::new(),
        };

        let circuit = Circuit {
            components: vec![
                ComponentInstance {
                    uuid: Uuid::new_v4(),
                    lib_component: comp_uuid,
                    lib_variant: Uuid::new_v4(),
                    name: "R1".to_string(),
                    value: "10k".to_string(),
                    lock_assembly: false,
                    device_assignments: vec![],
                    signal_connections: vec![],
                },
                ComponentInstance {
                    uuid: Uuid::new_v4(),
                    lib_component: comp_uuid,
                    lib_variant: Uuid::new_v4(),
                    name: "R2".to_string(),
                    value: "4.7k".to_string(),
                    lock_assembly: false,
                    device_assignments: vec![],
                    signal_connections: vec![],
                },
            ],
            differential_pairs: vec![],
            nets: vec![],
            net_classes: vec![],
            assembly_variants: vec![],
        };

        let bom = generate_bom(&circuit, &library);

        assert_eq!(bom.total_components, 2);
        assert_eq!(bom.unique_parts, 2);
        assert_eq!(bom.entries.len(), 2);
    }

    #[test]
    fn test_natural_sort_designators() {
        let comp_uuid = Uuid::new_v4();

        let lib_comp = Component {
            meta: make_meta(comp_uuid, "Resistor", "Generic resistor"),
            prefix: "R".to_string(),
            default_value: String::new(),
            schematic_only: false,
            attributes: vec![],
            signals: vec![],
            variants: vec![],
        };

        let mut components = HashMap::new();
        components.insert(comp_uuid, lib_comp);

        let library = TestLibrary {
            components,
            devices: HashMap::new(),
            packages: HashMap::new(),
        };

        let circuit = Circuit {
            components: vec![
                ComponentInstance {
                    uuid: Uuid::new_v4(),
                    lib_component: comp_uuid,
                    lib_variant: Uuid::new_v4(),
                    name: "R10".to_string(),
                    value: "10k".to_string(),
                    lock_assembly: false,
                    device_assignments: vec![],
                    signal_connections: vec![],
                },
                ComponentInstance {
                    uuid: Uuid::new_v4(),
                    lib_component: comp_uuid,
                    lib_variant: Uuid::new_v4(),
                    name: "R2".to_string(),
                    value: "10k".to_string(),
                    lock_assembly: false,
                    device_assignments: vec![],
                    signal_connections: vec![],
                },
                ComponentInstance {
                    uuid: Uuid::new_v4(),
                    lib_component: comp_uuid,
                    lib_variant: Uuid::new_v4(),
                    name: "R1".to_string(),
                    value: "10k".to_string(),
                    lock_assembly: false,
                    device_assignments: vec![],
                    signal_connections: vec![],
                },
            ],
            differential_pairs: vec![],
            nets: vec![],
            net_classes: vec![],
            assembly_variants: vec![],
        };

        let bom = generate_bom(&circuit, &library);

        assert_eq!(bom.entries.len(), 1);
        assert_eq!(bom.entries[0].designators, vec!["R1", "R2", "R10"]);
    }

    #[test]
    fn test_csv_export_format() {
        let bom = BomResult {
            entries: vec![BomEntry {
                designators: vec!["R1".to_string(), "R2".to_string()],
                quantity: 2,
                value: "10k".to_string(),
                package: "R0402".to_string(),
                description: "Resistor, thick film".to_string(),
                mpn: "RC0402FR-0710KL".to_string(),
                manufacturer: "Yageo".to_string(),
            }],
            total_components: 2,
            unique_parts: 1,
        };

        let csv = export_bom_csv(&bom);
        let lines: Vec<&str> = csv.lines().collect();

        assert_eq!(
            lines[0],
            "Designator,Quantity,Value,Package,Description,MPN,Manufacturer"
        );
        assert_eq!(
            lines[1],
            "R1 R2,2,10k,R0402,\"Resistor, thick film\",RC0402FR-0710KL,Yageo"
        );
    }

    #[test]
    fn test_csv_quoting() {
        assert_eq!(csv_field("hello"), "hello");
        assert_eq!(csv_field("hello, world"), "\"hello, world\"");
        assert_eq!(csv_field("say \"hi\""), "\"say \"\"hi\"\"\"");
    }

    #[test]
    fn test_json_export() {
        let bom = BomResult {
            entries: vec![BomEntry {
                designators: vec!["C1".to_string()],
                quantity: 1,
                value: "100nF".to_string(),
                package: "C0402".to_string(),
                description: "Capacitor".to_string(),
                mpn: "".to_string(),
                manufacturer: "".to_string(),
            }],
            total_components: 1,
            unique_parts: 1,
        };

        let json = export_bom_json(&bom);
        let parsed: Vec<serde_json::Value> = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.len(), 1);
        assert_eq!(parsed[0]["value"], "100nF");
        assert_eq!(parsed[0]["quantity"], 1);
    }
}
