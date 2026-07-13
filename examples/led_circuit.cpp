#include "led_circuit.hpp"

#include <iostream>

#include <volt/circuit/validation/validation.hpp>

int main() {
    const auto circuit = volt::examples::build_led_circuit();
    const auto report = volt::validate_circuit(circuit);

    std::cout << "LED circuit: " << circuit.all<volt::ComponentId>().size() << " components, "
              << circuit.all<volt::PinId>().size() << " pins, " << circuit.all<volt::NetId>().size()
              << " nets, " << report.count() << " diagnostics\n";

    for (const auto &diagnostic : report.diagnostics()) {
        std::cerr << diagnostic.code().value() << ": " << diagnostic.message() << '\n';
    }

    return report.has_errors() ? 1 : 0;
}
