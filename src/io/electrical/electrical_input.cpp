#include <volt/io/electrical/electrical_input_io.hpp>

#include <optional>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

namespace volt {

ElectricalInput ElectricalInput::Codec::prepare(const Circuit &circuit,
                                                const PartDefinitionResolver &resolver) {
    auto selections = nlohmann::ordered_json::array();
    auto parts = std::vector<std::optional<PartDefinition>>{};
    parts.reserve(circuit.all<ComponentId>().size());
    for (std::size_t index = 0; index < circuit.all<ComponentId>().size(); ++index) {
        const auto &selected = circuit.get(ComponentId{index}).selected_library_part_ref();
        if (!selected) {
            parts.emplace_back(std::nullopt);
            continue;
        }
        selections.push_back({{"occurrence", io::detail::encode_local_id(ComponentId{index})},
                              {"library_namespace", selected->library_namespace()},
                              {"library_version", selected->library_version()},
                              {"part_key", selected->part_key().value()},
                              {"library_digest", selected->library_digest().value()},
                              {"part_digest", selected->part_digest().value()}});
        try {
            parts.emplace_back(resolver.resolve(*selected));
        } catch (const KernelError &error) {
            if (error.code() != ErrorCode::UnknownEntity) {
                throw;
            }
            parts.emplace_back(std::nullopt);
        }
    }
    auto identity = ElectricalInputIdentity{sha256_content_hash(io::write_logical_circuit(circuit)),
                                            sha256_content_hash(selections.dump() + "\n")};
    return ElectricalInput{circuit, std::move(identity), std::move(parts)};
}

} // namespace volt

namespace volt::io {

ElectricalInput prepare_electrical_input(const Circuit &circuit,
                                         const PartDefinitionResolver &resolver) {
    return ElectricalInput::Codec::prepare(circuit, resolver);
}

} // namespace volt::io
