#pragma once

#include <volt/circuit/circuit.hpp>

#include <utility>

namespace volt::python {

/** Owning authoring input shared by model construction and exact Part lowering. */
class PyPartComponentDefinition {
  public:
    explicit PyPartComponentDefinition(ComponentSpec spec)
        : spec_{std::move(spec)}, definition_{[this] {
              auto circuit = Circuit{};
              const auto &definition = circuit.get(circuit.define_component(spec_));
              for (const auto pin : definition.pins()) {
                  pins_.push_back(circuit.get(pin));
              }
              return definition;
          }()} {}

    [[nodiscard]] const ComponentSpec &spec() const noexcept { return spec_; }

    [[nodiscard]] const std::vector<PinDefinition> &pins() const noexcept { return pins_; }

    [[nodiscard]] const ComponentDefinition &definition() const noexcept { return definition_; }

  private:
    ComponentSpec spec_;
    std::vector<PinDefinition> pins_;
    ComponentDefinition definition_;
};

} // namespace volt::python
