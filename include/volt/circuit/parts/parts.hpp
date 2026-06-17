#pragma once

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/mutation_access.hpp>
#include <volt/core/properties.hpp>

namespace volt {

class ElectricalModel;

/** Manufacturer-specific ordering identity for a selected physical part. */
class ManufacturerPart {
  public:
    /** Construct a manufacturer part from non-empty manufacturer and part-number labels. */
    ManufacturerPart(std::string manufacturer, std::string part_number);

    /** Return the selected part manufacturer name. */
    [[nodiscard]] const std::string &manufacturer() const noexcept { return manufacturer_; }

    /** Return the selected manufacturer part number. */
    [[nodiscard]] const std::string &part_number() const noexcept { return part_number_; }

    /** Return whether two manufacturer parts name the same selected physical part. */
    [[nodiscard]] friend bool operator==(const ManufacturerPart &lhs,
                                         const ManufacturerPart &rhs) noexcept {
        return lhs.manufacturer_ == rhs.manufacturer_ && lhs.part_number_ == rhs.part_number_;
    }

  private:
    std::string manufacturer_;
    std::string part_number_;
};

/** Package label for the selected physical form factor, such as 0603 or SOIC-8. */
class PackageRef {
  public:
    /** Construct a non-empty package reference. */
    explicit PackageRef(std::string value);

    /** Return the package reference label. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two package references carry the same label. */
    [[nodiscard]] friend bool operator==(const PackageRef &lhs, const PackageRef &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::string value_;
};

/** Library-qualified reference to a footprint definition. */
class FootprintRef {
  public:
    /** Construct a footprint reference from non-empty library and footprint names. */
    FootprintRef(std::string library, std::string name);

    /** Return the footprint library name. */
    [[nodiscard]] const std::string &library() const noexcept { return library_; }

    /** Return the footprint name inside the library. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return whether two footprint references point at the same footprint. */
    [[nodiscard]] friend bool operator==(const FootprintRef &lhs,
                                         const FootprintRef &rhs) noexcept {
        return lhs.library_ == rhs.library_ && lhs.name_ == rhs.name_;
    }

  private:
    std::string library_;
    std::string name_;
};

/** Mapping from a logical pin definition to a physical footprint pad label. */
class PinPadMapping {
  public:
    /** Construct a pin-to-pad mapping with a non-empty pad label. */
    PinPadMapping(PinDefId pin, std::string pad);

    /** Return the logical pin definition being mapped. */
    [[nodiscard]] PinDefId pin() const noexcept { return pin_; }

    /** Return the physical footprint pad label. */
    [[nodiscard]] const std::string &pad() const noexcept { return pad_; }

    /** Return whether two mappings have the same logical pin and physical pad. */
    [[nodiscard]] friend bool operator==(const PinPadMapping &lhs,
                                         const PinPadMapping &rhs) noexcept {
        return lhs.pin_ == rhs.pin_ && lhs.pad_ == rhs.pad_;
    }

  private:
    PinDefId pin_;
    std::string pad_;
};

/** Reusable 3D asset metadata attached to a selected physical part. */
class PartModel3D {
  public:
    /** Construct a selected-part 3D model reference from a file name and footprint-relative pose.
     */
    PartModel3D(std::string format, std::string file_name, std::array<double, 3> translation_mm,
                double rotation_deg);

    /** Return the normalized asset format, such as glb or step. */
    [[nodiscard]] const std::string &format() const noexcept { return format_; }

    /** Return the source file name carried in logical model metadata. */
    [[nodiscard]] const std::string &file_name() const noexcept { return file_name_; }

    /** Return the footprint-relative model translation in millimeters. */
    [[nodiscard]] const std::array<double, 3> &translation_mm() const noexcept {
        return translation_mm_;
    }

    /** Return the footprint-relative model rotation around the board normal in degrees. */
    [[nodiscard]] double rotation_deg() const noexcept { return rotation_deg_; }

    /** Return whether two selected-part model declarations are identical. */
    [[nodiscard]] friend bool operator==(const PartModel3D &lhs, const PartModel3D &rhs) noexcept {
        return lhs.format_ == rhs.format_ && lhs.file_name_ == rhs.file_name_ &&
               lhs.translation_mm_ == rhs.translation_mm_ && lhs.rotation_deg_ == rhs.rotation_deg_;
    }

  private:
    std::string format_;
    std::string file_name_;
    std::array<double, 3> translation_mm_;
    double rotation_deg_;
};

/** Selected physical implementation for a logical component definition. */
class PhysicalPart {
  public:
    /**
     * Construct a physical part selection with manufacturer identity, package, footprint,
     * pin/pad mappings, and extensible properties.
     */
    PhysicalPart(ManufacturerPart manufacturer_part, PackageRef package, FootprintRef footprint,
                 std::vector<PinPadMapping> pin_pad_mappings, PropertyMap properties = {},
                 std::optional<PartModel3D> model_3d = std::nullopt,
                 std::vector<std::string> approved_alternate_mpns = {});

    /** Return the selected manufacturer part identity. */
    [[nodiscard]] const ManufacturerPart &manufacturer_part() const noexcept;

    /** Return the selected package reference. */
    [[nodiscard]] const PackageRef &package() const noexcept { return package_; }

    /** Return the selected footprint reference. */
    [[nodiscard]] const FootprintRef &footprint() const noexcept { return footprint_; }

    /** Return logical pin to physical pad mappings in deterministic insertion order. */
    [[nodiscard]] const std::vector<PinPadMapping> &pin_pad_mappings() const noexcept;

    /** Return extensible metadata properties for this physical part selection. */
    [[nodiscard]] const PropertyMap &properties() const noexcept { return properties_; }

    /** Return typed electrical attributes for this physical part selection. */
    [[nodiscard]] const ElectricalAttributeMap &electrical_attributes() const noexcept;

    /** Return optional selected-part 3D model metadata. */
    [[nodiscard]] const std::optional<PartModel3D> &model_3d() const noexcept { return model_3d_; }

    /** Return approved alternate manufacturer part numbers for this selected part. */
    [[nodiscard]] const std::vector<std::string> &approved_alternate_mpns() const noexcept {
        return approved_alternate_mpns_;
    }

    /** Set typed electrical metadata for this physical part selection. */
    void set_electrical_attribute(detail::KernelMutationAccess access,
                                  const ElectricalAttributeSpec &spec,
                                  ElectricalAttributeValue value);

  private:
    ManufacturerPart manufacturer_part_;
    PackageRef package_;
    FootprintRef footprint_;
    std::vector<PinPadMapping> pin_pad_mappings_;
    PropertyMap properties_;
    ElectricalAttributeMap electrical_attributes_;
    std::optional<PartModel3D> model_3d_;
    std::vector<std::string> approved_alternate_mpns_;
};

} // namespace volt
