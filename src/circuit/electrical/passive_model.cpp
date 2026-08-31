#include <volt/electrical/passive_model.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <type_traits>

namespace volt {
namespace {

Quantity normalized_quantity(UnitDimension dimension, double value) {
    if (!std::isfinite(value)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical-model parameter normalization must be finite"};
    }
    return Quantity{dimension, value == 0.0 ? 0.0 : value};
}

std::optional<Tolerance> normalized_tolerance(const Quantity &nominal,
                                              const std::optional<Tolerance> &tolerance) {
    if (!tolerance) {
        return std::nullopt;
    }
    double minus = tolerance->minus().value();
    double plus = tolerance->plus().value();
    if (tolerance->mode() == ToleranceMode::Percent) {
        const double magnitude = std::abs(nominal.value());
        minus *= magnitude;
        plus *= magnitude;
    } else if (tolerance->minus().dimension() != nominal.dimension() ||
               tolerance->plus().dimension() != nominal.dimension()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical-model tolerance dimension must match nominal"};
    }
    return Tolerance::absolute(normalized_quantity(nominal.dimension(), minus),
                               normalized_quantity(nominal.dimension(), plus));
}

void validate_element(const ModelEndpoint &from, const ModelEndpoint &to,
                      const ModelParameter &parameter, UnitDimension dimension) {
    if (from == to) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical-model element endpoints must be distinct"};
    }
    if (parameter.nominal().dimension() != dimension) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical-model parameter dimension must match element"};
    }
    const auto in_domain = [dimension](double value) {
        return dimension == UnitDimension::Resistance ? value >= 0.0 : value > 0.0;
    };
    if (!in_domain(parameter.nominal().value())) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical-model nominal is outside the element domain"};
    }
    if (const auto bounds = parameter.bounds();
        bounds &&
        (!in_domain(bounds->minimum()->value()) || !in_domain(bounds->maximum()->value()))) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical-model tolerance bounds exceed the element domain"};
    }
}

const ModelElementKey &element_key(const ModelElement &element) {
    return std::visit([](const auto &value) -> const ModelElementKey & { return value.key(); },
                      element);
}

template <typename Range, typename KeyFn> void canonicalize_keys(Range &items, KeyFn key) {
    for (const auto &item : items) {
        if (std::invoke(key, item).value().empty()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Electrical-model key must not be empty"};
        }
    }
    std::ranges::sort(items, {}, key);
    if (std::adjacent_find(items.begin(), items.end(), [&](const auto &lhs, const auto &rhs) {
            return std::invoke(key, lhs) == std::invoke(key, rhs);
        }) != items.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Electrical-model collection contains duplicate keys"};
    }
}

} // namespace

ModelParameter::ModelParameter(Quantity nominal, std::optional<Tolerance> tolerance,
                               std::vector<ContentHash> evidence)
    : nominal_{normalized_quantity(nominal.dimension(), nominal.value())},
      tolerance_{normalized_tolerance(nominal_, tolerance)}, evidence_{std::move(evidence)} {
    static_cast<void>(bounds());
    std::ranges::sort(evidence_, {}, &ContentHash::value);
    evidence_.erase(std::unique(evidence_.begin(), evidence_.end()), evidence_.end());
}

std::optional<QuantityRange> ModelParameter::bounds() const {
    if (!tolerance_) {
        return std::nullopt;
    }
    return QuantityRange::bounded(
        normalized_quantity(nominal_.dimension(), nominal_.value() - tolerance_->minus().value()),
        normalized_quantity(nominal_.dimension(), nominal_.value() + tolerance_->plus().value()));
}

ResistanceElement::ResistanceElement(ModelElementKey key, ModelEndpoint from, ModelEndpoint to,
                                     ModelParameter parameter)
    : key_{std::move(key)}, from_{std::move(from)}, to_{std::move(to)},
      parameter_{std::move(parameter)} {
    validate_element(from_, to_, parameter_, UnitDimension::Resistance);
}

CapacitanceElement::CapacitanceElement(ModelElementKey key, ModelEndpoint from, ModelEndpoint to,
                                       ModelParameter parameter)
    : key_{std::move(key)}, from_{std::move(from)}, to_{std::move(to)},
      parameter_{std::move(parameter)} {
    validate_element(from_, to_, parameter_, UnitDimension::Capacitance);
}

InductanceElement::InductanceElement(ModelElementKey key, ModelEndpoint from, ModelEndpoint to,
                                     ModelParameter parameter)
    : key_{std::move(key)}, from_{std::move(from)}, to_{std::move(to)},
      parameter_{std::move(parameter)} {
    validate_element(from_, to_, parameter_, UnitDimension::Inductance);
}

