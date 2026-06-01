#include "led_circuit.hpp"

#include <iostream>

#include <volt/circuit/validation.hpp>

int main() {
    const auto circuit = volt::examples::build_led_circuit();
    const auto report = volt::validate_circuit(circuit);

    std::cout << "LED circuit: " << circuit.view().component_count() << " components, "
              << circuit.view().pin_count() << " pins, " << circuit.view().net_count() << " nets, "
              << report.count() << " diagnostics\n";

    for (const auto &diagnostic : report.diagnostics()) {
        std::cerr << diagnostic.code().value() << ": " << diagnostic.message() << '\n';
    }

    return report.has_errors() ? 1 : 0;
}
