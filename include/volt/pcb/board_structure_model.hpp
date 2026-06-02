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

class BoardStructureModel {
  public:
    [[nodiscard]] BoardLayerId add_layer(BoardLayer layer);

    void set_layer_stack(LayerStack stack);

    void set_outline(BoardOutline outline);

    void set_design_rules(BoardDesignRules rules);

    [[nodiscard]] BoardFeatureId add_feature(BoardFeature feature);

    [[nodiscard]] const BoardLayer &layer(BoardLayerId id) const;

    [[nodiscard]] std::size_t layer_count() const noexcept;

    [[nodiscard]] const std::optional<LayerStack> &layer_stack() const noexcept;

    [[nodiscard]] const std::optional<BoardOutline> &outline() const noexcept;

    [[nodiscard]] const BoardDesignRules &design_rules() const noexcept;

    [[nodiscard]] const BoardFeature &feature(BoardFeatureId id) const;

    [[nodiscard]] std::size_t feature_count() const noexcept;

    void require_layer(BoardLayerId layer) const;

    [[nodiscard]] std::optional<BoardLayerId> layer_by_name(const std::string &name) const;

  private:
    EntityTable<BoardLayer, BoardLayerId> layers_;
    std::optional<LayerStack> layer_stack_;
    std::optional<BoardOutline> outline_;
    BoardDesignRules design_rules_;
    EntityTable<BoardFeature, BoardFeatureId> features_;
};

} // namespace volt
