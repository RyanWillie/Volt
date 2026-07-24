// Verifies that Volt::Volt and the aggregate <volt/volt.hpp> header work from an install
// tree. This is a packaging test, not a behavior test: it only needs enough real API use to
// prove headers resolve and the static archives link.

#include <iostream>
#include <string>

#include <volt/volt.hpp>

int main() {
    auto circuit = volt::Circuit{};

    const auto resistor = volt::authoring::define_component(circuit, volt::authoring::resistor());
    const auto r1 = volt::authoring::instantiate(circuit, resistor, "R");
    const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"N1"}, .kind = volt::NetKind::Signal});

    const auto pin = volt::queries::pin_by_number(circuit, r1, "1");
    if (!pin.has_value()) {
        std::cerr << "expected resistor pin 1\n";
        return 1;
    }
    if (!circuit.connect(net, pin.value())) {
        std::cerr << "expected pin 1 to connect to N1\n";
        return 1;
    }

    const auto report = volt::validate_circuit(circuit);
    const auto serialized = volt::io::write_logical_circuit(circuit);

    if (serialized.empty()) {
        std::cerr << "expected non-empty serialized circuit\n";
        return 1;
    }

    std::cout << "umbrella consumer: volt " << volt::version_string() << ", "
              << circuit.all<volt::ComponentId>().size() << " components, "
              << circuit.all<volt::NetId>().size() << " nets, " << report.count()
              << " diagnostics, " << serialized.size() << " serialized bytes\n";
    return 0;
}
