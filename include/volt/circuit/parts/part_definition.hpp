#pragma once

#include <array>
#include <compare>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/electrical/records.hpp>
#include <volt/circuit/parts/parts.hpp>
#include <volt/core/content_hash.hpp>
#include <volt/core/diagnostics.hpp>

namespace volt {

/** Stable library identity for one exact part artifact. */
class PartIdentity {
  public:
    /** Construct identity from non-empty namespace, name, and version fields. */
    PartIdentity(std::string namespace_name, std::string name, std::string version);

    /** Return the library namespace. */
    [[nodiscard]] const std::string &namespace_name() const noexcept { return namespace_; }

    /** Return the part name inside the namespace. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the human part release version. */
    [[nodiscard]] const std::string &version() const noexcept { return version_; }

  private:
    std::string namespace_;
    std::string name_;
    std::string version_;
};

/** Provenance metadata carried by the exact part artifact itself. */
class PartProvenance {
  public:
    /** Construct optional provenance fields; empty strings are omitted when serialized. */
    PartProvenance(std::string datasheet = {}, std::string authored_by = {},
                   std::string derived_from = {});

    /** Return whether no provenance fields are present. */
    [[nodiscard]] bool empty() const noexcept;

    /** Return the datasheet identifier or revision, when present. */
    [[nodiscard]] const std::string &datasheet() const noexcept { return datasheet_; }

    /** Return the author identifier, when present. */
    [[nodiscard]] const std::string &authored_by() const noexcept { return authored_by_; }

    /** Return the derivation source, when present. */
    [[nodiscard]] const std::string &derived_from() const noexcept { return derived_from_; }

  private:
    std::string datasheet_;
    std::string authored_by_;
    std::string derived_from_;
};

/** Hash-addressed schematic asset used by an exact part. */
class PartSchematicAssetReference {
  public:
    /** Construct a content-addressed schematic asset reference. */
    PartSchematicAssetReference(std::string name, std::string variant, ContentHash hash);

    /** Return the symbol asset name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the component-local variant label. */
    [[nodiscard]] const std::string &variant() const noexcept { return variant_; }

    /** Return the content hash for the referenced symbol bytes. */
    [[nodiscard]] const ContentHash &hash() const noexcept { return hash_; }

  private:
    std::string name_;
    std::string variant_;
    ContentHash hash_;
};

/** Stable exact-package terminal identity. */
class PackageTerminalKey {
  public:
    /** Construct a package-terminal key from a non-empty label. */
    explicit PackageTerminalKey(std::string value);

    /** Return the stable package-terminal label. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare package-terminal identities. */
    [[nodiscard]] bool operator==(const PackageTerminalKey &) const noexcept = default;

    /** Order package-terminal identities for deterministic normalization. */
    [[nodiscard]] std::strong_ordering
    operator<=>(const PackageTerminalKey &) const noexcept = default;

  private:
    std::string value_;
};

/** Stable selected-footprint pad identity. */
class FootprintPadKey {
  public:
    /** Construct a footprint-pad key from a non-empty label. */
    explicit FootprintPadKey(std::string value);

    /** Return the stable footprint-pad label. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare footprint-pad identities. */
    [[nodiscard]] bool operator==(const FootprintPadKey &) const noexcept = default;

    /** Order footprint-pad identities for deterministic normalization. */
    [[nodiscard]] std::strong_ordering
    operator<=>(const FootprintPadKey &) const noexcept = default;

  private:
    std::string value_;
};

/** Mapping from one contract-local logical PinKey to one or more package terminals. */
class PinPackageTerminalMapping {
  public:
    /** Construct and normalize one non-empty logical-pin to terminal mapping. */
    PinPackageTerminalMapping(PinKey pin, std::vector<PackageTerminalKey> terminals);

    /** Return the component-contract logical pin. */
    [[nodiscard]] const PinKey &pin() const noexcept { return pin_; }

    /** Return sorted unique package terminals implementing the logical pin. */
    [[nodiscard]] const std::vector<PackageTerminalKey> &terminals() const noexcept {
        return terminals_;
    }

  private:
    PinKey pin_;
    std::vector<PackageTerminalKey> terminals_;
};

/** Explicit reason why a package terminal is outside the logical electrical interface. */
enum class PackageTerminalDisposition {
    NoConnect,
    NonElectrical,
};

/** One package terminal explicitly excluded from logical-pin ownership. */
class DisposedPackageTerminal {
  public:
    /** Construct an explicitly disposed package terminal. */
    DisposedPackageTerminal(PackageTerminalKey terminal, PackageTerminalDisposition disposition);

