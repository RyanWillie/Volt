#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/circuit/parts.hpp>
#include <volt/core/content_hash.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>

namespace volt {

/** Stable library identity for a kernel-owned part artifact. */
class PartIdentity {
  public:
    /** Construct identity from non-empty namespace, name, and version fields. */
    PartIdentity(std::string namespace_name, std::string name, std::string version);

    /** Return the library namespace. */
    [[nodiscard]] const std::string &namespace_name() const noexcept { return namespace_; }

    /** Return the part name inside the namespace. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the part version. */
    [[nodiscard]] const std::string &version() const noexcept { return version_; }

  private:
    std::string namespace_;
    std::string name_;
    std::string version_;
};

/** One canonical logical pin entry in a part artifact pin map. */
class PartPin {
  public:
    /** Construct a pin entry with canonical typed semantics and optional typed constraints. */
    explicit PartPin(PinDefinition definition, ElectricalAttributeMap electrical_attributes = {});

    /** Return the canonical pin semantics. */
    [[nodiscard]] const PinDefinition &definition() const noexcept { return definition_; }

    /** Return typed electrical constraints attached to this pin. */
    [[nodiscard]] const ElectricalAttributeMap &electrical_attributes() const noexcept {
        return electrical_attributes_;
    }

  private:
    PinDefinition definition_;
    ElectricalAttributeMap electrical_attributes_;
};

/** Provenance metadata carried by the part artifact itself. */
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

/** One pin occurrence in a schematic symbol projection for a part. */
class PartSymbolPin {
  public:
    /** Construct a symbol pin occurrence from non-empty pin name and number labels. */
    PartSymbolPin(std::string name, std::string number);

    /** Return the projected pin name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the projected pin number. */
    [[nodiscard]] const std::string &number() const noexcept { return number_; }

  private:
    std::string name_;
    std::string number_;
};

/** Hash-addressed schematic symbol projection for a part. */
class HashedSchematicSymbolReference {
  public:
    /** Construct a hashed symbol projection reference. */
    HashedSchematicSymbolReference(std::string name, std::string variant, ContentHash hash,
                                   std::vector<PartSymbolPin> pins);

    /** Return the symbol name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the symbol variant label. */
    [[nodiscard]] const std::string &variant() const noexcept { return variant_; }

    /** Return the content hash for the referenced symbol projection. */
    [[nodiscard]] const ContentHash &hash() const noexcept { return hash_; }

    /** Return the schematic pin occurrences carried by this symbol projection. */
    [[nodiscard]] const std::vector<PartSymbolPin> &pins() const noexcept { return pins_; }

  private:
    std::string name_;
    std::string variant_;
    ContentHash hash_;
    std::vector<PartSymbolPin> pins_;
};

/** Optional non-logical role that lets an unmapped footprint pad stay intentional. */
enum class PartFootprintPadRole {
    Mechanical,
    Thermal,
};

/** Minimal footprint pad summary carried by the part artifact for lineup checks. */
class PartFootprintPad {
  public:
    /** Construct an electrical footprint pad with finite center and positive size. */
    PartFootprintPad(std::string label, double x_mm, double y_mm, double width_mm,
                     double height_mm);

    /** Construct a footprint pad with an explicit non-logical role. */
    PartFootprintPad(std::string label, double x_mm, double y_mm, double width_mm, double height_mm,
                     PartFootprintPadRole role);

    /** Return the stable pad label inside the footprint projection. */
    [[nodiscard]] const std::string &label() const noexcept { return label_; }

    /** Return the footprint-local X center in millimeters. */
    [[nodiscard]] double x_mm() const noexcept { return x_mm_; }

    /** Return the footprint-local Y center in millimeters. */
    [[nodiscard]] double y_mm() const noexcept { return y_mm_; }

    /** Return the pad width in millimeters. */
    [[nodiscard]] double width_mm() const noexcept { return width_mm_; }

    /** Return the pad height in millimeters. */
    [[nodiscard]] double height_mm() const noexcept { return height_mm_; }

    /** Return the optional non-logical role. */
    [[nodiscard]] const std::optional<PartFootprintPadRole> &role() const noexcept { return role_; }

    /** Return whether this pad should normally be mapped to a pin. */
    [[nodiscard]] bool requires_pin_mapping() const noexcept { return !role_.has_value(); }

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

/** Point in an orderable part's footprint projection, measured in millimeters. */
class PartFootprintPoint {
  public:
    /** Construct a finite footprint-projection point. */
    PartFootprintPoint(double x_mm, double y_mm);

    /** Return the footprint-local X coordinate in millimeters. */
    [[nodiscard]] double x_mm() const noexcept { return x_mm_; }

    /** Return the footprint-local Y coordinate in millimeters. */
    [[nodiscard]] double y_mm() const noexcept { return y_mm_; }

    /** Return whether two part footprint points have the same coordinates. */
    [[nodiscard]] friend bool operator==(PartFootprintPoint lhs,
                                         PartFootprintPoint rhs) noexcept = default;

  private:
    double x_mm_;
    double y_mm_;
};

/** Closed polygon in an orderable part's footprint projection. */
class PartFootprintPolygon {
  public:
    /** Construct a non-degenerate footprint-projection polygon from boundary vertices. */
    explicit PartFootprintPolygon(std::vector<PartFootprintPoint> vertices);

