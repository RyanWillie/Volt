#include <volt/adapters/kicad/pcb_writer.hpp>

int main() {
    volatile auto writer = &volt::adapters::kicad::write_board;
    return writer == nullptr ? 1 : 0;
}