    /** Return the disposed package terminal. */
    [[nodiscard]] const PackageTerminalKey &terminal() const noexcept { return terminal_; }

    /** Return why this terminal is outside the logical interface. */
    [[nodiscard]] PackageTerminalDisposition disposition() const noexcept { return disposition_; }

  private:
    PackageTerminalKey terminal_;
    PackageTerminalDisposition disposition_;
};

/** Mapping from one package terminal to one or more selected-footprint pads. */
class PackageTerminalPadMapping {
  public:
    /** Construct and normalize one non-empty terminal-to-pad mapping. */
    PackageTerminalPadMapping(PackageTerminalKey terminal, std::vector<FootprintPadKey> pads);

    /** Return the package terminal. */
    [[nodiscard]] const PackageTerminalKey &terminal() const noexcept { return terminal_; }

    /** Return sorted unique footprint pads implementing the package terminal. */
    [[nodiscard]] const std::vector<FootprintPadKey> &pads() const noexcept { return pads_; }

  private:
    PackageTerminalKey terminal_;
    std::vector<FootprintPadKey> pads_;
};

/** Explicit non-terminal role or electrical thermal role of a footprint pad. */
enum class PartFootprintPadRole {
    Mechanical,
    Thermal,
    NonElectrical,
};

/** Minimal footprint pad summary carried by the exact part for lineup checks. */
class PartFootprintPad {
  public:
    /** Construct an electrical footprint pad with finite center and positive size. */
    PartFootprintPad(std::string label, double x_mm, double y_mm, double width_mm,
                     double height_mm);

    /** Construct a footprint pad with an explicit physical role. */
    PartFootprintPad(std::string label, double x_mm, double y_mm, double width_mm, double height_mm,
                     PartFootprintPadRole role);

    /** Return the stable pad label inside the selected footprint. */
    [[nodiscard]] const std::string &label() const noexcept { return label_; }

    /** Return the footprint-local X center in millimeters. */
    [[nodiscard]] double x_mm() const noexcept { return x_mm_; }

    /** Return the footprint-local Y center in millimeters. */
    [[nodiscard]] double y_mm() const noexcept { return y_mm_; }

    /** Return the pad width in millimeters. */
    [[nodiscard]] double width_mm() const noexcept { return width_mm_; }

    /** Return the pad height in millimeters. */
    [[nodiscard]] double height_mm() const noexcept { return height_mm_; }

    /** Return the optional explicit physical role. */
    [[nodiscard]] const std::optional<PartFootprintPadRole> &role() const noexcept { return role_; }

    /** Return whether this pad must be owned through a package-terminal mapping. */
    [[nodiscard]] bool requires_terminal_mapping() const noexcept;

  private:
    PartFootprintPad(std::string label, double x_mm, double y_mm, double width_mm, double height_mm,
                     std::optional<PartFootprintPadRole> role);

    std::string label_;
    double x_mm_;
    double y_mm_;
    double width_mm_;
    double height_mm_;
    std::optional<PartFootprintPadRole> role_;
};

/** Point in an exact part's footprint projection, measured in millimeters. */
class PartFootprintPoint {
  public:
    /** Construct a finite footprint-projection point. */
    PartFootprintPoint(double x_mm, double y_mm);

    /** Return the footprint-local X coordinate in millimeters. */
    [[nodiscard]] double x_mm() const noexcept { return x_mm_; }

    /** Return the footprint-local Y coordinate in millimeters. */
    [[nodiscard]] double y_mm() const noexcept { return y_mm_; }

    /** Compare exact footprint points. */
    [[nodiscard]] bool operator==(const PartFootprintPoint &) const noexcept = default;

  private:
    double x_mm_;
    double y_mm_;
};

/** Closed polygon in an exact part's footprint projection. */
class PartFootprintPolygon {
  public:
    /** Construct a non-degenerate footprint-projection polygon from boundary vertices. */
    explicit PartFootprintPolygon(std::vector<PartFootprintPoint> vertices);

    /** Return polygon vertices in deterministic boundary order. */
    [[nodiscard]] const std::vector<PartFootprintPoint> &vertices() const noexcept {
        return vertices_;
    }

    /** Compare exact footprint polygon vertices. */
    [[nodiscard]] bool operator==(const PartFootprintPolygon &) const noexcept = default;

  private:
    std::vector<PartFootprintPoint> vertices_;
};

/** Semantic kind for non-pad markings in an exact part footprint projection. */
enum class PartFootprintMarkingKind {
    Silkscreen,
    Polarity,
    PinOne,
};

/** Non-pad marking geometry in an exact part footprint projection. */
class PartFootprintMarking {
  public:
    /** Construct a semantic part footprint marking polygon. */
    PartFootprintMarking(PartFootprintMarkingKind kind, PartFootprintPolygon polygon);