    /** Return polygon vertices in deterministic boundary order. */
    [[nodiscard]] const std::vector<PartFootprintPoint> &vertices() const noexcept {
        return vertices_;
    }

    /** Return whether two part footprint polygons carry the same vertices. */
    [[nodiscard]] friend bool operator==(const PartFootprintPolygon &lhs,
                                         const PartFootprintPolygon &rhs) noexcept {
        return lhs.vertices_ == rhs.vertices_;
    }

  private:
    void drop_duplicate_closing_vertex();

    std::vector<PartFootprintPoint> vertices_;
};

/** Hash-addressed footprint projection for an orderable part. */
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

/** Mapping from part pin number to physical footprint pad label. */
class OrderablePinPadMapping {
  public:
    /** Construct a pin-number to pad mapping from non-empty labels. */
    OrderablePinPadMapping(std::string pin_number, std::string pad);

    /** Return the part pin number. */
    [[nodiscard]] const std::string &pin_number() const noexcept { return pin_number_; }

    /** Return the physical footprint pad label. */
    [[nodiscard]] const std::string &pad() const noexcept { return pad_; }

  private:
    std::string pin_number_;
    std::string pad_;
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

/** Stable manufacturer/orderable identity for a kernel-owned part definition. */
class OrderablePart {
  public:
    /** Construct an orderable part with stable physical identity and mappings. */
    OrderablePart(ManufacturerPart manufacturer_part, PackageRef package,
                  HashedFootprintReference footprint, std::vector<PartFootprintPad> footprint_pads,
                  std::vector<OrderablePinPadMapping> pin_pad_mappings,
                  std::vector<std::string> approved_alternate_mpns = {},
                  std::optional<PartModel3DReference> model_3d = std::nullopt,
                  std::optional<PartFootprintPolygon> footprint_courtyard = std::nullopt,
                  std::optional<PartFootprintPolygon> footprint_body = std::nullopt);

    /** Return the primary manufacturer part identity. */
    [[nodiscard]] const ManufacturerPart &manufacturer_part() const noexcept {
        return manufacturer_part_;
    }

    /** Return the package label. */
    [[nodiscard]] const PackageRef &package() const noexcept { return package_; }

    /** Return the hash-addressed footprint reference. */
    [[nodiscard]] const HashedFootprintReference &footprint() const noexcept { return footprint_; }

    /** Return footprint pad summaries in projection order. */
    [[nodiscard]] const std::vector<PartFootprintPad> &footprint_pads() const noexcept {
        return footprint_pads_;
    }

    /** Return the optional declared footprint courtyard polygon. */
    [[nodiscard]] const std::optional<PartFootprintPolygon> &footprint_courtyard() const noexcept {
        return footprint_courtyard_;
    }

    /** Return the optional declared footprint body or silk-outline polygon. */
    [[nodiscard]] const std::optional<PartFootprintPolygon> &footprint_body() const noexcept {
        return footprint_body_;
    }

    /** Return pin-number to pad mappings in deterministic insertion order. */
    [[nodiscard]] const std::vector<OrderablePinPadMapping> &pin_pad_mappings() const noexcept {
        return pin_pad_mappings_;
    }

    /** Return approved alternate manufacturer part numbers. */
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
    std::optional<PartFootprintPolygon> footprint_courtyard_;
    std::optional<PartFootprintPolygon> footprint_body_;
    std::vector<OrderablePinPadMapping> pin_pad_mappings_;
    std::vector<std::string> approved_alternate_mpns_;
    std::optional<PartModel3DReference> model_3d_;
};

/** Kernel-owned canonical part artifact model. */
class PartDefinition {
  public:
    /** Construct a kernel-owned part definition artifact. */
    PartDefinition(PartIdentity identity, std::vector<PartPin> pins,
                   ElectricalAttributeMap electrical_attributes, PartProvenance provenance,
                   std::vector<HashedSchematicSymbolReference> symbols,
                   OrderablePart orderable_part);

    /** Return stable part identity. */
    [[nodiscard]] const PartIdentity &identity() const noexcept { return identity_; }

    /** Return the canonical pin map. */
    [[nodiscard]] const std::vector<PartPin> &pins() const noexcept { return pins_; }

    /** Return part-level typed ratings. */
    [[nodiscard]] const ElectricalAttributeMap &electrical_attributes() const noexcept {
        return electrical_attributes_;
    }

    /** Return provenance fields. */
    [[nodiscard]] const PartProvenance &provenance() const noexcept { return provenance_; }

    /** Return hash-addressed schematic symbol projection references. */
    [[nodiscard]] const std::vector<HashedSchematicSymbolReference> &symbols() const noexcept {
        return symbols_;
    }

    /** Return the stable orderable physical identity. */
    [[nodiscard]] const OrderablePart &orderable_part() const noexcept { return orderable_part_; }

  private:
    void require_symbol_lineup_matches_pins() const;
    void require_orderable_mappings_match_pins() const;

    PartIdentity identity_;
    std::vector<PartPin> pins_;
    ElectricalAttributeMap electrical_attributes_;
    PartProvenance provenance_;
    std::vector<HashedSchematicSymbolReference> symbols_;
    OrderablePart orderable_part_;
};

/** Validate design-quality lineup concerns for an assembled or loaded part artifact. */
[[nodiscard]] DiagnosticReport validate_part_lineup(const PartDefinition &part);

} // namespace volt
