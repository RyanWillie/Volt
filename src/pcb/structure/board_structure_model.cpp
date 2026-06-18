#include <volt/pcb/structure/board_structure_model.hpp>

#include "../board_storage.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace volt {

BoardStructureModel::BoardStructureModel()
    : BoardStructureModel{std::make_shared<detail::BoardStructureState>()} {}

BoardStructureModel::BoardStructureModel(std::shared_ptr<const detail::BoardStructureState> state)
    : state_{std::move(state)} {}

BoardStructureModel::BoardStructureModel(const BoardStructureModel &other)
    : BoardStructureModel{std::make_shared<detail::BoardStructureState>(other.state())} {}

BoardStructureModel::BoardStructureModel(BoardStructureModel &&other) noexcept = default;

BoardStructureModel &BoardStructureModel::operator=(const BoardStructureModel &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::BoardStructureState>(other.state());
    }
    return *this;
}

BoardStructureModel &BoardStructureModel::operator=(BoardStructureModel &&other) noexcept = default;

BoardStructureModel::~BoardStructureModel() = default;

Board::StructureStorage::StructureStorage()
    : StructureStorage{std::make_shared<detail::BoardStructureState>()} {}

Board::StructureStorage::StructureStorage(std::shared_ptr<detail::BoardStructureState> state)
    : BoardStructureModel{state}, state_{std::move(state)} {}

Board::StructureStorage::StructureStorage(const StructureStorage &other)
    : StructureStorage{std::make_shared<detail::BoardStructureState>(other.state())} {}

Board::StructureStorage &Board::StructureStorage::operator=(const StructureStorage &other) {
    if (this != &other) {
        auto replacement =
            StructureStorage{std::make_shared<detail::BoardStructureState>(other.state())};
        *this = std::move(replacement);
    }
    return *this;
}

[[nodiscard]] detail::BoardStructureState &Board::StructureStorage::mutable_state() noexcept {
    return *state_;
}

[[nodiscard]] const detail::BoardStructureState &Board::StructureStorage::state() const noexcept {
    return *state_;
}

[[nodiscard]] BoardLayerId Board::StructureStorage::add_layer(BoardLayer layer) {
    if (layer_by_name(layer.name()).has_value()) {
        throw std::logic_error{"Board layer name already exists"};
    }

    return mutable_state().layers.insert(std::move(layer));
}

void Board::StructureStorage::set_layer_stack(LayerStack stack) {
    auto copper_count = std::size_t{0};
    for (const auto layer : stack.layers()) {
        require_layer(layer);
        if (state().layers.get(layer).role() == BoardLayerRole::Copper) {
            ++copper_count;
        }
    }
    if (!stack.dielectrics().empty() && stack.dielectrics().size() + 1 != copper_count) {
        throw std::invalid_argument{
            "Layer stack dielectrics must sit between adjacent copper layers"};
    }
    mutable_state().layer_stack = std::move(stack);
}

void Board::StructureStorage::set_outline(BoardOutline outline) {
    mutable_state().outline = std::move(outline);
}

void Board::StructureStorage::set_design_rules(BoardDesignRules rules) {
    mutable_state().design_rules = rules;
}

void Board::StructureStorage::set_capability_profile(BoardCapabilityProfile profile) {
    mutable_state().capability_profile = std::move(profile);
}

[[nodiscard]] BoardFeatureId Board::StructureStorage::add_feature(BoardFeature feature) {
    return mutable_state().features.insert(std::move(feature));
}

[[nodiscard]] const BoardLayer &BoardStructureModel::layer(BoardLayerId id) const {
    return state().layers.get(id);
}

[[nodiscard]] std::size_t BoardStructureModel::layer_count() const noexcept {
    return state().layers.size();
}

[[nodiscard]] const std::optional<LayerStack> &BoardStructureModel::layer_stack() const noexcept {
    return state().layer_stack;
}

[[nodiscard]] const std::optional<BoardOutline> &BoardStructureModel::outline() const noexcept {
    return state().outline;
}

[[nodiscard]] const BoardDesignRules &BoardStructureModel::design_rules() const noexcept {
    return state().design_rules;
}

[[nodiscard]] const std::optional<BoardCapabilityProfile> &
BoardStructureModel::capability_profile() const noexcept {
    return state().capability_profile;
}

[[nodiscard]] const BoardFeature &BoardStructureModel::feature(BoardFeatureId id) const {
    return state().features.get(id);
}

[[nodiscard]] std::size_t BoardStructureModel::feature_count() const noexcept {
    return state().features.size();
}

void BoardStructureModel::require_layer(BoardLayerId layer_id) const {
    if (!state().layers.contains(layer_id)) {
        throw std::out_of_range{"Board layer ID does not belong to this board"};
    }
}

[[nodiscard]] std::optional<BoardLayerId>
BoardStructureModel::layer_by_name(const std::string &name) const {
    for (std::size_t index = 0; index < state().layers.size(); ++index) {
        const auto id = BoardLayerId{index};
        if (state().layers.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] const detail::BoardStructureState &BoardStructureModel::state() const noexcept {
    return *state_;
}

} // namespace volt
