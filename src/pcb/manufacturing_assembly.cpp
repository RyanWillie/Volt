#include "assembly/cpl_rotation_detail.hpp"
#include "manufacturing_assembly_detail.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/pcb/queries/board_queries.hpp>

namespace volt::detail {
namespace {
void fail(ManufacturingCoverageRow &row, std::string reason) {
    row.state = ManufacturingState::Failed;
    row.blocker = ManufacturingBlocker::ConstraintFailed;
    row.reason = std::move(reason);
    row.remediation = "Correct explicit assembly intent, exact selection or required placement";
}

bool same_part(const BomSelectedPart &bom, const PhysicalPart &physical) {
    return bom.manufacturer() == physical.manufacturer_part().manufacturer() &&
           bom.mpn() == physical.manufacturer_part().part_number() &&
           bom.package() == physical.package().value() &&
           bom.approved_alternate_mpns() == physical.approved_alternate_mpns();
}

bool same_line(const BomLine &line, const BomSelectedPart &part, bool dnp) {
    return line.manufacturer() == part.manufacturer() && line.mpn() == part.mpn() &&
           line.package() == part.package() && line.dnp() == dnp;
}

void observe(ManufacturingCoverageRow &row, const std::vector<EntityRef> &entities, double value,
             double selected, ManufacturingUnit unit) {
    const auto matches = std::isfinite(value) && value == selected;
    row.entities.insert(row.entities.end(), entities.begin(), entities.end());
    row.observations.push_back(
        {entities,
         value,
         {},
         {},
         selected,
         unit,
         matches ? ManufacturingState::NativePass : ManufacturingState::Failed});
    if (!matches)
        fail(row, "Native assembly projection differs from exact inventory or authored placement");
}
} // namespace

void check_manufacturing_assembly(const CompiledBoard &compiled, const ManufacturingTarget &target,
                                  const Bom &bom, const Cpl &cpl,
                                  std::span<ManufacturingCoverageRow> rows) {
    const auto row = [&](ManufacturingCheck key) -> ManufacturingCoverageRow & {
        return rows[static_cast<std::size_t>(key)];
    };
    const auto checks = {ManufacturingCheck::PopulatedQuantities,
                         ManufacturingCheck::DnpQuantities,
                         ManufacturingCheck::SelectedAlternates,
                         ManufacturingCheck::CplIdentity,
                         ManufacturingCheck::CplSide,
                         ManufacturingCheck::CplRotation};
    for (const auto key : checks) {
        auto &entry = row(key);
        entry.owner = ManufacturingOwner::NativeAssembly;
        entry.state = ManufacturingState::NativePass;
        entry.blocker = ManufacturingBlocker::None;
        entry.reason = "Complete logical inventory matches the native exact assembly projection";
        entry.remediation.clear();
    }
    auto &populated = row(ManufacturingCheck::PopulatedQuantities);
    auto &dnp_quantity = row(ManufacturingCheck::DnpQuantities);
    auto &selection = row(ManufacturingCheck::SelectedAlternates);
    auto &identity = row(ManufacturingCheck::CplIdentity);
    auto &side = row(ManufacturingCheck::CplSide);
    auto &rotation = row(ManufacturingCheck::CplRotation);
    const auto &board = compiled.board();
    const auto &circuit = board.circuit();
    // The complete owning logical inventory precedes every projection lookup.
    for (std::size_t i = 0; i < circuit.all<ComponentId>().size(); ++i) {
        const auto component = ComponentId{i};
        const auto &instance = circuit.get(component);
        const auto intent = queries::component_dnp(circuit, component);
        const auto dnp = intent.value_or(false);
        const auto occurrence =
            std::ranges::find(bom.components(), component, &BomComponent::component);
        if (!intent)
            fail(populated, "Every assembly occurrence needs explicit fitted or DNP intent");
        if (occurrence == bom.components().end() ||
            std::ranges::count(bom.components(), component, &BomComponent::component) != 1 ||
            occurrence->reference() != instance.reference().value() || occurrence->dnp() != dnp ||
            occurrence->dnp_explicit() != intent.has_value()) {
            fail(populated, "Native BOM must represent every logical occurrence exactly once");
            fail(dnp_quantity, "Native BOM occurrence inventory differs from explicit DNP intent");
            continue;
        }
        const auto *part = compiled.part(component);
        const auto &reference = queries::selected_library_part_ref(circuit, component);
        if (occurrence->selection_override() !=
            queries::is_component_selection_override(circuit, component))
            fail(selection, "Native BOM changed exact selected-part override intent");
        if (!dnp && (!reference || !part || !occurrence->selected_part())) {
            fail(populated, "Populated occurrence has no exact selected purchase identity");
            fail(selection, "Populated occurrence has no exact selected part");
        }
        if (reference) {
            const auto &full = compiled.part_definitions().resolve(*reference);
            if (!part || part->reference() != *reference || !occurrence->selected_part() ||
                !same_part(*occurrence->selected_part(), part->physical_part()) ||
                full.orderable_part().manufacturer_part() !=
                    part->physical_part().manufacturer_part() ||
                full.orderable_part().package() != part->physical_part().package() ||
                full.orderable_part().footprint().footprint() != part->physical_part().footprint())
                fail(selection, "Exact full selection and native physical/BOM identities differ");
        } else if (occurrence->selected_part())
            fail(selection, "An unselected occurrence acquired a projected purchase identity");
        if (occurrence->selected_part() &&
            std::ranges::count_if(bom.lines(), [&](const BomLine &line) {
                return same_line(line, *occurrence->selected_part(), dnp) &&
                       std::ranges::count(line.references(), occurrence->reference()) == 1;
            }) != 1)
            fail(dnp ? dnp_quantity : populated,
                 "Selected occurrence is missing or duplicated in grouped BOM");

        const auto row_count = std::ranges::count(cpl.rows(), component, &CplRow::component);
        if (dnp) {
            if (row_count != 0)
                for (const auto key : {ManufacturingCheck::CplIdentity, ManufacturingCheck::CplSide,
                                       ManufacturingCheck::CplRotation})
                    fail(row(key), "DNP occurrence must not have an emitted populated CPL row");
            continue;
        }
        const auto placement = queries::placement_for_component(board, component);
        if (!placement || row_count != 1) {
            for (const auto key : {ManufacturingCheck::CplIdentity, ManufacturingCheck::CplSide,
                                   ManufacturingCheck::CplRotation})
                fail(row(key),
                     "Every populated occurrence requires exactly one placement and CPL row");
            continue;
        }
        const auto &authored = board.get(*placement);
        const auto &projected = *std::ranges::find(cpl.rows(), component, &CplRow::component);
        const auto entities = std::vector{EntityRef::component(component),
                                          EntityRef::component_placement(*placement)};
        for (const auto key : {ManufacturingCheck::CplIdentity, ManufacturingCheck::CplSide,
                               ManufacturingCheck::CplRotation})
            row(key).entities.push_back(EntityRef::component_placement(*placement));
        if (projected.placement() != *placement ||
            projected.reference() != instance.reference().value() || !part ||
            !projected.footprint() || !projected.part_identity() ||
            *projected.footprint() != part->physical_part().footprint() ||
            projected.part_identity()->manufacturer() !=
                part->physical_part().manufacturer_part().manufacturer() ||
            projected.part_identity()->mpn() !=
                part->physical_part().manufacturer_part().part_number() ||
            projected.part_identity()->package() != part->physical_part().package().value() ||
            compiled.footprints().find(*projected.footprint()) == nullptr)
            fail(identity,
                 "CPL occurrence, exact selected part or resolved footprint identity differs");
        observe(identity, entities, projected.position().x_mm(), authored.position().x_mm(),
                ManufacturingUnit::Millimetres);
        observe(identity, entities, projected.position().y_mm(), authored.position().y_mm(),
                ManufacturingUnit::Millimetres);
        observe(side, entities, static_cast<double>(projected.side()),
                static_cast<double>(authored.side()), ManufacturingUnit::Count);
        if (!part || !projected.footprint() ||
            *projected.footprint() != part->physical_part().footprint() ||
            compiled.footprints().find(*projected.footprint()) == nullptr) {
            fail(rotation,
                 "Exact selected footprint is required to evaluate the pinned CPL correction");
            continue;
        }
        auto offset = 0.0;
        for (const auto &candidate : target.cpl_options()->rotation_offsets)
            if (candidate.footprint() == part->physical_part().footprint())
                offset = candidate.rotation_deg();
        const auto normalized = normalize_cpl_rotation(authored.rotation().degrees());
        observe(rotation, entities, projected.authored_rotation_deg(), normalized,
                ManufacturingUnit::Degrees);
        observe(rotation, entities, projected.rotation_offset_deg(), offset,
                ManufacturingUnit::Degrees);
        observe(rotation, entities, projected.rotation_deg(),
                normalize_cpl_rotation(normalized + offset), ManufacturingUnit::Degrees);
    }
    if (bom.components().size() != circuit.all<ComponentId>().size())
        fail(populated, "Native BOM occurrence count differs from the full logical inventory");
    for (const auto &line : bom.lines()) {
        auto references = std::vector<std::string>{};
        auto overrides = std::vector<std::string>{};
        auto alternates = std::set<std::string>{};
        auto entities = std::vector<EntityRef>{};
        for (const auto &occurrence : bom.components())
            if (occurrence.selected_part() &&
                same_line(line, *occurrence.selected_part(), occurrence.dnp())) {
                references.push_back(occurrence.reference());
                entities.push_back(EntityRef::component(occurrence.component()));
                if (occurrence.selection_override())
                    overrides.push_back(occurrence.reference());
                alternates.insert(occurrence.selected_part()->approved_alternate_mpns().begin(),
                                  occurrence.selected_part()->approved_alternate_mpns().end());
            }
        std::ranges::sort(references);
        std::ranges::sort(overrides);
        std::ranges::sort(entities);
        auto &quantity = line.dnp() ? dnp_quantity : populated;
        if (references.empty() || line.references() != references)
            fail(quantity,
                 "Grouped BOM references differ from the complete native occurrence inventory");
        if (line.selection_override_references() != overrides ||
            line.approved_alternate_mpns() !=
                std::vector<std::string>{alternates.begin(), alternates.end()})
            fail(selection,
                 "Grouped BOM changed selected overrides or approved-alternate metadata");
        observe(quantity, entities, static_cast<double>(line.quantity()),
                line.dnp() ? 0.0 : static_cast<double>(references.size()),
                ManufacturingUnit::Count);
    }
    for (const auto &projected : cpl.rows())
        if (projected.component().index() >= circuit.all<ComponentId>().size())
            fail(identity, "CPL contains an occurrence outside the complete logical inventory");
    for (const auto key : checks)
        if (row(key).state == ManufacturingState::NativePass && row(key).observations.empty())
            row(key).reason = "Complete native inventory checked; this consistency leaf has no "
                              "applicable measurements";
}
} // namespace volt::detail
