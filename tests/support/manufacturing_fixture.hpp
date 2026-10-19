#pragma once
#include "board_completion_fixture.hpp"
#include <volt/io/pcb/manufacturing_target.hpp>

namespace volt::test {
/** All sources and numeric process limits here are deliberately synthetic. */
inline ManufacturingProcess manufacturing_process() {
    return {.family = ManufacturingFamily::RigidFr4,
            .material = ManufacturingMaterial::Fr4Tg135,
            .copper_layers = 2U,
            .top_copper_weight_oz = 1.0,
            .bottom_copper_weight_oz = 1.0,
            .thickness_mm = 1.6,
            .finish = ManufacturingFinish::LeadFreeHasl,
            .mask_sides = ManufacturingMaskSides::Both,
            .mask_colour = ManufacturingMaskColour::Green,
            .via_treatment = ManufacturingViaTreatment::Exposed,
            .drilling = ManufacturingDrilling::MechanicalThrough,
            .holes = ManufacturingHoleTreatment::RoundOnly,
            .slots = ManufacturingSlotTreatment::NoSlots,
            .assembly = ManufacturingAssembly::NoAssembly,
            .dimension_x_mm = 30.0,
            .dimension_y_mm = 20.0};
}

inline BoardCapabilityProfile manufacturing_capability(bool strict = false) {
    return {strict ? "Synthetic B" : "Synthetic A",
            {strict ? "synthetic://test/profile-b" : "synthetic://test/profile-a", "2026-10-19"},
            strict ? 0.25 : 0.15,
            0.2,
            0.6,
            {},
            {},
            {2},
            BoardCapabilityRange{1.0, 2.0},
            {1.0, 2.0},
            BoardCapabilityRange{0.2, 2.0}};
}

inline ManufacturingProfile
manufacturing_profile(bool strict = false,
                      ManufacturingProcess applicability = manufacturing_process()) {
    return {manufacturing_capability(strict),
            sha256_content_hash(strict ? "Synthetic profile B v1; not vendor acceptance"
                                       : "Synthetic profile A v1; not vendor acceptance"),
            applicability,
            BoardCapabilityRange{10, 40},
            BoardCapabilityRange{10, 30},
            0.15};
}

inline CompletionFixtureOptions manufacturing_options(bool strict = false) {
    auto options = CompletionFixtureOptions{};
    options.capability = manufacturing_capability(strict);
    options.top_weight = 1.0;
    options.bottom_weight = 1.0;
    options.tracks = true;
    options.via = true;
    return options;
}

inline ManufacturingTarget manufacturing_target(const CompiledBoard &compiled,
                                                bool strict = false) {
    return {compiled.identity(), compiled.content_digest(), ManufacturingGoal::Fabrication,
            manufacturing_process(), manufacturing_profile(strict)};
}
} // namespace volt::test
