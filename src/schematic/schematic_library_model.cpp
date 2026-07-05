#include <volt/schematic/schematic_library_model.hpp>

#include <volt/core/errors.hpp>

#include "schematic_storage.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace volt {

SchematicLibraryModel::SchematicLibraryModel()
    : SchematicLibraryModel{std::make_shared<detail::SchematicLibraryState>()} {}

SchematicLibraryModel::SchematicLibraryModel(
    std::shared_ptr<const detail::SchematicLibraryState> state)
    : state_{std::move(state)} {}

SchematicLibraryModel::SchematicLibraryModel(const SchematicLibraryModel &other)
    : SchematicLibraryModel{std::make_shared<detail::SchematicLibraryState>(other.state())} {}

SchematicLibraryModel::SchematicLibraryModel(SchematicLibraryModel &&other) noexcept = default;

SchematicLibraryModel &SchematicLibraryModel::operator=(const SchematicLibraryModel &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::SchematicLibraryState>(other.state());
    }
    return *this;
}

SchematicLibraryModel &
SchematicLibraryModel::operator=(SchematicLibraryModel &&other) noexcept = default;

SchematicLibraryModel::~SchematicLibraryModel() = default;

Schematic::LibraryStorage::LibraryStorage()
    : LibraryStorage{std::make_shared<detail::SchematicLibraryState>()} {}

Schematic::LibraryStorage::LibraryStorage(std::shared_ptr<detail::SchematicLibraryState> state)
    : SchematicLibraryModel{state}, state_{std::move(state)} {}

Schematic::LibraryStorage::LibraryStorage(const LibraryStorage &other)
    : LibraryStorage{std::make_shared<detail::SchematicLibraryState>(other.state())} {}

Schematic::LibraryStorage &Schematic::LibraryStorage::operator=(const LibraryStorage &other) {
    if (this != &other) {
        auto replacement =
            LibraryStorage{std::make_shared<detail::SchematicLibraryState>(other.state())};
        *this = std::move(replacement);
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
    const auto existing = symbol_definition_by_name(definition.name());
    if (existing.has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Symbol definition name already exists",
                               EntityRef::symbol_def(existing.value())};
    }

    return mutable_state().symbol_definitions.insert(std::move(definition));
}

[[nodiscard]] std::optional<SymbolDefId>
SchematicLibraryModel::symbol_definition_by_name(const std::string &name) const {
    for (std::size_t index = 0; index < state().symbol_definitions.size(); ++index) {
        const auto id = SymbolDefId{index};
        if (state().symbol_definitions.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] const SymbolDefinition &
SchematicLibraryModel::symbol_definition(SymbolDefId id) const {
    return state().symbol_definitions.get(id);
}

[[nodiscard]] std::size_t SchematicLibraryModel::symbol_definition_count() const noexcept {
    return state().symbol_definitions.size();
}

void SchematicLibraryModel::require_symbol_definition(SymbolDefId symbol_definition) const {
    if (!state().symbol_definitions.contains(symbol_definition)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Symbol definition ID does not belong to this schematic",
                               EntityRef::symbol_def(symbol_definition)};
    }
}

[[nodiscard]] const detail::SchematicLibraryState &SchematicLibraryModel::state() const noexcept {
    return *state_;
}

} // namespace volt
