#include <volt/pcb/board_layers.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace volt {

BoardLayer::BoardLayer(std::string name, BoardLayerRole role, BoardLayerSide side,
                       double thickness_mm, bool enabled)
    : name_{std::move(name)}, role_{role}, side_{side}, thickness_mm_{thickness_mm},
      enabled_{enabled} {
    if (name_.empty()) {
        throw std::invalid_argument{"Board layer name must not be empty"};
    }
    if (!std::isfinite(thickness_mm_)) {
        throw std::invalid_argument{"Board layer thickness must be finite"};
    }
    if (thickness_mm_ < 0.0) {
        throw std::invalid_argument{"Board layer thickness must not be negative"};
    }
}

void BoardLayer::set_copper_weight_oz(double weight_oz) {
    if (role_ != BoardLayerRole::Copper) {
        throw std::invalid_argument{"Copper weight applies only to copper layers"};
    }
    if (!std::isfinite(weight_oz) || weight_oz <= 0.0) {
        throw std::invalid_argument{"Copper weight must be finite and positive"};
    }

    copper_weight_oz_ = weight_oz;
}

BoardDielectric::BoardDielectric(double thickness_mm, double relative_permittivity)
    : thickness_mm_{thickness_mm}, relative_permittivity_{relative_permittivity} {
    if (!std::isfinite(thickness_mm_) || thickness_mm_ <= 0.0) {
        throw std::invalid_argument{"Dielectric thickness must be finite and positive"};
    }
    if (!std::isfinite(relative_permittivity_) || relative_permittivity_ < 1.0) {
        throw std::invalid_argument{"Dielectric relative permittivity must be at least 1"};
    }
}

LayerStack::LayerStack(std::vector<BoardLayerId> layers, double board_thickness_mm,
                       std::vector<BoardDielectric> dielectrics)
    : layers_{std::move(layers)}, board_thickness_mm_{board_thickness_mm},
      dielectrics_{std::move(dielectrics)} {
    if (layers_.empty()) {
        throw std::invalid_argument{"Layer stack must contain at least one layer"};
    }
    if (!std::isfinite(board_thickness_mm_)) {
        throw std::invalid_argument{"Board thickness must be finite"};
    }
    if (board_thickness_mm_ <= 0.0) {
        throw std::invalid_argument{"Board thickness must be positive"};
    }

    auto sorted = layers_;
    std::sort(sorted.begin(), sorted.end(),
              [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Layer stack must not contain duplicate layers"};
    }
}

} // namespace volt
