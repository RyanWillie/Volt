#include <volt/io/pcb_writer.hpp>

namespace volt::io::detail {

void write_board_rooms(std::ostream &out, const Board &board, bool trailing_comma) {
    out << "    \"rooms\": [\n";
    for (std::size_t index = 0; index < board.room_count(); ++index) {
        const auto id = BoardRoomId{index};
        const auto &room = board.room(id);
        out << "      {\"id\": " << json_string(encode_local_id(id))
            << ", \"name\": " << json_string(room.name()) << ", \"outline\": ";
        write_board_points(out, room.outline().vertices());
        out << ", \"layers\": ";
        write_board_layers(out, room.layers());
        if (room.priority() != 0) {
            out << ", \"priority\": " << room.priority();
        }
        if (room.copper_clearance_mm().has_value()) {
            out << ", \"copper_clearance_mm\": ";
            write_number(out, room.copper_clearance_mm().value());
        }
        if (room.track_width_mm().has_value()) {
            out << ", \"track_width_mm\": ";
            write_number(out, room.track_width_mm().value());
        }
        out << '}';
        if (index + 1U != board.room_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ]";
    if (trailing_comma) {
        out << ',';
    }
    out << '\n';
}

} // namespace volt::io::detail
