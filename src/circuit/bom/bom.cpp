#include <volt/circuit/bom/bom.hpp>

#include <volt/core/errors.hpp>

#include <algorithm>
#include <tuple>
#include <utility>

namespace volt {

namespace {

void append_unique(std::vector<std::string> &values, const std::vector<std::string> &candidates) {
    for (const auto &candidate : candidates) {
        if (std::find(values.begin(), values.end(), candidate) == values.end()) {
            values.push_back(candidate);
        }
    }
}

struct LineAccumulator {
    std::string manufacturer;
    std::string mpn;
    std::string package;
    bool dnp;
    std::size_t quantity = 0;
    std::vector<std::string> references;
    std::vector<std::string> approved_alternate_mpns;
    std::vector<std::string> selection_override_references;
    PropertyMap sourcing;
};

[[nodiscard]] bool same_line_key(const LineAccumulator &line, const BomSelectedPart &part,
                                 bool dnp) {
    return line.manufacturer == part.manufacturer() && line.mpn == part.mpn() &&
           line.package == part.package() && line.dnp == dnp;
}

[[nodiscard]] bool line_less(const BomLine &lhs, const BomLine &rhs) {
    if (lhs.manufacturer() != rhs.manufacturer()) {
        return lhs.manufacturer() < rhs.manufacturer();
    }
    if (lhs.mpn() != rhs.mpn()) {
        return lhs.mpn() < rhs.mpn();
    }
    if (lhs.package() != rhs.package()) {
        return lhs.package() < rhs.package();
    }
    return !lhs.dnp() && rhs.dnp();
}

} // namespace

void BomSourcingSnapshot::set_mpn_properties(std::string mpn, PropertyMap properties) {
    if (mpn.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "BOM sourcing MPN must not be empty"};
    }
    const auto existing = std::find_if(entries_.begin(), entries_.end(),
                                       [&mpn](const auto &entry) { return entry.first == mpn; });
    if (existing == entries_.end()) {
        entries_.emplace_back(std::move(mpn), std::move(properties));
        return;
    }
    existing->second = std::move(properties);
}

[[nodiscard]] const PropertyMap *
BomSourcingSnapshot::properties_for_mpn(const std::string &mpn) const noexcept {
    const auto existing = std::find_if(entries_.begin(), entries_.end(),
                                       [&mpn](const auto &entry) { return entry.first == mpn; });
    if (existing == entries_.end()) {
        return nullptr;
    }
    return &existing->second;
}

BomSelectedPart::BomSelectedPart(std::string manufacturer, std::string mpn, std::string package,
                                 std::vector<std::string> approved_alternate_mpns)
    : manufacturer_{std::move(manufacturer)}, mpn_{std::move(mpn)}, package_{std::move(package)},
      approved_alternate_mpns_{std::move(approved_alternate_mpns)} {}

BomComponent::BomComponent(ComponentId component, std::string reference, bool dnp,
                           bool dnp_explicit, bool selection_override,
                           std::optional<BomSelectedPart> selected_part)
    : component_{component}, reference_{std::move(reference)}, dnp_{dnp},
      dnp_explicit_{dnp_explicit}, selection_override_{selection_override},
      selected_part_{std::move(selected_part)} {}

BomLine::BomLine(std::string manufacturer, std::string mpn, std::string package, bool dnp,
                 std::size_t quantity, std::vector<std::string> references,
                 std::vector<std::string> approved_alternate_mpns,
                 std::vector<std::string> selection_override_references, PropertyMap sourcing)
    : manufacturer_{std::move(manufacturer)}, mpn_{std::move(mpn)}, package_{std::move(package)},
      dnp_{dnp}, quantity_{quantity}, references_{std::move(references)},
      approved_alternate_mpns_{std::move(approved_alternate_mpns)},
      selection_override_references_{std::move(selection_override_references)},
      sourcing_{std::move(sourcing)} {}

Bom::Bom(std::vector<BomComponent> components, std::vector<BomLine> lines)
    : components_{std::move(components)}, lines_{std::move(lines)} {}

[[nodiscard]] Bom project_bom(const Circuit &circuit) {
    return project_bom(circuit, BomSourcingSnapshot{});
}

[[nodiscard]] Bom project_bom(const Circuit &circuit, const BomSourcingSnapshot &sourcing) {
    auto components = std::vector<BomComponent>{};
    components.reserve(circuit.all<volt::ComponentId>().size());

    auto accumulators = std::vector<LineAccumulator>{};
    for (std::size_t index = 0; index < circuit.all<volt::ComponentId>().size(); ++index) {
        const auto component_id = ComponentId{index};
        const auto &component = circuit.get(component_id);
        const auto dnp_intent = circuit.component_dnp(component_id);
        const auto dnp = dnp_intent.value_or(false);
        const auto selection_override = circuit.is_component_selection_override(component_id);
        auto selected = std::optional<BomSelectedPart>{};
        const auto &selected_part = circuit.selected_physical_part(component_id);
        if (selected_part.has_value()) {
            selected = BomSelectedPart{
                selected_part->manufacturer_part().manufacturer(),
                selected_part->manufacturer_part().part_number(),
                selected_part->package().value(),
                selected_part->approved_alternate_mpns(),
            };
        }

        components.emplace_back(component_id, component.reference().value(), dnp,
                                dnp_intent.has_value(), selection_override, selected);

        if (!selected.has_value()) {
            continue;
        }

        auto existing = std::find_if(accumulators.begin(), accumulators.end(),
                                     [&](const LineAccumulator &line) {
                                         return same_line_key(line, selected.value(), dnp);
                                     });
        if (existing == accumulators.end()) {
            auto sourcing_properties = PropertyMap{};
            if (const auto *properties = sourcing.properties_for_mpn(selected->mpn());
                properties != nullptr) {
                sourcing_properties = *properties;
            }
            existing = accumulators.emplace(accumulators.end(), LineAccumulator{
                                                                    selected->manufacturer(),
                                                                    selected->mpn(),
                                                                    selected->package(),
                                                                    dnp,
                                                                    0,
                                                                    {},
                                                                    {},
                                                                    {},
                                                                    std::move(sourcing_properties),
                                                                });
        }
        existing->references.push_back(component.reference().value());
        if (!dnp) {
            ++existing->quantity;
        }
        append_unique(existing->approved_alternate_mpns, selected->approved_alternate_mpns());
        if (selection_override) {
            existing->selection_override_references.push_back(component.reference().value());
        }
    }

    std::sort(components.begin(), components.end(),
              [](const BomComponent &lhs, const BomComponent &rhs) {
                  return lhs.reference() < rhs.reference();
              });

    auto lines = std::vector<BomLine>{};
    lines.reserve(accumulators.size());
    for (auto &line : accumulators) {
        std::sort(line.references.begin(), line.references.end());
        std::sort(line.approved_alternate_mpns.begin(), line.approved_alternate_mpns.end());
        std::sort(line.selection_override_references.begin(),
                  line.selection_override_references.end());
        lines.emplace_back(std::move(line.manufacturer), std::move(line.mpn),
                           std::move(line.package), line.dnp, line.quantity,
                           std::move(line.references), std::move(line.approved_alternate_mpns),
                           std::move(line.selection_override_references), std::move(line.sourcing));
    }
    std::sort(lines.begin(), lines.end(), line_less);

    return Bom{std::move(components), std::move(lines)};
}

} // namespace volt
