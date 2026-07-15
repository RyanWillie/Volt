#include <volt/schematic/schematic.hpp>

#include <volt/core/errors.hpp>

#include "schematic_storage.hpp"

#include <cstddef>
#include <memory>
#include <utility>

namespace volt {

Schematic::LibraryStorage::LibraryStorage()
    : LibraryStorage{std::make_shared<detail::SchematicLibraryState>()} {}

Schematic::LibraryStorage::LibraryStorage(std::shared_ptr<detail::SchematicLibraryState> state)
    : state_{std::move(state)} {}

Schematic::LibraryStorage::LibraryStorage(const LibraryStorage &other)
    : LibraryStorage{std::make_shared<detail::SchematicLibraryState>(other.state())} {}

Schematic::LibraryStorage &Schematic::LibraryStorage::operator=(const LibraryStorage &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::SchematicLibraryState>(other.state());
    }
    return *this;
}

[[nodiscard]] detail::SchematicLibraryState &Schematic::LibraryStorage::mutable_state() noexcept {
    return *state_;
}

[[nodiscard]] const detail::SchematicLibraryState &
Schematic::LibraryStorage::state() const noexcept {
    return *state_;
}

[[nodiscard]] SymbolDefId
Schematic::LibraryStorage::add_symbol_definition(SymbolDefinition definition) {
    for (std::size_t index = 0; index < state().symbol_definitions.size(); ++index) {
        const auto id = SymbolDefId{index};
        if (state().symbol_definitions.get(id).name() == definition.name()) {
            throw KernelLogicError{ErrorCode::DuplicateName,
                                   "Symbol definition name already exists",
                                   EntityRef::symbol_def(id)};
        }
    }
    return mutable_state().symbol_definitions.insert(std::move(definition));
}

[[nodiscard]] const SymbolDefinition &Schematic::LibraryStorage::get(SymbolDefId id) const {
    return state().symbol_definitions.get(id);
}

[[nodiscard]] std::size_t Schematic::LibraryStorage::size() const noexcept {
    return state().symbol_definitions.size();
}

void Schematic::LibraryStorage::require(SymbolDefId id) const {
    if (!state().symbol_definitions.contains(id)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Symbol definition ID does not belong to this schematic",
                               EntityRef::symbol_def(id)};
    }
}

} // namespace volt
