#include <volt/pcb/board_structure_model.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace volt {

[[nodiscard]] BoardLayerId BoardStructureModel::add_layer(BoardLayer layer) {
    if (layer_by_name(layer.name()).has_value()) {
        throw std::logic_error{"Board layer name already exists"};
    }

    return layers_.insert(std::move(layer));
}
void BoardStructureModel::set_layer_stack(LayerStack stack) {
    for (const auto layer : stack.layers()) {
        require_layer(layer);
    }
    layer_stack_ = std::move(stack);
}
void BoardStructureModel::set_outline(BoardOutline outline) { outline_ = std::move(outline); }
void BoardStructureModel::set_design_rules(BoardDesignRules rules) { design_rules_ = rules; }
[[nodiscard]] BoardFeatureId BoardStructureModel::add_feature(BoardFeature feature) {
    return features_.insert(std::move(feature));
}
[[nodiscard]] const BoardLayer &BoardStructureModel::layer(BoardLayerId id) const {
    return layers_.get(id);
}
[[nodiscard]] std::size_t BoardStructureModel::layer_count() const noexcept {
    return layers_.size();
}
[[nodiscard]] const std::optional<LayerStack> &BoardStructureModel::layer_stack() const noexcept {
    return layer_stack_;
}
[[nodiscard]] const std::optional<BoardOutline> &BoardStructureModel::outline() const noexcept {
    return outline_;
}
[[nodiscard]] const BoardDesignRules &BoardStructureModel::design_rules() const noexcept {
    return design_rules_;
}
[[nodiscard]] const BoardFeature &BoardStructureModel::feature(BoardFeatureId id) const {
    return features_.get(id);
}
[[nodiscard]] std::size_t BoardStructureModel::feature_count() const noexcept {
    return features_.size();
}
void BoardStructureModel::require_layer(BoardLayerId layer_id) const {
    if (!layers_.contains(layer_id)) {
        throw std::out_of_range{"Board layer ID does not belong to this board"};
    }
}
[[nodiscard]] std::optional<BoardLayerId>
BoardStructureModel::layer_by_name(const std::string &name) const {
    for (std::size_t index = 0; index < layers_.size(); ++index) {
        const auto id = BoardLayerId{index};
        if (layers_.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

} // namespace volt
