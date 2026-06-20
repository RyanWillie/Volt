#pragma once

#include <optional>
#include <string>
#include <vector>

#include <volt/core/diagnostics.hpp>
#include <volt/pcb/board.hpp>

namespace volt {

/** Vendor-specific rotation correction data for one footprint reference. */
class CplRotationOffset {
  public:
    /** Construct a finite rotation offset in degrees for a footprint. */
    CplRotationOffset(FootprintRef footprint, double rotation_deg);

    /** Return the footprint this offset applies to. */
    [[nodiscard]] const FootprintRef &footprint() const noexcept { return footprint_; }

    /** Return the rotation correction in degrees. */
    [[nodiscard]] double rotation_deg() const noexcept { return rotation_deg_; }

  private:
    FootprintRef footprint_;
    double rotation_deg_;
};

/** Options for projecting canonical CPL placement data. */
struct CplProjectionOptions {
    /** Data-only per-footprint rotation corrections. */
    std::vector<CplRotationOffset> rotation_offsets;
};

/** Orderable identity available to assembly handoff consumers. */
class CplPartIdentity {
  public:
    /** Construct projected selected-part identity. */
    CplPartIdentity(std::string manufacturer, std::string mpn, std::string package);

    /** Return manufacturer name. */
    [[nodiscard]] const std::string &manufacturer() const noexcept { return manufacturer_; }

    /** Return manufacturer part number. */
    [[nodiscard]] const std::string &mpn() const noexcept { return mpn_; }

    /** Return package label. */
    [[nodiscard]] const std::string &package() const noexcept { return package_; }

  private:
    std::string manufacturer_;
    std::string mpn_;
    std::string package_;
};

/** One deterministic component placement row in the canonical CPL projection. */
class CplRow {
  public:
    /** Construct one CPL row. */
    CplRow(ComponentPlacementId placement, ComponentId component, std::string reference,
           std::optional<FootprintRef> footprint, BoardSide side, BoardPoint position,
           double authored_rotation_deg, double rotation_offset_deg, double rotation_deg,
           std::optional<CplPartIdentity> part_identity);

    /** Return the source placement ID. */
    [[nodiscard]] ComponentPlacementId placement() const noexcept { return placement_; }

    /** Return the placed logical component ID. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the reference designator. */
    [[nodiscard]] const std::string &reference() const noexcept { return reference_; }

    /** Return selected footprint identity, when available. */
    [[nodiscard]] const std::optional<FootprintRef> &footprint() const noexcept {
        return footprint_;
    }

    /** Return the assembly side. */
    [[nodiscard]] BoardSide side() const noexcept { return side_; }

    /** Return board-space placement origin in millimeters. */
    [[nodiscard]] BoardPoint position() const noexcept { return position_; }

    /** Return authored board placement rotation before correction. */
    [[nodiscard]] double authored_rotation_deg() const noexcept { return authored_rotation_deg_; }

    /** Return data-supplied footprint rotation correction. */
    [[nodiscard]] double rotation_offset_deg() const noexcept { return rotation_offset_deg_; }

    /** Return final assembly rotation in degrees. */
    [[nodiscard]] double rotation_deg() const noexcept { return rotation_deg_; }

    /** Return selected part identity, when available. */
    [[nodiscard]] const std::optional<CplPartIdentity> &part_identity() const noexcept {
        return part_identity_;
    }

  private:
    ComponentPlacementId placement_;
    ComponentId component_;
    std::string reference_;
    std::optional<FootprintRef> footprint_;
    BoardSide side_;
    BoardPoint position_;
    double authored_rotation_deg_;
    double rotation_offset_deg_;
    double rotation_deg_;
    std::optional<CplPartIdentity> part_identity_;
};

/** Complete CPL projection, including diagnostics for assembly handoff gaps. */
class Cpl {
  public:
    /** Construct a CPL projection. */
    Cpl(std::vector<CplRow> rows, DiagnosticReport diagnostics);

    /** Return CPL rows sorted by reference designator. */
    [[nodiscard]] const std::vector<CplRow> &rows() const noexcept { return rows_; }

    /** Return diagnostics emitted while projecting assembly handoff data. */
    [[nodiscard]] const DiagnosticReport &diagnostics() const noexcept { return diagnostics_; }

  private:
    std::vector<CplRow> rows_;
    DiagnosticReport diagnostics_;
};

/** Project board placements into a deterministic canonical CPL using default options. */
[[nodiscard]] Cpl project_cpl(const Board &board, const FootprintLibrary &footprints);

/** Project board placements into a deterministic canonical CPL. */
[[nodiscard]] Cpl project_cpl(const Board &board, const FootprintLibrary &footprints,
                              const CplProjectionOptions &options);

} // namespace volt
