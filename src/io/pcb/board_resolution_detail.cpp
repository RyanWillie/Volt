#include "board_resolution_detail.hpp"

#include <map>
#include <ranges>
#include <utility>
#include <vector>

#include <volt/core/errors.hpp>

namespace volt::io::detail {
namespace {

[[nodiscard]] bool same_summary_role(const PartFootprintPad &summary,
                                     const FootprintPad &definition) {
    if (!summary.role().has_value()) {
        return !definition.mechanical_role().has_value();
    }
    switch (*summary.role()) {
    case PartFootprintPadRole::Mechanical:
        return definition.mechanical_role().has_value() &&
               *definition.mechanical_role() != FootprintPadMechanicalRole::Thermal;
    case PartFootprintPadRole::Thermal:
        return definition.mechanical_role() == FootprintPadMechanicalRole::Thermal;
    case PartFootprintPadRole::NonElectrical:
        return definition.mechanical_role().has_value() &&
               *definition.mechanical_role() != FootprintPadMechanicalRole::Thermal;
    }
    return false;
}

void require_footprint_summary(const PartDefinition &part, const FootprintDefinition &definition) {
    const auto &expected = part.orderable_part();
    if (definition.ref() != expected.footprint().footprint()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Part footprint asset identity does not match its exact reference"};
    }
    if (definition.pad_count() != expected.footprint_pads().size()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Part footprint asset pad set does not match its exact summary"};
    }
    for (const auto &summary : expected.footprint_pads()) {
        const auto pad_id = definition.pad_id(summary.label());
        if (!pad_id.has_value()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Part footprint asset is missing an exact footprint pad key"};
        }
        const auto &pad = definition.pad(*pad_id);
        if (pad.position().x_mm() != summary.x_mm() || pad.position().y_mm() != summary.y_mm() ||
            pad.size().width_mm() != summary.width_mm() ||
            pad.size().height_mm() != summary.height_mm() || !same_summary_role(summary, pad)) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Part footprint asset geometry differs from its exact summary"};
        }
    }
}

[[nodiscard]] std::vector<PinPadMapping> physical_mappings(const ComponentDefinition &component,
                                                           const PartDefinition &part,
                                                           const FootprintDefinition &footprint) {
    auto terminal_pads = std::map<PackageTerminalKey, std::vector<FootprintPadKey>>{};
    for (const auto &mapping : part.orderable_part().terminal_pad_mappings()) {
        terminal_pads.emplace(mapping.terminal(), mapping.pads());
    }

    auto result = std::vector<PinPadMapping>{};
    for (const auto &pin_mapping : part.pin_terminal_mappings()) {
        const auto key = std::ranges::find(component.contract().pin_keys(), pin_mapping.pin());
        if (key == component.contract().pin_keys().end()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Exact part maps a PinKey outside the selected component"};
        }
        const auto pin_index =
            static_cast<std::size_t>(key - component.contract().pin_keys().begin());
        const auto pin = component.pins().at(pin_index);
        for (const auto &terminal : pin_mapping.terminals()) {
            const auto pads = terminal_pads.find(terminal);
            if (pads == terminal_pads.end()) {
                throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                       "Exact part package terminal has no footprint-pad mapping"};
            }
            for (const auto &pad : pads->second) {
                if (!footprint.pad_id(pad.value()).has_value()) {
                    throw KernelLogicError{
                        ErrorCode::CrossReferenceViolation,
                        "Exact part footprint-pad mapping is absent from decoded asset"};
                }
                result.emplace_back(pin, pad.value());
            }
        }
    }
    return result;
}

[[nodiscard]] std::optional<PartModel3D> physical_model(const PartDefinition &part) {
    const auto &reference = part.orderable_part().model_3d();
    if (!reference.has_value()) {
        return std::nullopt;
    }
    return PartModel3D{reference->format(), reference->file_name(), reference->translation_mm(),
                       reference->rotation_deg()};
}

[[nodiscard]] ElectricalAttributeMap physical_electrical_attributes(const PartDefinition &part) {
    auto result = ElectricalAttributeMap{};
    if (part.electrical_records().pin_count() != 2U) {
        return result;
    }

    auto voltage_rating = std::optional<double>{};
    for (const auto &record : part.electrical_records().records()) {
        if (record.observable() != ElectricalObservable::Voltage ||
            record.meaning() != ElectricalMeaning::AbsoluteLimit || !record.conditions().empty() ||
            record.value().kind() != ElectricalValueKind::Range) {
            continue;
        }
        const auto &maximum = record.value().as_range().maximum();
        if (maximum.has_value() &&
            (!voltage_rating.has_value() || maximum->value() < *voltage_rating)) {
            voltage_rating = maximum->value();
        }
    }
    if (voltage_rating.has_value()) {
        result.set(ElectricalAttributeSpec{ElectricalAttributeName{"voltage_rating"},
                                           ElectricalAttributeOwner::SelectedPart,
                                           ElectricalAttributeKind::Constraint,
                                           UnitDimension::Voltage},
                   ElectricalAttributeValue{Quantity{UnitDimension::Voltage, *voltage_rating}});
    }
    return result;
}

} // namespace

PartAssetReference footprint_asset_reference(const PartDefinition &part) {
    const auto &footprint = part.orderable_part().footprint();
    return PartAssetReference{PartAssetKind::Footprint,
                              "footprint:" + footprint.footprint().library() + "/" +
                                  footprint.footprint().name(),
                              footprint.hash()};
}

PartAssetReference model_asset_reference(const PartModel3DReference &model) {
    return PartAssetReference{PartAssetKind::Model3D,
                              "model:" + model.format() + "/" + model.file_name(), model.hash()};
}

void add_exact_footprint(FootprintLibrary &library, const FootprintDefinition &definition) {
    const auto *existing = library.find(definition.ref());
    if (existing == nullptr) {
        library.add(definition);
        return;
    }
    if (*existing != definition) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Selected closure contains conflicting footprint definitions"};
    }
}

ResolvedBoardPart materialize_resolved_part(const Circuit &circuit, ComponentId component,
                                            const PartDefinition &part,
                                            const FootprintDefinition &footprint,
                                            std::optional<std::string> model_bytes,
                                            const ContentHash &implemented_component) {
    const auto &instance = circuit.get(component);
    if (!instance.selected_library_part_ref().has_value()) {
        throw KernelLogicError{ErrorCode::InvalidState,
                               "Resolved Board part has no exact selected reference",
                               EntityRef::component(component)};
    }
    if (part.implemented_component() != implemented_component) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Selected exact part implements another component contract",
                               EntityRef::component(component)};
    }

    require_footprint_summary(part, footprint);
    const auto &model = part.orderable_part().model_3d();
    if (model_bytes.has_value()) {
        if (!model.has_value() ||
            sha256_content_hash(*model_bytes) != model_asset_reference(*model).digest()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Selected exact 3D model digest does not match bytes",
                                   EntityRef::component(component)};
        }
    }

    auto physical_part = PhysicalPart{
        part.orderable_part().manufacturer_part(),
        part.orderable_part().package(),
        part.orderable_part().footprint().footprint(),
        physical_mappings(circuit.get(instance.definition()), part, footprint),
        {},
        physical_model(part),
        part.orderable_part().approved_alternate_mpns(),
        physical_electrical_attributes(part),
    };
    return ResolvedBoardPart{component, *instance.selected_library_part_ref(),
                             std::move(physical_part), std::move(model_bytes)};
}

} // namespace volt::io::detail
