#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <volt/core/ids.hpp>
#include <volt/pcb/copper/board_copper.hpp>
#include <volt/pcb/features/board_features.hpp>
#include <volt/pcb/geometry/board_outline.hpp>
#include <volt/pcb/layers/board_layers.hpp>

namespace volt {

namespace detail {
struct BoardStructureState;
}

/**
 * Owns board structure data: layers, stackup, outline, design rules, and mechanical features.
 *
 * Responsibility: stores the physical board frame (copper/technical layers, stackup ordering,
 *   board outline, DRC rule values, holes/slots/cutouts/circles).
 * Invariants: layer references resolve within the stackup; outline/feature geometry is finite.
 * Collaborators: composed by Board; supplies design-rule values consumed by DRC; acyclic.
 */
class BoardStructureModel {
  public:
    /** Construct an empty board-structure facade. */
    BoardStructureModel();
    /** Copy board-structure state. */
    BoardStructureModel(const BoardStructureModel &other);
    /** Move board-structure state. */
    BoardStructureModel(BoardStructureModel &&other) noexcept;
    /** Copy board-structure state. */
    BoardStructureModel &operator=(const BoardStructureModel &other);
    /** Move board-structure state. */
    BoardStructureModel &operator=(BoardStructureModel &&other) noexcept;
    /** Destroy board-structure state. */
    ~BoardStructureModel();

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

    /** Return the optional board capability profile snapshot. */
    [[nodiscard]] const std::optional<BoardCapabilityProfile> &capability_profile() const noexcept;

    /** Return a board feature by board-local ID. */
    [[nodiscard]] const BoardFeature &feature(BoardFeatureId id) const;

    /** Return the number of board features. */
    [[nodiscard]] std::size_t feature_count() const noexcept;

    /** Require that a layer ID belongs to this model. */
    void require_layer(BoardLayerId layer) const;

    /** Return the board layer with the requested name, if present. */
    [[nodiscard]] std::optional<BoardLayerId> layer_by_name(const std::string &name) const;

  protected:
    /** Construct a read-only facade over owner-private storage. */
    explicit BoardStructureModel(std::shared_ptr<const detail::BoardStructureState> state);

  private:
    [[nodiscard]] const detail::BoardStructureState &state() const noexcept;

    std::shared_ptr<const detail::BoardStructureState> state_;
};

} // namespace volt