    /** Return the marking semantics. */
    [[nodiscard]] PartFootprintMarkingKind kind() const noexcept { return kind_; }

    /** Return the footprint-local marking polygon. */
    [[nodiscard]] const PartFootprintPolygon &polygon() const noexcept { return polygon_; }

  private:
    PartFootprintMarkingKind kind_;
    PartFootprintPolygon polygon_;
};

/** Hash-addressed footprint projection for an exact part. */
class HashedFootprintReference {
  public:
    /** Construct a hashed footprint projection reference. */
    HashedFootprintReference(FootprintRef footprint, ContentHash hash);

    /** Return the footprint reference. */
    [[nodiscard]] const FootprintRef &footprint() const noexcept { return footprint_; }

    /** Return the content hash for the referenced footprint projection. */
    [[nodiscard]] const ContentHash &hash() const noexcept { return hash_; }

  private:
    FootprintRef footprint_;
    ContentHash hash_;
};

/** Hash-addressed 3D artifact reference and footprint-relative placement. */
class PartModel3DReference {
  public:
    /** Construct a hashed 3D model reference. */
    PartModel3DReference(std::string format, std::string file_name, ContentHash hash,
                         std::array<double, 3> translation_mm, double rotation_deg);

    /** Return the normalized model format, such as glb or step. */
    [[nodiscard]] const std::string &format() const noexcept { return format_; }

    /** Return the model file basename. */
    [[nodiscard]] const std::string &file_name() const noexcept { return file_name_; }

    /** Return the content hash for the frozen 3D artifact bytes. */
    [[nodiscard]] const ContentHash &hash() const noexcept { return hash_; }

    /** Return the footprint-relative model translation in millimeters. */
    [[nodiscard]] const std::array<double, 3> &translation_mm() const noexcept {
        return translation_mm_;
    }

    /** Return the footprint-relative rotation around the board normal in degrees. */
    [[nodiscard]] double rotation_deg() const noexcept { return rotation_deg_; }

  private:
    std::string format_;
    std::string file_name_;
    ContentHash hash_;
    std::array<double, 3> translation_mm_;
    double rotation_deg_;
};

/** Stable manufacturer, package, footprint, and terminal-to-pad truth for one exact part. */
class OrderablePart {
  public:
    /** Construct an orderable exact part with content-addressed physical implementation data. */
    OrderablePart(ManufacturerPart manufacturer_part, PackageRef package,
                  HashedFootprintReference footprint, std::vector<PartFootprintPad> footprint_pads,
                  std::vector<PackageTerminalPadMapping> terminal_pad_mappings,
                  std::vector<std::string> approved_alternate_mpns = {},
                  std::optional<PartModel3DReference> model_3d = std::nullopt,
                  std::optional<PartFootprintPolygon> footprint_courtyard = std::nullopt,
                  std::optional<PartFootprintPolygon> footprint_body = std::nullopt,
                  std::optional<PartFootprintPolygon> footprint_fabrication_outline = std::nullopt,
                  std::optional<PartFootprintPolygon> footprint_assembly_outline = std::nullopt,
                  std::vector<PartFootprintMarking> footprint_markings = {});

    /** Return the primary manufacturer part identity. */
    [[nodiscard]] const ManufacturerPart &manufacturer_part() const noexcept {
        return manufacturer_part_;
    }

    /** Return the exact package label. */
    [[nodiscard]] const PackageRef &package() const noexcept { return package_; }

    /** Return the hash-addressed footprint reference. */
    [[nodiscard]] const HashedFootprintReference &footprint() const noexcept { return footprint_; }

    /** Return footprint pad summaries in stable key order. */
    [[nodiscard]] const std::vector<PartFootprintPad> &footprint_pads() const noexcept {
        return footprint_pads_;
    }

    /** Return normalized package-terminal to footprint-pad mappings. */
    [[nodiscard]] const std::vector<PackageTerminalPadMapping> &
    terminal_pad_mappings() const noexcept {
        return terminal_pad_mappings_;
    }

    /** Return the optional declared footprint courtyard polygon. */
    [[nodiscard]] const std::optional<PartFootprintPolygon> &footprint_courtyard() const noexcept {
        return footprint_courtyard_;
    }

    /** Return the optional declared footprint body envelope polygon. */
    [[nodiscard]] const std::optional<PartFootprintPolygon> &footprint_body() const noexcept {
        return footprint_body_;
    }

    /** Return the optional declared footprint fabrication outline polygon. */
    [[nodiscard]] const std::optional<PartFootprintPolygon> &
    footprint_fabrication_outline() const noexcept {
        return footprint_fabrication_outline_;
    }