PartElectricalModel::PartElectricalModel(const ComponentDefinition &component,
                                         std::vector<ModelTerminal> terminals,
                                         std::vector<ModelInternalNode> internal_nodes,
                                         std::vector<ModelElement> elements)
    : implemented_component_{component.content_identity()}, terminals_{std::move(terminals)},
      internal_nodes_{std::move(internal_nodes)}, elements_{std::move(elements)} {
    canonicalize_keys(terminals_, &ModelTerminal::key);
    canonicalize_keys(internal_nodes_, &ModelInternalNode::key);
    canonicalize_keys(elements_, element_key);
    if (elements_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical model must contain at least one element"};
    }

    const auto &pin_keys = component.contract().pin_keys();
    std::set<PinKey> unbound_pins{pin_keys.begin(), pin_keys.end()};
    std::set<ModelEndpoint> declared;
    for (const auto &terminal : terminals_) {
        if (unbound_pins.erase(terminal.pin()) != 1U) {
            throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                      "Electrical-model terminal has foreign or duplicate PinKey"};
        }
        declared.emplace(terminal.key());
    }
    if (!unbound_pins.empty()) {
        throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                  "Electrical model must cover every component PinKey"};
    }
    for (const auto &node : internal_nodes_) {
        declared.emplace(node.key());
    }

    auto unused = declared;
    for (const auto &element : elements_) {
        std::visit(
            [&](const auto &value) {
                for (const auto *endpoint : {&value.from(), &value.to()}) {
                    if (!declared.contains(*endpoint)) {
                        throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                                  "Electrical-model endpoint is not declared"};
                    }
                    unused.erase(*endpoint);
                }
            },
            element);
    }
    if (!unused.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical model contains an unused terminal or private node"};
    }
}

PartElectricalModelBuilder::PartElectricalModelBuilder(ComponentDefinition component)
    : component_{std::move(component)}, identity_{std::make_shared<const Identity>()} {}

PartElectricalModelBuilder::TerminalHandle
PartElectricalModelBuilder::terminal(ModelTerminalKey key, PinKey pin) {
    if (std::ranges::find(component_.contract().pin_keys(), pin) ==
        component_.contract().pin_keys().end()) {
        throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                  "Electrical-model terminal references a foreign PinKey"};
    }
    if (std::ranges::find(terminals_, key, &ModelTerminal::key) != terminals_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Electrical-model terminal key is already declared"};
    }
    if (std::ranges::find(terminals_, pin, &ModelTerminal::pin) != terminals_.end()) {
        throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                  "Electrical-model PinKey is already bound"};
    }
    terminals_.emplace_back(key, std::move(pin));
    return TerminalHandle{*this, std::move(key)};
}

PartElectricalModelBuilder::InternalNodeHandle
PartElectricalModelBuilder::internal_node(ModelInternalNodeKey key) {
    if (std::ranges::find(internal_nodes_, key, &ModelInternalNode::key) != internal_nodes_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Electrical-model private-node key is already declared"};
    }
    internal_nodes_.emplace_back(key);
    return InternalNodeHandle{*this, std::move(key)};
}

void PartElectricalModelBuilder::require_endpoint(const ModelEndpoint &endpoint) const {
    const bool known = std::visit(
        [&](const auto &key) {
            if constexpr (std::is_same_v<std::decay_t<decltype(key)>, ModelTerminalKey>) {
                return std::ranges::find(terminals_, key, &ModelTerminal::key) != terminals_.end();
            } else {
                return std::ranges::find(internal_nodes_, key, &ModelInternalNode::key) !=
                       internal_nodes_.end();
            }
        },
        endpoint);
    if (!known) {
        throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                  "Electrical-model handle key is not declared by this builder"};
    }
}

ModelEndpoint PartElectricalModelBuilder::resolve(const Endpoint &endpoint) const {
    return std::visit(
        [&](const auto &handle) -> ModelEndpoint {
            if (!handle.belongs_to(*this)) {
                throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                          "Electrical-model handle belongs to another builder"};
            }
            const ModelEndpoint result{handle.key()};
            require_endpoint(result);
            return result;
        },
        endpoint);
}

void PartElectricalModelBuilder::append(ModelElement element) {
    if (std::ranges::find(elements_, element_key(element), element_key) != elements_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Electrical-model element key is already declared"};
    }
    elements_.push_back(std::move(element));
}

PartElectricalModel PartElectricalModelBuilder::build() const {
    return PartElectricalModel{component_, terminals_, internal_nodes_, elements_};
}

} // namespace volt
