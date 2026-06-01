#include <volt/schematic/schematic_document.hpp>

#include <utility>

namespace volt {

SchematicDocument::SchematicDocument(const Circuit &circuit) : schematic_{circuit} {}
SchematicDocument::SchematicDocument(CircuitView circuit) : schematic_{circuit} {}
SchematicDocument::SchematicDocument(Schematic schematic) : schematic_{std::move(schematic)} {}
void SchematicDocument::replace_schematic(Schematic schematic) {
    schematic_.replace_with(std::move(schematic));
}

} // namespace volt
