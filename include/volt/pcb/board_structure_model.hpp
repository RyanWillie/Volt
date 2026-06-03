#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/board_copper.hpp>
#include <volt/pcb/board_features.hpp>
#include <volt/pcb/board_layers.hpp>
#include <volt/pcb/board_outline.hpp>

namespace volt {

/**
 * Owns board structure data: layers, stackup, outline, design rules, and mechanical features.
 *
 * Responsibility: stores the physical board frame (copper/technical layers, stackup ordering,
 *   board outline, DRC rule values, holes/slots/cutouts/fiducials).
 * Invariants: layer references resolve within the stackup; outline/feature geometry is finite.
 * Collaborators: composed by Board; supplies design-rule values consumed by DRC; acyclic.
 */
class BoardStructureModel {
  public:
    /** Add a board layer and return its stable board-local ID. */
    [[nodiscard]] BoardLayerId add_layer(BoardLayer layer);

    /** Replace the board layer stack. */
    void set_layer_stack(LayerStack stack);

    /** Replace the board outline. */
    void set_outline(BoardOutline outline);

    /** Replace the board design rules. */
    void set_design_rules(BoardDesignRules rules);

    /** Add a non-copper board feature and return its stable board-local ID. */
    [[nodiscard]] BoardFeatureId add_feature(BoardFeature feature);

    /** Return a board layer by board-local ID. */
    [[nodiscard]] const BoardLayer &layer(BoardLayerId id) const;

    /** Return the number of board layers. */
    [[nodiscard]] std::size_t layer_count() const noexcept;

    /** Return the optional board layer stack. */
    [[nodiscard]] const std::optional<LayerStack> &layer_stack() const noexcept;

    /** Return the optional board outline. */
    [[nodiscard]] const std::optional<BoardOutline> &outline() const noexcept;

    /** Return the active board design rules. */
    [[nodiscard]] const BoardDesignRules &design_rules() const noexcept;

    /** Return a board feature by board-local ID. */
    [[nodiscard]] const BoardFeature &feature(BoardFeatureId id) const;

    /** Return the number of board features. */
    [[nodiscard]] std::size_t feature_count() const noexcept;

    /** Require that a layer ID belongs to this model. */
    void require_layer(BoardLayerId layer) const;

    /** Return the board layer with the requested name, if present. */
    [[nodiscard]] std::optional<BoardLayerId> layer_by_name(const std::string &name) const;

  private:
    EntityTable<BoardLayer, BoardLayerId> layers_;
    std::optional<LayerStack> layer_stack_;
    std::optional<BoardOutline> outline_;
    BoardDesignRules design_rules_;
    EntityTable<BoardFeature, BoardFeatureId> features_;
};

} // namespace volt