    /** Return the optional declared footprint assembly outline polygon. */
    [[nodiscard]] const std::optional<PartFootprintPolygon> &
    footprint_assembly_outline() const noexcept {
        return footprint_assembly_outline_;
    }

    /** Return declared footprint silkscreen, polarity, and pin-one markings. */
    [[nodiscard]] const std::vector<PartFootprintMarking> &footprint_markings() const noexcept {
        return footprint_markings_;
    }

    /** Return approved alternate manufacturer part numbers retained from current exact truth. */
    [[nodiscard]] const std::vector<std::string> &approved_alternate_mpns() const noexcept {
        return approved_alternate_mpns_;
    }

    /** Return the optional hash-addressed 3D model reference. */
    [[nodiscard]] const std::optional<PartModel3DReference> &model_3d() const noexcept {
        return model_3d_;
    }

  private:
    ManufacturerPart manufacturer_part_;
    PackageRef package_;
    HashedFootprintReference footprint_;
    std::vector<PartFootprintPad> footprint_pads_;
    std::vector<PackageTerminalPadMapping> terminal_pad_mappings_;
    std::optional<PartFootprintPolygon> footprint_courtyard_;
    std::optional<PartFootprintPolygon> footprint_body_;
    std::optional<PartFootprintPolygon> footprint_fabrication_outline_;
    std::optional<PartFootprintPolygon> footprint_assembly_outline_;
    std::vector<PartFootprintMarking> footprint_markings_;
    std::vector<std::string> approved_alternate_mpns_;
    std::optional<PartModel3DReference> model_3d_;
};

/** One immutable exact manufacturer part implementing exactly one component digest. */
class PartDefinition {
  public:
    /** Construct and structurally validate one exact implementation of a component definition. */
    PartDefinition(const ComponentDefinition &component, PartIdentity identity,
                   ElectricalRecordSet electrical_records,
                   std::vector<PinPackageTerminalMapping> pin_terminal_mappings,
                   std::vector<DisposedPackageTerminal> terminal_dispositions,
                   PartProvenance provenance,
                   std::vector<PartSchematicAssetReference> schematic_assets,
                   OrderablePart orderable_part);

    /** Return stable human part identity. */
    [[nodiscard]] const PartIdentity &identity() const noexcept { return identity_; }

    /** Return the exact immutable ComponentDefinition content identity implemented by this part. */
    [[nodiscard]] const ContentHash &implemented_component() const noexcept {
        return implemented_component_;
    }

    /** Return canonical P1 Voltage and Current records for this exact implementation. */
    [[nodiscard]] const ElectricalRecordSet &electrical_records() const noexcept {
        return electrical_records_;
    }

    /** Return normalized logical PinKey to package-terminal mappings. */
    [[nodiscard]] const std::vector<PinPackageTerminalMapping> &
    pin_terminal_mappings() const noexcept {
        return pin_terminal_mappings_;
    }

    /** Return explicit package terminals outside the logical electrical interface. */
    [[nodiscard]] const std::vector<DisposedPackageTerminal> &
    terminal_dispositions() const noexcept {
        return terminal_dispositions_;
    }

    /** Return provenance fields. */
    [[nodiscard]] const PartProvenance &provenance() const noexcept { return provenance_; }

    /** Return content-addressed schematic assets used by this exact part. */
    [[nodiscard]] const std::vector<PartSchematicAssetReference> &
    schematic_assets() const noexcept {
        return schematic_assets_;
    }

    /** Return exact manufacturer, package, footprint, and model truth. */
    [[nodiscard]] const OrderablePart &orderable_part() const noexcept { return orderable_part_; }

    /** Return the immutable semantic content identity of this exact part. */
    [[nodiscard]] const ContentHash &content_identity() const noexcept { return content_identity_; }

  private:
    void require_complete_component_implementation(const ComponentDefinition &component) const;
    void require_complete_physical_mappings(const ComponentDefinition &component) const;

    PartIdentity identity_;
    ContentHash implemented_component_;
    ElectricalRecordSet electrical_records_;
    std::vector<PinPackageTerminalMapping> pin_terminal_mappings_;
    std::vector<DisposedPackageTerminal> terminal_dispositions_;
    PartProvenance provenance_;
    std::vector<PartSchematicAssetReference> schematic_assets_;
    OrderablePart orderable_part_;
    ContentHash content_identity_;
};

/** Validate design-quality geometry concerns for an assembled or loaded exact part. */
[[nodiscard]] DiagnosticReport validate_part_lineup(const PartDefinition &part);

} // namespace volt
