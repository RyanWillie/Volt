#include "completion_detail.hpp"
#include <algorithm>
#include <ranges>
#include <volt/circuit/connectivity/queries.hpp>

namespace volt::detail {
namespace {
namespace codes = pcb_completion_diagnostic_codes;

[[nodiscard]] std::optional<ComponentPlacementId> placement_for(const Board &board,
                                                                ComponentId component) {
    for (std::size_t i = 0; i < board.all<ComponentPlacementId>().size(); ++i) {
        if (board.get(ComponentPlacementId{i}).component() == component)
            return ComponentPlacementId{i};
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<EntityRef> pin_entities(PinId pin, ComponentId component,
                                                  std::optional<NetId> net) {
    auto entities = std::vector{EntityRef::component(component), EntityRef::pin(pin)};
    if (net)
        entities.push_back(EntityRef::net(*net));
    return entities;
}
} // namespace

void inventory_completion(CompletionWork &work) {
    const auto &compiled = work.compiled;
    const auto &board = compiled.board();
    const auto &circuit = board.circuit();
    for (std::size_t i = 0; i < circuit.all<ComponentId>().size(); ++i) {
        const auto component = ComponentId{i};
        const auto dnp = queries::component_dnp(circuit, component).value_or(false);
        const auto placement = placement_for(board, component);
        const auto *part = compiled.part(component);
        const auto footprint =
            part == nullptr
                ? std::optional<FootprintResolution>{}
                : std::optional{resolve_footprint(part->physical_part(), compiled.footprints())};
        const auto resolved = footprint && footprint->definition() != nullptr;
        const auto selection = queries::selected_library_part_ref(circuit, component);
        work.components.push_back({component, dnp, selection, placement, resolved});
        if (!dnp || placement) {
            if (!selection || part == nullptr || !resolved) {
                work.note(BoardCompletionCheck::Inventory, BoardCompletionState::Failed,
                          codes::MissingImplementation,
                          "Component requires an exact selected part and resolved footprint; "
                          "select/recompile its implementation",
                          {EntityRef::component(component)});
            }
        }
        if (!dnp && !placement) {
            work.note(BoardCompletionCheck::Inventory, BoardCompletionState::Failed,
                      codes::MissingPlacement,
                      "Populated component is unplaced; place it on this Board",
                      {EntityRef::component(component)});
        }
    }
    for (std::size_t i = 0; i < circuit.all<PinId>().size(); ++i) {
        const auto pin_id = PinId{i};
        const auto &pin = circuit.get(pin_id);
        const auto &definition = circuit.get(pin.definition());
        const auto &component = work.components.at(pin.component().index());
        const auto net = queries::net_of(circuit, pin_id);
        const auto nc = is_no_connect_pin(definition) ||
                        queries::is_intentional_no_connect_pin(circuit, pin_id);
        const auto required =
            net.has_value() && (!component.dnp || component.placement.has_value()) && !nc;
        auto row = BoardCompletionPin{
            pin_id, pin.component(), definition.connection_requirement(), nc, net, required, {},
            {}};
        if (nc && net) {
            work.note(BoardCompletionCheck::Inventory, BoardCompletionState::Failed,
                      codes::NoConnectContradiction,
                      "No-connect pin has a logical connection; correct source intent",
                      pin_entities(pin_id, pin.component(), net));
        } else if (!nc && !net && !component.dnp &&
                   definition.connection_requirement() == ConnectionRequirement::Required) {
            work.note(BoardCompletionCheck::Inventory, BoardCompletionState::Failed,
                      codes::RequiredLogicalConnection,
                      "Required populated pin lacks a logical connection; connect or declare valid "
                      "no-connect intent",
                      pin_entities(pin_id, pin.component(), net));
        }
        const auto *part = compiled.part(pin.component());
        if (part != nullptr) {
            const auto footprint = resolve_footprint(part->physical_part(), compiled.footprints());
            if (const auto *fp = footprint.definition(); fp != nullptr) {
                for (const auto &mapping : part->physical_part().pin_pad_mappings()) {
                    if (mapping.pin() == pin.definition()) {
                        if (const auto pad = fp->pad_id(mapping.pad()); pad)
                            row.mapped_pads.push_back(*pad);
                    }
                }
                std::ranges::sort(row.mapped_pads, {}, &FootprintPadId::index);
            }
        }
        if (required && row.mapped_pads.empty()) {
            work.note(BoardCompletionCheck::RequiredContacts, BoardCompletionState::Failed,
                      codes::MissingMapping,
                      "Connected required terminal has no exact pin-pad mapping; select a complete "
                      "implementation",
                      pin_entities(pin_id, pin.component(), net));
        }
        work.pins.push_back(std::move(row));
    }
    auto &scope = work.coverage[static_cast<std::size_t>(BoardCompletionCheck::Inventory)].entities;
    for (const auto &component : work.components)
        scope.push_back(EntityRef::component(component.component));
    for (const auto &pin : work.pins)
        scope.push_back(EntityRef::pin(pin.pin));
}

void bind_completion_contacts(CompletionWork &work) {
    for (auto &row : work.pins) {
        const auto &component = work.components.at(row.component.index());
        for (const auto pad : row.mapped_pads) {
            const auto before = row.copper.size();
            for (std::size_t i = 0; i < work.copper.size(); ++i) {
                const auto &item = work.copper[i];
                if (item.placement == component.placement && item.pad == pad && item.pin == row.pin)
                    row.copper.push_back(i);
            }
            if (row.copper.size() == before && row.physical_required) {
                auto entities = pin_entities(row.pin, row.component, row.net);
                if (component.placement)
                    entities.push_back(EntityRef::component_placement(*component.placement));
                entities.push_back(EntityRef::footprint_pad(pad));
                work.note(BoardCompletionCheck::RequiredContacts, BoardCompletionState::Failed,
                          codes::MissingContact,
                          "Required mapped terminal lacks placed emitted copper; restore its "
                          "placement, mapping and copper layer",
                          std::move(entities));
            }
        }
    }
    auto &contacts =
        work.coverage[static_cast<std::size_t>(BoardCompletionCheck::RequiredContacts)].entities;
    for (const auto &pin : work.pins)
        if (pin.physical_required) {
            contacts.push_back(EntityRef::pin(pin.pin));
            for (const auto item : pin.copper) {
                const auto entities = completion_entities(work.copper[item]);
                contacts.insert(contacts.end(), entities.begin(), entities.end());
            }
        }
}
} // namespace volt::detail
