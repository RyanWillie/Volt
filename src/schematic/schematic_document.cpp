#include <volt/schematic/schematic_document.hpp>

#include <volt/core/errors.hpp>

#include <utility>

namespace volt {

SchematicDocument::SchematicDocument(const Circuit &circuit) : schematic_{circuit} {}

SchematicDocument::SchematicDocument(Schematic schematic) : schematic_{std::move(schematic)} {}

void SchematicDocument::replace_schematic(Schematic schematic) {
    if (&schematic.circuit() != &circuit()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Schematic replacement must reference the same logical circuit"};
    }
    schematic_.emplace(std::move(schematic));
}

} // namespace volt
