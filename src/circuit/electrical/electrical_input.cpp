#include <volt/electrical/electrical_input.hpp>

#include <cstddef>
#include <memory>
#include <utility>

#include <volt/core/errors.hpp>

namespace volt {

struct ElectricalInput::Storage {
    Circuit circuit;
    ElectricalInputIdentity identity;
    std::vector<std::optional<PartDefinition>> parts;
};

ElectricalInput::ElectricalInput(Circuit circuit, ElectricalInputIdentity identity,
                                 std::vector<std::optional<PartDefinition>> parts) {
    const auto count = circuit.all<ComponentId>().size();
    if (parts.size() != count) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical input resolved Parts must match occurrence count"};
    }

    for (std::size_t index = 0; index < count; ++index) {
        const auto occurrence = ComponentId{index};
        const auto &instance = circuit.get(occurrence);
        const auto &resolved = parts[index];
        if (!instance.selected_library_part_ref().has_value()) {
            if (resolved.has_value()) {
                throw KernelLogicError{
                    ErrorCode::CrossReferenceViolation,
                    "Electrical input cannot resolve a Part for an unselected occurrence",
                    EntityRef::component(occurrence)};
            }
            continue;
        }
        if (!resolved.has_value()) {
            continue;
        }

        const auto &reference = *instance.selected_library_part_ref();
        const auto &part = *resolved;
        if (part.content_identity() != reference.part_digest() ||
            part.identity().namespace_name() != reference.library_namespace() ||
            part.identity().name() != reference.part_key().value()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Electrical input resolved Part differs from exact selection",
                                   EntityRef::component(occurrence)};
        }
        const auto &component = circuit.get(instance.definition());
        if (part.implemented_component() != component.content_identity()) {
            throw KernelLogicError{
                ErrorCode::CrossReferenceViolation,
                "Electrical input resolved Part implements another component definition",
                EntityRef::component(occurrence)};
        }
    }

    storage_ = std::make_shared<const Storage>(
        Storage{std::move(circuit), std::move(identity), std::move(parts)});
}

const Circuit &ElectricalInput::circuit() const & { return storage_->circuit; }

const ElectricalInputIdentity &ElectricalInput::identity() const & { return storage_->identity; }

const PartDefinition *ElectricalInput::part(ComponentId occurrence) const & {
    static_cast<void>(storage_->circuit.get(occurrence));
    const auto &part = storage_->parts.at(occurrence.index());
    return part.has_value() ? &*part : nullptr;
}

} // namespace volt
