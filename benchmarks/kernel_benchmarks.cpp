#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/authoring/component_library.hpp>
#include <volt/authoring/connection_helpers.hpp>
#include <volt/authoring/reference_designators.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkResult {
    std::string name;
    std::size_t size;
    std::chrono::nanoseconds duration;
};

class BenchmarkSink {
  public:
    void add(std::string name, std::size_t size, std::chrono::nanoseconds duration) {
        results_.push_back(BenchmarkResult{std::move(name), size, duration});
    }

    void print(std::ostream &out) const {
        out << "benchmark,size,ms\n";
        for (const auto &result : results_) {
            const auto milliseconds =
                std::chrono::duration<double, std::milli>{result.duration}.count();
            out << result.name << ',' << result.size << ',' << milliseconds << '\n';
        }
    }

  private:
    std::vector<BenchmarkResult> results_;
};

template <typename Func>
decltype(auto) measure(BenchmarkSink &sink, std::string name, std::size_t size, Func &&func) {
    const auto start = Clock::now();
    decltype(auto) result = std::forward<Func>(func)();
    const auto end = Clock::now();
    sink.add(std::move(name), size,
             std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
    return result;
}

volt::Circuit build_resistor_chain(std::size_t component_count) {
    auto circuit = volt::Circuit{};
    const auto resistor = volt::authoring::define_component(circuit, volt::authoring::resistor());

    auto components = std::vector<volt::ComponentId>{};
    components.reserve(component_count);
    for (std::size_t index = 0; index < component_count; ++index) {
        components.push_back(volt::authoring::instantiate(
            circuit, resistor, volt::ReferenceDesignator{"R" + std::to_string(index + 1)}));
    }

    for (std::size_t index = 0; index <= component_count; ++index) {
        [[maybe_unused]] const auto net = circuit.add_net(
            volt::Net{volt::NetName{"N" + std::to_string(index)}, volt::NetKind::Signal});
    }

    for (std::size_t index = 0; index < component_count; ++index) {
        const auto component = components[index];
        const auto first_net = volt::NetId{index};
        const auto second_net = volt::NetId{index + 1};
        const auto first_pin = volt::queries::pin_by_number(circuit, component, "1").value();
        const auto second_pin = volt::queries::pin_by_number(circuit, component, "2").value();
        volt::authoring::connect(circuit, first_net, {first_pin});
        volt::authoring::connect(circuit, second_net, {second_pin});
    }

    return circuit;
}

void require_valid_round_trip(const volt::Circuit &circuit, std::string_view json) {
    const auto restored = volt::io::read_logical_circuit_text(json);
    if (restored.component_count() != circuit.component_count() ||
        restored.pin_count() != circuit.pin_count() ||
        restored.net_count() != circuit.net_count()) {
        throw std::runtime_error{"Logical circuit round-trip changed entity counts"};
    }
}

void run_size(BenchmarkSink &sink, std::size_t component_count) {
    auto circuit = measure(sink, "build_resistor_chain", component_count,
                           [component_count] { return build_resistor_chain(component_count); });

    const auto report = measure(sink, "validate_circuit", component_count,
                                [&circuit] { return volt::validate_circuit(circuit); });
    if (report.has_errors()) {
        throw std::runtime_error{"Generated benchmark circuit has validation errors"};
    }

    auto json = measure(sink, "write_logical_circuit", component_count,
                        [&circuit] { return volt::io::write_logical_circuit(circuit); });

    measure(sink, "read_logical_circuit", component_count, [&circuit, &json] {
        require_valid_round_trip(circuit, json);
        return json.size();
    });
}

} // namespace

int main() {
    auto sink = BenchmarkSink{};
    for (const auto component_count : {100U, 1000U, 5000U}) {
        run_size(sink, component_count);
    }

    sink.print(std::cout);
}
