#pragma once

#include <iosfwd>

#include <nlohmann/json_fwd.hpp>

#include <volt/pcb/board.hpp>

namespace volt::io::detail {

void read_features(Board &board, const nlohmann::json &board_json);

void write_features(std::ostream &out, const Board &board);

} // namespace volt::io::detail
