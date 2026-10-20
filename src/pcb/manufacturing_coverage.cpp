#include "manufacturing_assembly_detail.hpp"
#include "manufacturing_target_detail.hpp"
#include "validation/board_capability_validation.hpp"
#include <algorithm>
#include <cmath>
#include <ranges>
#include <tuple>

namespace volt {
namespace {
bool complete_process(const ManufacturingProcess &p) {
    return p.family.has_value() && p.material.has_value() && p.copper_layers.has_value() &&
           p.top_copper_weight_oz.has_value() && p.bottom_copper_weight_oz.has_value() &&
           p.thickness_mm.has_value() && p.finish.has_value() && p.mask_sides.has_value() &&
           p.mask_colour.has_value() && p.via_treatment.has_value() && p.drilling.has_value() &&
           p.holes.has_value() && p.slots.has_value() && p.assembly.has_value() &&
           p.dimension_x_mm.has_value() && p.dimension_y_mm.has_value();
}

bool unsupported_process(const ManufacturingProcess &p, ManufacturingGoal goal) {
    return (p.family && *p.family != ManufacturingFamily::RigidFr4) ||
           (p.material && *p.material != ManufacturingMaterial::Fr4Tg135) ||
           (p.finish && *p.finish != ManufacturingFinish::LeadFreeHasl) ||
           (p.mask_sides && *p.mask_sides != ManufacturingMaskSides::Both) ||
           (p.mask_colour && *p.mask_colour != ManufacturingMaskColour::Green) ||
           (p.via_treatment && *p.via_treatment != ManufacturingViaTreatment::Exposed) ||
           (p.drilling && *p.drilling != ManufacturingDrilling::MechanicalThrough) ||
           (p.holes && *p.holes != ManufacturingHoleTreatment::RoundOnly) ||
           (p.slots && *p.slots != ManufacturingSlotTreatment::NoSlots) ||
           (p.copper_layers && *p.copper_layers != 2U) ||
           (p.assembly && *p.assembly != (goal == ManufacturingGoal::Fabrication
                                              ? ManufacturingAssembly::NoAssembly
                                              : ManufacturingAssembly::SmtAndThroughHole));
}

void state(ManufacturingCoverageRow &row, ManufacturingState value, ManufacturingBlocker blocker,
           std::string reason, std::string remediation = {}) {
    row.state = value;
    row.blocker = blocker;
    row.reason = std::move(reason);
    row.remediation = std::move(remediation);
}

void pass(ManufacturingCoverageRow &row, std::string reason) {
    state(row, ManufacturingState::NativePass, ManufacturingBlocker::None, std::move(reason));
}

void gap(ManufacturingCoverageRow &row, std::string reason) {
    if (row.state == ManufacturingState::Failed)
        return;
    if (row.entities.empty())
        row.entities = {EntityRef::board()};
    state(row, ManufacturingState::NotEvaluated, ManufacturingBlocker::MissingInput,
          std::move(reason),
          "Supply every explicit selection, profile limit and native prerequisite");
}

void fail(ManufacturingCoverageRow &row, std::string reason,
          ManufacturingBlocker blocker = ManufacturingBlocker::ConstraintFailed) {
    state(row, ManufacturingState::Failed, blocker, std::move(reason),
          "Correct the reported artifact or create a matching explicit target and re-evaluate");
}

void observe(ManufacturingCoverageRow &row, std::vector<EntityRef> entities, double value,
             std::optional<double> minimum, std::optional<double> maximum,
             std::optional<double> selected, ManufacturingUnit unit) {
    const auto mismatch = (minimum && value + detail::board_drc_epsilon < *minimum) ||
                          (maximum && value > *maximum + detail::board_drc_epsilon) ||
                          (selected && std::abs(value - *selected) > detail::board_drc_epsilon);
    row.entities.insert(row.entities.end(), entities.begin(), entities.end());
    row.observations.push_back(
        {std::move(entities), value, minimum, maximum, selected, unit,
         mismatch ? ManufacturingState::Failed : ManufacturingState::NativePass});
    if (mismatch)
        fail(row, "Measured value violates the inclusive profile limits or exact target selection");
}

std::optional<BoardLayerId> side_layer(const Board &board, BoardLayerSide side) {
    auto result = std::optional<BoardLayerId>{};
    for (std::size_t i = 0; i < board.all<BoardLayerId>().size(); ++i) {
        const auto id = BoardLayerId{i};
        const auto &layer = board.get(id);
        if (layer.role() == BoardLayerRole::Copper && layer.side() == side) {
            if (result)
                return std::nullopt;
            result = id;
        }
    }
    return result;
}

bool assembly_only(ManufacturingCheck check) {
    return check >= ManufacturingCheck::PartPackageReview &&
           check != ManufacturingCheck::SelectedPartRatings;
}
} // namespace

ManufacturingCoverage::ManufacturingCoverage(const CompiledBoard &compiled,
                                             const ManufacturingTarget &target)
    : completion_{evaluate_board_completion(compiled)}, target_{target},
      target_digest_{target.digest()}, profile_digest_{target.profile().digest()} {
    for (std::size_t i = 0; i < coverage_.size(); ++i) {
        const auto check = static_cast<ManufacturingCheck>(i);
        auto entities = std::vector{EntityRef::board()};
        if (check >= ManufacturingCheck::PartPackageReview) {
            entities.clear();
            for (std::size_t j = 0; j < compiled.board().circuit().all<ComponentId>().size(); ++j)
                entities.push_back(EntityRef::component(ComponentId{j}));
        }
        auto owner = ManufacturingOwner::DeferredNative;
        if (check <= ManufacturingCheck::ProfileApplicability)
            owner = ManufacturingOwner::NativeTarget;
        else if (check <= ManufacturingCheck::PhysicalGeometry)
            owner = ManufacturingOwner::NativeCompletion;
        else if (check >= ManufacturingCheck::PopulatedQuantities &&
                 check <= ManufacturingCheck::CplRotation)
            owner = ManufacturingOwner::NativeAssembly;
        else if (check == ManufacturingCheck::SelectedPartRatings)
            owner = ManufacturingOwner::NativeRatings;
        else if (check == ManufacturingCheck::ExportFidelity)
            owner = ManufacturingOwner::DeferredIO;
        else if ((check >= ManufacturingCheck::MaterialRealization &&
                  check <= ManufacturingCheck::AssemblyOrientation) ||
                 check == ManufacturingCheck::AssemblyProcess)
            owner = ManufacturingOwner::DeferredReview;
        coverage_[i] = {
            check,
            ManufacturingState::NotEvaluated,
            owner,
            true,
            ManufacturingBlocker::MissingCoverage,
            "Required checker or exact evidence is not implemented",
            "Complete the required native checker or independently reviewed evidence stage",
            std::move(entities),
            {}};
        if (assembly_only(check) && target.goal() == ManufacturingGoal::Fabrication) {
            coverage_[i].applicable = false;
            state(coverage_[i], ManufacturingState::NotApplicable, ManufacturingBlocker::None,
                  "Assembly-only requirement is not applicable to the fabrication-only goal");
        }
    }
}

ManufacturingCoverage evaluate_manufacturing_coverage(const CompiledBoard &compiled,
                                                      const ManufacturingTarget &target) {
    auto result = ManufacturingCoverage{compiled, target};
    const auto row = [&](ManufacturingCheck key) -> ManufacturingCoverageRow & {
        return result.coverage_[static_cast<std::size_t>(key)];
    };
    const auto &p = target.process();
    const auto &profile = target.profile();
    const auto &capability = profile.capability();
    const auto &board = compiled.board();
    auto &identity = row(ManufacturingCheck::ArtifactIdentity);
    const auto identity_ok =
        target.source() == compiled.identity() &&
        target.content_digest() == compiled.content_digest() &&
        detail::manufacturing_capability_bytes(capability) ==
            detail::manufacturing_capability_bytes(compiled.capabilities().profile());
    if (identity_ok)
        pass(identity,
             "Exact board, artifact bytes and complete immutable capability profile match");
    else
        fail(identity,
             "Target board, artifact bytes or complete capability snapshot does not match",
             ManufacturingBlocker::IdentityMismatch);

    // Export evidence belongs to a later stage, regardless of native completion state.
    for (std::size_t i = 0; i < static_cast<std::size_t>(BoardCompletionCheck::ExportFidelity);
         ++i) {
        auto &physical = row(static_cast<ManufacturingCheck>(
            static_cast<std::size_t>(ManufacturingCheck::LogicalContinuity) + i));
        const auto &native = result.completion_.coverage()[i];
        physical.entities = native.entities;
        physical.reason = native.reason;
        if (native.state == BoardCompletionState::Pass) {
            physical.state = ManufacturingState::NativePass;
            physical.blocker = ManufacturingBlocker::None;
            physical.remediation.clear();
        } else if (native.state == BoardCompletionState::Failed) {
            physical.state = ManufacturingState::Failed;
            physical.blocker = ManufacturingBlocker::ConstraintFailed;
            physical.remediation = "Resolve the retained native physical diagnostics";
        }
    }
    auto &selection = row(ManufacturingCheck::ProcessSelections);
    const auto unsupported = unsupported_process(p, target.goal()) ||
                             unsupported_process(profile.applicability(), target.goal());
    if (unsupported) {
        result.status_ = ManufacturingStatus::UnsupportedTarget;
        fail(selection, "Well-formed requested process is outside the finite V1 family",
             ManufacturingBlocker::UnsupportedSelection);
    } else if (!complete_process(p) ||
               (target.goal() == ManufacturingGoal::FabricationAndAssembly &&
                !target.cpl_options()))
        gap(selection, "Required process selections or explicit assembly CPL options are missing");
    else
        pass(selection, "All explicit selections belong to the finite V1 process family");
    auto &applicability = row(ManufacturingCheck::ProfileApplicability);
    if (!complete_process(p) || !complete_process(profile.applicability()))
        gap(applicability, "Exact process selections and profile applicability are incomplete");
    else if (detail::manufacturing_process_bytes(p) !=
             detail::manufacturing_process_bytes(profile.applicability()))
        fail(applicability, "Pinned profile applicability differs from the selected process");
    else if (unsupported)
        gap(applicability, "Unsupported process applicability cannot establish coverage");
    else
        pass(applicability,
             "Every explicit process selection matches the pinned profile applicability");

    const auto numeric = {
        ManufacturingCheck::CopperWidth,       ManufacturingCheck::ViaDrill,
        ManufacturingCheck::ViaCopperDiameter, ManufacturingCheck::ViaRadialAnnulus,
        ManufacturingCheck::DimensionX,        ManufacturingCheck::DimensionY,
        ManufacturingCheck::CopperLayerCount,  ManufacturingCheck::BoardThickness,
        ManufacturingCheck::TopCopperWeight,   ManufacturingCheck::BottomCopperWeight,
        ManufacturingCheck::SlotTreatment};
    for (const auto check : numeric)
        row(check).owner = ManufacturingOwner::NativeTarget;
    auto &slots = row(ManufacturingCheck::SlotTreatment);
    slots.entities = {EntityRef::board()};
    for (std::size_t i = 0; i < board.all<BoardFeatureId>().size(); ++i)
        if (identity_ok && board.get(BoardFeatureId{i}).kind() == BoardFeatureKind::Slot)
            slots.entities.push_back(EntityRef::board_feature(BoardFeatureId{i}));
    if (slots.entities.size() > 1) {
        fail(slots, "Actual slot geometry is outside the NoSlots V1 process",
             ManufacturingBlocker::UnsupportedSelection);
        fail(selection, "Actual slot geometry is outside the finite V1 process",
             ManufacturingBlocker::UnsupportedSelection);
        gap(applicability,
            "Unsupported actual slot geometry cannot establish process applicability");
        result.status_ = ManufacturingStatus::UnsupportedTarget;
    }
    if (!identity_ok || selection.state != ManufacturingState::NativePass ||
        applicability.state != ManufacturingState::NativePass) {
        for (const auto check : numeric)
            gap(row(check), "Matching artifact identity and complete supported process/profile "
                            "selections are required");
        for (const auto check :
             {ManufacturingCheck::PopulatedQuantities, ManufacturingCheck::DnpQuantities,
              ManufacturingCheck::SelectedAlternates, ManufacturingCheck::CplIdentity,
              ManufacturingCheck::CplSide, ManufacturingCheck::CplRotation,
              ManufacturingCheck::SelectedPartRatings}) {
            auto &stopped = row(check);
            if (stopped.applicable)
                state(stopped, ManufacturingState::NotEvaluated, ManufacturingBlocker::MissingInput,
                      "Matching artifact identity and complete supported process/profile "
                      "selections, including explicit assembly CPL options when applicable, "
                      "are required",
                      "Supply the exact artifact target and all supported native prerequisites");
        }
        return result;
    }
    result.ratings_ = detail::compiled_selected_part_ratings(compiled);
    detail::apply_manufacturing_ratings(row(ManufacturingCheck::SelectedPartRatings),
                                        *result.ratings_);
    if (target.goal() == ManufacturingGoal::FabricationAndAssembly) {
        result.bom_ = project_bom(board.circuit(), compiled.part_definitions());
        result.cpl_ = project_cpl(compiled.view(), *target.cpl_options());
        detail::check_manufacturing_assembly(compiled, target, *result.bom_, *result.cpl_,
                                             result.coverage_);
    }
    for (const auto check : numeric) {
        row(check).entities.clear();
        pass(row(check),
             "All measured native facts satisfy the explicit target and available profile limits");
    }
    const auto top = side_layer(board, BoardLayerSide::Top);
    const auto bottom = side_layer(board, BoardLayerSide::Bottom);
    auto &layers = row(ManufacturingCheck::CopperLayerCount);
    if (!board.layer_stack())
        gap(layers, "Native layer stack is missing");
    else {
        const auto count = board.all<BoardLayerId>().size();
        auto copper = std::vector<BoardLayerId>{};
        for (std::size_t i = 0; i < count; ++i)
            if (board.get(BoardLayerId{i}).role() == BoardLayerRole::Copper)
                copper.push_back(BoardLayerId{i});
        observe(layers, {EntityRef::board()}, static_cast<double>(copper.size()), {}, {},
                static_cast<double>(*p.copper_layers), ManufacturingUnit::Count);
        if (!top || !bottom ||
            board.layer_stack()->layers() !=
                std::vector<BoardLayerId>{top.value_or(BoardLayerId{0}),
                                          bottom.value_or(BoardLayerId{0})} ||
            !board.get(*top).enabled() || !board.get(*bottom).enabled())
            fail(layers,
                 "Actual copper layers must be exactly the enabled top/bottom pair in stack order");
        if (capability.supported_copper_layer_counts().empty())
            gap(layers, "Pinned supported copper layer counts are missing");
        else if (layers.state != ManufacturingState::Failed &&
                 std::ranges::find(capability.supported_copper_layer_counts(),
                                   static_cast<int>(copper.size())) ==
                     capability.supported_copper_layer_counts().end())
            fail(layers, "Actual copper layer count is absent from the pinned supported counts");
    }
    auto &thickness = row(ManufacturingCheck::BoardThickness);
    if (!board.layer_stack())
        gap(thickness, "Native board thickness is missing");
    else {
        const auto limits = capability.board_thickness_range_mm();
        observe(thickness, {EntityRef::board()}, board.layer_stack()->board_thickness_mm(),
                limits ? std::optional{limits->minimum_mm} : std::nullopt,
                limits ? std::optional{limits->maximum_mm} : std::nullopt, p.thickness_mm,
                ManufacturingUnit::Millimetres);
        if (!limits)
            gap(thickness, "Pinned board thickness range is missing");
    }
    for (const auto &[key, id, selected] :
         {std::tuple{ManufacturingCheck::TopCopperWeight, top, p.top_copper_weight_oz},
          std::tuple{ManufacturingCheck::BottomCopperWeight, bottom, p.bottom_copper_weight_oz}}) {
        auto &weight = row(key);
        if (id)
            weight.entities = {EntityRef::board_layer(*id)};
        if (!id || !board.get(*id).copper_weight_oz()) {
            gap(weight, "Native finished copper weight or unique side layer is missing");
            continue;
        }
        const auto actual = *board.get(*id).copper_weight_oz();
        observe(weight, {EntityRef::board_layer(*id)}, actual, {}, {}, selected,
                ManufacturingUnit::OuncesPerSquareFoot);
        if (capability.available_copper_weights_oz().empty())
            gap(weight, "Pinned available finished copper weights are missing");
        else if (!std::ranges::any_of(capability.available_copper_weights_oz(), [&](double value) {
                     return std::abs(value - actual) <= detail::board_drc_epsilon;
                 }))
            fail(weight, "Native finished copper weight is not available in the pinned profile");
    }
    for (const auto key : {ManufacturingCheck::DimensionX, ManufacturingCheck::DimensionY}) {
        auto &dimension = row(key);
        if (!board.outline()) {
            gap(dimension, "Native board outline is missing");
            continue;
        }
        const auto x = key == ManufacturingCheck::DimensionX;
        const auto coordinate = [x](BoardPoint point) { return x ? point.x_mm() : point.y_mm(); };
        const auto &vertices = board.outline()->vertices();
        const auto [low, high] = std::ranges::minmax_element(vertices, {}, coordinate);
        const auto limits = x ? profile.dimension_x_mm() : profile.dimension_y_mm();
        observe(dimension, {EntityRef::board()}, coordinate(*high) - coordinate(*low),
                limits ? std::optional{limits->minimum_mm} : std::nullopt,
                limits ? std::optional{limits->maximum_mm} : std::nullopt,
                x ? p.dimension_x_mm : p.dimension_y_mm, ManufacturingUnit::Millimetres);
        if (!limits)
            gap(dimension, "Pinned physical dimension limits are missing");
    }
    auto &width = row(ManufacturingCheck::CopperWidth);
    for (std::size_t i = 0; i < board.all<BoardTrackId>().size(); ++i) {
        const auto id = BoardTrackId{i};
        const auto &track = board.get(id);
        const auto weight = board.get(track.layer()).copper_weight_oz();
        auto minimum = capability.minimum_track_width_mm();
        if (weight) {
            const auto refinement = detail::capability_refinement_for_weight(capability, *weight);
            if (refinement)
                minimum = std::max(minimum, refinement->minimum_track_width_mm);
        }
        observe(width, {EntityRef::board_track(id), EntityRef::board_layer(track.layer())},
                track.width_mm(), minimum, {}, {}, ManufacturingUnit::Millimetres);
        if (!weight && !capability.copper_weight_refinements().empty())
            gap(width, "Track layer copper weight is missing for pinned refinements");
    }
    for (const auto key : {ManufacturingCheck::ViaDrill, ManufacturingCheck::ViaCopperDiameter,
                           ManufacturingCheck::ViaRadialAnnulus}) {
        auto &via_row = row(key);
        for (std::size_t i = 0; i < board.all<BoardViaId>().size(); ++i) {
            const auto id = BoardViaId{i};
            const auto &via = board.get(id);
            const auto drill = key == ManufacturingCheck::ViaDrill;
            const auto radial = key == ManufacturingCheck::ViaRadialAnnulus;
            const auto value = drill ? via.drill_diameter_mm()
                               : radial
                                   ? (via.annular_diameter_mm() - via.drill_diameter_mm()) / 2.0
                                   : via.annular_diameter_mm();
            const auto minimum = radial
                                     ? profile.minimum_via_radial_annulus_mm()
                                     : std::optional{drill ? capability.minimum_via_drill_mm()
                                                           : capability.minimum_via_annular_mm()};
            observe(via_row, {EntityRef::board_via(id)}, value, minimum, {}, {},
                    ManufacturingUnit::Millimetres);
            if (!minimum)
                gap(via_row, "Pinned minimum via radial annulus is missing");
        }
        if (key == ManufacturingCheck::ViaRadialAnnulus && !profile.minimum_via_radial_annulus_mm())
            gap(via_row,
                "Pinned minimum via radial annulus is missing, even for an empty via inventory");
    }
    slots.entities = {EntityRef::board()};
    slots.applicable = false;
    state(slots, ManufacturingState::NotApplicable, ManufacturingBlocker::None,
          "NoSlots was selected and the native feature inventory contains no slots");
    for (auto &entry : result.coverage_) {
        std::ranges::sort(entry.entities);
        entry.entities.erase(std::unique(entry.entities.begin(), entry.entities.end()),
                             entry.entities.end());
        if (entry.entities.empty() && entry.observations.empty() &&
            entry.owner == ManufacturingOwner::NativeTarget &&
            entry.state == ManufacturingState::NativePass)
            entry.reason =
                "Complete native inventory is empty; universal numeric check has zero measurements";
    }
    return result;
}
} // namespace volt
