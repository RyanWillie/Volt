#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/ids.hpp>

namespace volt {

/** Broad role of a PCB board layer. */
enum class BoardLayerRole {
    Copper,
    SolderMask,
    Paste,
    Silkscreen,
    Fabrication,
    EdgeCuts,
    Drill,
    Mechanical,
    Courtyard,
    Keepout,
};

/** Physical side or scope of a board layer. */
enum class BoardLayerSide {
    Top,
    Bottom,
    Inner,
    Both,
    None,
};

/** Layer metadata owned by a board projection. */
class BoardLayer {
  public:
    /** Construct a board layer with deterministic metadata. */
    BoardLayer(std::string name, BoardLayerRole role, BoardLayerSide side,
               double thickness_mm = 0.0, bool enabled = true);

    /** Return the board-local layer name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the broad layer role. */
    [[nodiscard]] BoardLayerRole role() const noexcept { return role_; }

    /** Return the physical layer side. */
    [[nodiscard]] BoardLayerSide side() const noexcept { return side_; }

    /** Return optional physical thickness metadata in millimeters. */
    [[nodiscard]] double thickness_mm() const noexcept { return thickness_mm_; }

    /** Return whether this layer participates in board output. */
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    /** Set finished copper weight in ounces per square foot; copper layers only. */
    void set_copper_weight_oz(double weight_oz);

    /** Return finished copper weight in ounces per square foot, if present. */
    [[nodiscard]] std::optional<double> copper_weight_oz() const noexcept {
        return copper_weight_oz_;
    }

  private:
    std::string name_;
    BoardLayerRole role_;
    BoardLayerSide side_;
    double thickness_mm_;
    bool enabled_;
    std::optional<double> copper_weight_oz_;
};

/** Dielectric material between two adjacent copper layers in the stackup. */
class BoardDielectric {
  public:
    /** Construct a dielectric with physical thickness and relative permittivity. */
    BoardDielectric(double thickness_mm, double relative_permittivity);

    /** Return the dielectric thickness in millimeters. */
    [[nodiscard]] double thickness_mm() const noexcept { return thickness_mm_; }

    /** Return the relative permittivity (dielectric constant). */
    [[nodiscard]] double relative_permittivity() const noexcept { return relative_permittivity_; }

  private:
    double thickness_mm_;
    double relative_permittivity_;
};

/** Ordered stack of board layers plus total board thickness metadata. */
class LayerStack {
  public:
    /** Construct a deterministic non-empty layer stack. */
    LayerStack(std::vector<BoardLayerId> layers, double board_thickness_mm,
               std::vector<BoardDielectric> dielectrics = {});

    /** Return the ordered board layer IDs. */
    [[nodiscard]] const std::vector<BoardLayerId> &layers() const noexcept { return layers_; }

    /** Return total board thickness metadata in millimeters. */
    [[nodiscard]] double board_thickness_mm() const noexcept { return board_thickness_mm_; }

    /** Return dielectrics between adjacent copper layers in stack order; may be empty. */
    [[nodiscard]] const std::vector<BoardDielectric> &dielectrics() const noexcept {
        return dielectrics_;
    }

  private:
    std::vector<BoardLayerId> layers_;
    double board_thickness_mm_;
    std::vector<BoardDielectric> dielectrics_;
};

} // namespace volt
