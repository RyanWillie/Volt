// Verifies that each installed layer target can be linked by name from an install tree,
// including Volt::KiCadAdapter, which is not reachable through the Volt::Volt umbrella.
//
// This is a packaging test, not a behavior test. Each layer only needs one real symbol so
// that a target missing from the export set, or an archive missing from the install, fails
// the build rather than passing silently.

#include <iostream>
#include <string>

#include <volt/adapters/kicad/schematic_writer.hpp>
#include <volt/authoring/component_library.hpp>
#include <volt/authoring/reference_designators.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/version.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/library/part_library.hpp>
#include <volt/pcb/board.hpp>
#include <volt/schematic/schematic.hpp>

int main() {
    // Volt::Core
    const auto version = volt::version_string();

    // Volt::Circuit + Volt::Authoring
    auto circuit = volt::Circuit{};
    const auto resistor = volt::authoring::define_component(circuit, volt::authoring::resistor());
    const auto r1 = volt::authoring::instantiate(circuit, resistor, "R");
    const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"N1"}, .kind = volt::NetKind::Signal});
    const auto pin = volt::queries::pin_by_number(circuit, r1, "1");
    if (!pin.has_value() || !circuit.connect(net, pin.value())) {
        std::cerr << "expected resistor pin 1 to connect to N1\n";
        return 1;
    }

    // Volt::Library
    const auto library_digest =
        volt::PartLibraryBuilder{
            volt::PartLibraryIdentity{"packaging", "0.1.0", volt::PartLibrarySchemaVersion::V1}}
            .reference_digest();

    // Volt::PCB
    const auto board = volt::Board{circuit};

    // Volt::Schematic
    const auto schematic = volt::Schematic{circuit};

    // Volt::IO
    const auto serialized = volt::io::write_logical_circuit(circuit);

    // Volt::KiCadAdapter
    const auto exported = volt::adapters::kicad::write_flat_schematic(schematic);

    if (version.empty() || serialized.empty() || exported.text.empty() ||
        library_digest.value().empty()) {
        std::cerr << "expected non-empty version, digest, serialization, and KiCad export\n";
        return 1;
    }

    std::cout << "layers consumer: volt " << version << ", board '" << board.name().value() << "', "
              << schematic.all<volt::SheetId>().size() << " sheets, library digest "
              << library_digest.value() << ", " << serialized.size() << " serialized bytes, "
              << exported.text.size() << " KiCad bytes\n";
    return 0;
}
