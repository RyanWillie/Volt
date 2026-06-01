#pragma once

#include <string>
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

  private:
    std::string name_;
    BoardLayerRole role_;
    BoardLayerSide side_;
    double thickness_mm_;
    bool enabled_;
};

/** Ordered stack of board layers plus total board thickness metadata. */
class LayerStack {
  public:
    /** Construct a deterministic non-empty layer stack. */
    LayerStack(std::vector<BoardLayerId> layers, double board_thickness_mm);

    /** Return the ordered board layer IDs. */
    [[nodiscard]] const std::vector<BoardLayerId> &layers() const noexcept { return layers_; }

    /** Return total board thickness metadata in millimeters. */
    [[nodiscard]] double board_thickness_mm() const noexcept { return board_thickness_mm_; }

  private:
    std::vector<BoardLayerId> layers_;
    double board_thickness_mm_;
};

} // namespace volt
