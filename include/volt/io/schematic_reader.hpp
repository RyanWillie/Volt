#pragma once

#include <istream>
#include <string_view>

#include <volt/circuit/circuit.hpp>
#include <volt/schematic/schematic_document.hpp>

namespace volt::io {

/** Read a schematic projection from a JSON string, rejecting structurally invalid input. */
[[nodiscard]] Schematic read_schematic_text(std::string_view text, const Circuit &circuit);

/** Read a schematic projection from a JSON stream, rejecting structurally invalid input. */
[[nodiscard]] Schematic read_schematic(std::istream &input, const Circuit &circuit);

/** Read a schematic document from a JSON string, rejecting structurally invalid input. */
[[nodiscard]] SchematicDocument read_schematic_document_text(std::string_view text,
                                                             const Circuit &circuit);

/** Read a schematic document from a JSON stream, rejecting structurally invalid input. */
[[nodiscard]] SchematicDocument read_schematic_document(std::istream &input,
                                                        const Circuit &circuit);

} // namespace volt::io
