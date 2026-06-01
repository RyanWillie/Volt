#pragma once

#include <istream>
#include <string_view>

#include <volt/circuit/circuit.hpp>

namespace volt::io {

/** Read a logical circuit from a JSON string, rejecting structurally invalid input. */
[[nodiscard]] Circuit read_logical_circuit_text(std::string_view text);

/** Read a logical circuit from a JSON stream, rejecting structurally invalid input. */
[[nodiscard]] Circuit read_logical_circuit(std::istream &input);

} // namespace volt::io
