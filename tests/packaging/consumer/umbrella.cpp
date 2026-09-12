// Verifies that Volt::Volt and the aggregate <volt/volt.hpp> header work from an install
// tree. This is a packaging test, not a behavior test: it only needs enough real API use to
// prove headers resolve and the static archives link.

#include <iostream>
#include <stdexcept>
#include <string>

#include <volt/volt.hpp>

class NoParts final : public volt::PartDefinitionResolver {
  public:
    const volt::PartDefinition &resolve(const volt::LibraryPartRef &) const & override {
        throw std::runtime_error{"source-only package test must not resolve a part"};
    }
};

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

    // Link the private numerical implementation without resolving Eigen in the consumer.
    auto dc_circuit = volt::Circuit{};
    const auto positive = dc_circuit.add_net(volt::NetSpec{.name = volt::NetName{"positive"}});
    const auto reference = dc_circuit.add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
    const auto input = volt::io::prepare_dc_input(dc_circuit, NoParts{});
    const auto request = volt::DcRequest{
        volt::DcRequestKey{"package-test"},
        input,
        input.net(reference),
        {volt::DcVoltageSource{volt::DcSourceKey{"drive"},
                               volt::DcNetPair{input.net(positive), input.net(reference)},
                               volt::Quantity{volt::UnitDimension::Voltage, 5.0}}}};
    const auto compiled = volt::compile_electrical(request);
    if (!compiled.complete()) {
        return 1;
    }
    const auto dc_report = volt::solve_dc(*compiled.model());
    if (!dc_report.success() || dc_report.solution()->branches().size() != 1U ||
        volt::io::write_dc_solve_report(dc_report).empty()) {
        return 1;
    }

    std::cout << "umbrella consumer: volt " << volt::version_string() << ", "
              << circuit.all<volt::ComponentId>().size() << " components, "
              << circuit.all<volt::NetId>().size() << " nets, " << report.count()
              << " diagnostics, " << serialized.size() << " serialized bytes\n";
    return 0;
}
