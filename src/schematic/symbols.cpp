#include <volt/schematic/symbols.hpp>

#include <volt/core/errors.hpp>

namespace volt {

SchematicTextStyle::SchematicTextStyle(TextHorizontalAlignment horizontal_alignment,
                                       TextVerticalAlignment vertical_alignment,
                                       std::optional<double> font_size)
    : horizontal_alignment_{horizontal_alignment}, vertical_alignment_{vertical_alignment},
      font_size_{font_size} {
    if (font_size_.has_value() &&
        (!std::isfinite(font_size_.value()) || font_size_.value() <= 0.0)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Schematic text font size must be finite and positive"};
    }
}

[[nodiscard]] TextHorizontalAlignment SchematicTextStyle::horizontal_alignment() const noexcept {
    return horizontal_alignment_;
}

[[nodiscard]] TextVerticalAlignment SchematicTextStyle::vertical_alignment() const noexcept {
    return vertical_alignment_;
}

SymbolCircle::SymbolCircle(Point center, double radius) : center_{center}, radius_{radius} {
    if (!std::isfinite(radius_) || radius_ <= 0.0) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Symbol circle radius must be finite and positive"};
    }
}

SymbolArc::SymbolArc(Point center, double radius, double start_degrees, double sweep_degrees)
    : center_{center}, radius_{radius}, start_degrees_{start_degrees},
      sweep_degrees_{sweep_degrees} {
    if (!std::isfinite(radius_) || radius_ <= 0.0) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Symbol arc radius must be finite and positive"};
    }
    if (!std::isfinite(start_degrees_) || !std::isfinite(sweep_degrees_)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Symbol arc angles must be finite"};
    }
}

SymbolText::SymbolText(std::string text, Point anchor, SchematicOrientation orientation,
                       SchematicTextStyle style)
    : text_{std::move(text)}, anchor_{anchor}, orientation_{orientation}, style_{style} {
    if (text_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Symbol text must not be empty"};
    }
}

SymbolPin::SymbolPin(std::string name, std::string number, Point anchor,
                     SchematicOrientation orientation)
    : name_{std::move(name)}, number_{std::move(number)}, anchor_{anchor},
      orientation_{orientation} {
    if (name_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Symbol pin name must not be empty"};
    }
    if (number_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Symbol pin number must not be empty"};
    }
}

SymbolDefinition::SymbolDefinition(std::string name) : name_{std::move(name)} {
    if (name_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Symbol definition name must not be empty"};
    }
}

void SymbolDefinition::add_pin(SymbolPin pin) {
    const auto duplicate = std::any_of(pins_.begin(), pins_.end(), [&pin](const auto &other) {
        return other.number() == pin.number();
    });
    if (duplicate) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Symbol pin number already exists"};
    }

    pins_.push_back(std::move(pin));
}

void SymbolDefinition::add_primitive(SymbolPrimitive primitive) {
    primitives_.push_back(std::move(primitive));
}

[[nodiscard]] const std::vector<SymbolPrimitive> &SymbolDefinition::primitives() const noexcept {
    return primitives_;
}

} // namespace volt
