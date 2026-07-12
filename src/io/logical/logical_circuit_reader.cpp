#include <volt/io/logical/logical_circuit_reader.hpp>

#include <istream>
#include <sstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "logical_circuit_parser.hpp"
#include "logical_circuit_restoration.hpp"

namespace {

[[nodiscard]] volt::Circuit read_logical_circuit_document(const nlohmann::json &document) {
    auto plan = volt::io::detail::LogicalCircuitParser{document}.parse();
    return volt::io::detail::restore_logical_circuit(std::move(plan));
}

} // namespace

namespace volt::io {

[[nodiscard]] Circuit read_logical_circuit_text(std::string_view text) {
    return read_logical_circuit_document(nlohmann::json::parse(text.begin(), text.end()));
}

[[nodiscard]] Circuit read_logical_circuit(std::istream &input) {
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return read_logical_circuit_text(buffer.str());
}

} // namespace volt::io
