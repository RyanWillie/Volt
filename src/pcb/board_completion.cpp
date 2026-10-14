#include "completion_detail.hpp"
#include <algorithm>
#include <map>
#include <ranges>
#include <tuple>
#include <volt/pcb/compiled/board_completion.hpp>
#include <volt/pcb/compiled/board_consumers.hpp>

namespace volt {
namespace detail {
namespace {
[[nodiscard]] bool entity_less(EntityRef lhs, EntityRef rhs) { return lhs < rhs; }
} // namespace

CompletionWork::CompletionWork(const CompiledBoard &source) : compiled{source} {
    for (std::size_t i = 0; i < coverage.size(); ++i) {
        coverage[i] = {static_cast<BoardCompletionCheck>(i),
                       BoardCompletionState::Pass,
                       {},
                       "Evaluated by volt.board-completion/1"};
    }
}

void CompletionWork::note(BoardCompletionCheck check, BoardCompletionState state,
                          std::string_view code, std::string message,
                          std::vector<EntityRef> entities,
                          std::optional<DiagnosticMeasurement> measurement) {
    auto &row = coverage[static_cast<std::size_t>(check)];
    if (state == BoardCompletionState::Failed || row.state == BoardCompletionState::Pass)
        row.state = state;
    row.reason = "Inspect exact diagnostics for this mandatory check";
    row.entities.insert(row.entities.end(), entities.begin(), entities.end());
    diagnostics.add(
        Diagnostic{state == BoardCompletionState::Failed ? Severity::Error : Severity::Warning,
                   DiagnosticCode{std::string{code}},
                   DiagnosticCategory{diagnostic_categories::PcbFabrication},
                   std::move(message),
                   std::move(entities),
                   {},
                   measurement,
                   "volt.board-completion/1"});
}

std::vector<EntityRef> completion_entities(const BoardCompletionCopper &copper) {
    auto result = copper.entities;
    if (copper.net)
        result.push_back(EntityRef::net(*copper.net));
    for (const auto layer : copper.layers)
        result.push_back(EntityRef::board_layer(layer));
    return result;
}

void validate_completion_groups(CompletionWork &work) {
    auto island_of = std::vector<std::size_t>(work.copper.size());
    for (std::size_t i = 0; i < work.islands.size(); ++i)
        for (const auto item : work.islands[i].copper)
            island_of[item] = i;
    const auto view = work.compiled.view();
    if (!view.has_complete_logical_continuity()) {
        work.note(
            BoardCompletionCheck::LogicalContinuity, BoardCompletionState::NotEvaluated,
            pcb_completion_diagnostic_codes::ContinuityUnavailable,
            "CompiledBoard V1 lacks preserved logical continuity; recompile complete source to V2");
        for (const auto check :
             {BoardCompletionCheck::Connectivity, BoardCompletionCheck::UnintendedContacts}) {
            if (work.coverage[static_cast<std::size_t>(check)].state !=
                BoardCompletionState::Failed)
                work.coverage[static_cast<std::size_t>(check)].state =
                    BoardCompletionState::NotEvaluated;
            work.coverage[static_cast<std::size_t>(check)].reason =
                "V1 lacks authoritative logical groups; recompile to V2";
        }
        return;
    }
    const auto partition = view.logical_continuity().partition();
    auto representative =
        std::vector<NetId>(work.compiled.board().circuit().all<NetId>().size(), NetId{0});
    for (const auto &group : partition)
        for (const auto net : group)
            representative[net.index()] = group.front();
    for (const auto &group : partition) {
        auto &scope = work.coverage[0].entities;
        for (const auto net : group)
            scope.push_back(EntityRef::net(net));
    }
    for (auto &island : work.islands) {
        for (const auto item : island.copper)
            if (work.copper[item].net)
                island.logical_groups.push_back(representative[work.copper[item].net->index()]);
        std::ranges::sort(island.logical_groups, {}, &NetId::index);
        island.logical_groups.erase(
            std::unique(island.logical_groups.begin(), island.logical_groups.end()),
            island.logical_groups.end());
        if (island.logical_groups.size() > 1) {
            auto entities = std::vector<EntityRef>{};
            for (const auto item : island.copper) {
                const auto scope = completion_entities(work.copper[item]);
                entities.insert(entities.end(), scope.begin(), scope.end());
            }
            work.note(BoardCompletionCheck::UnintendedContacts, BoardCompletionState::Failed,
                      pcb_completion_diagnostic_codes::CopperShort,
                      "Physical copper island joins distinct preserved logical groups; remove the "
                      "copper bridge",
                      std::move(entities));
        }
    }
    auto groups = std::map<std::size_t, std::vector<std::size_t>>{};
    for (const auto &pin : work.pins)
        if (pin.physical_required && pin.net) {
            auto &items = groups[representative[pin.net->index()].index()];
            items.insert(items.end(), pin.copper.begin(), pin.copper.end());
        }
    const auto unknown =
        work.coverage[static_cast<std::size_t>(BoardCompletionCheck::Geometry)].state !=
        BoardCompletionState::Pass;
    for (const auto &[net_index, items] : groups) {
        auto entities = std::vector{EntityRef::net(NetId{net_index})};
        for (const auto item : items) {
            const auto scope = completion_entities(work.copper[item]);
            entities.insert(entities.end(), scope.begin(), scope.end());
        }
        auto &scope =
            work.coverage[static_cast<std::size_t>(BoardCompletionCheck::Connectivity)].entities;
        scope.insert(scope.end(), entities.begin(), entities.end());
        if (!items.empty() && std::ranges::any_of(items, [&](auto item) {
                return island_of[item] != island_of[items.front()];
            })) {
            work.note(BoardCompletionCheck::Connectivity,
                      unknown ? BoardCompletionState::NotEvaluated : BoardCompletionState::Failed,
                      pcb_completion_diagnostic_codes::DisconnectedGroup,
                      unknown ? "Required group connectivity is unproven because geometry is not "
                                "evaluated; resolve unsupported geometry"
                              : "Required mapped contacts occupy disconnected copper islands; "
                                "connect every terminal",
                      std::move(entities));
        }
    }
    if (unknown)
        for (const auto check :
             {BoardCompletionCheck::Connectivity, BoardCompletionCheck::UnintendedContacts}) {
            auto &row = work.coverage[static_cast<std::size_t>(check)];
            if (row.state == BoardCompletionState::Pass) {
                row.state = BoardCompletionState::NotEvaluated;
                row.reason = "Unsupported or uncertain geometry prevents complete contact coverage";
            }
        }
}
} // namespace detail

BoardCompletion::BoardCompletion(const CompiledBoard &compiled)
    : source_{compiled.identity()}, content_digest_{compiled.content_digest()},
      provenance_{compiled.provenance()}, logical_groups_{compiled.logical_continuity_partition()} {
}

void BoardCompletion::order_diagnostics() {
    const auto check = [](const Diagnostic &diagnostic) {
        namespace codes = pcb_completion_diagnostic_codes;
        const auto &code = diagnostic.code().value();
        if (code == codes::ContinuityUnavailable)
            return BoardCompletionCheck::LogicalContinuity;
        if (code == codes::MissingImplementation || code == codes::MissingPlacement ||
            code == codes::RequiredLogicalConnection || code == codes::NoConnectContradiction)
            return BoardCompletionCheck::Inventory;
        if (code == codes::MissingMapping || code == codes::MissingContact)
            return BoardCompletionCheck::RequiredContacts;
        if (code == codes::DisconnectedGroup)
            return BoardCompletionCheck::Connectivity;
        if (code == codes::CopperShort || code == codes::IsolatedContact)
            return BoardCompletionCheck::UnintendedContacts;
        if (code == codes::ExportNotEvaluated || code.starts_with("PCB_NATIVE_FAB_"))
            return BoardCompletionCheck::ExportFidelity;
        return BoardCompletionCheck::Geometry;
    };
    auto ordered = diagnostics_.diagnostics();
    std::ranges::stable_sort(ordered, [&](const auto &lhs, const auto &rhs) {
        return std::tuple{check(lhs), lhs.entities(), lhs.code().value()} <
               std::tuple{check(rhs), rhs.entities(), rhs.code().value()};
    });
    diagnostics_ = DiagnosticReport{};
    for (auto &diagnostic : ordered)
        diagnostics_.add(std::move(diagnostic));
}

BoardCompletionStatus BoardCompletion::status() const noexcept {
    if (std::ranges::any_of(
            coverage_, [](const auto &row) { return row.state == BoardCompletionState::Failed; }))
        return BoardCompletionStatus::Incomplete;
    if (std::ranges::any_of(coverage_, [](const auto &row) {
            return row.state == BoardCompletionState::NotEvaluated;
        }))
        return BoardCompletionStatus::NotEvaluated;
    return BoardCompletionStatus::Complete;
}

BoardCompletion evaluate_board_completion(const CompiledBoard &compiled) {
    auto work = detail::CompletionWork{compiled};
    detail::inventory_completion(work);
    detail::inventory_completion_copper(work);
    detail::bind_completion_contacts(work);
    detail::connect_completion_copper(work);
    detail::validate_completion_groups(work);
    const auto validation = validate_board(compiled);
    for (const auto &diagnostic : validation.diagnostics().diagnostics()) {
        work.diagnostics.add(diagnostic);
        const auto unplaced_dnp =
            (diagnostic.code().value() == "PCB_COMPONENT_NOT_PLACED" ||
             diagnostic.code().value() == "PCB_COMPONENT_MISSING_SELECTED_PART" ||
             diagnostic.code().value() == "PCB_FOOTPRINT_UNRESOLVED") &&
            std::ranges::any_of(work.components, [&](const auto &component) {
                return component.dnp && !component.placement &&
                       diagnostic.entities() ==
                           std::vector{EntityRef::component(component.component)};
            });
        if (diagnostic.severity() == Severity::Error && !unplaced_dnp) {
            auto &row = work.coverage[static_cast<std::size_t>(BoardCompletionCheck::Geometry)];
            row.state = BoardCompletionState::Failed;
            row.reason =
                "Existing native Board validation has errors; correct the reported constraints";
            row.entities.insert(row.entities.end(), diagnostic.entities().begin(),
                                diagnostic.entities().end());
        }
    }
    work.note(BoardCompletionCheck::ExportFidelity, BoardCompletionState::NotEvaluated,
              pcb_completion_diagnostic_codes::ExportNotEvaluated,
              "Native export bytes/losses have not been evaluated; use IO fabrication-completion "
              "composition");
    for (auto &row : work.coverage) {
        std::ranges::sort(row.entities, detail::entity_less);
        row.entities.erase(std::unique(row.entities.begin(), row.entities.end()),
                           row.entities.end());
    }
    auto result = BoardCompletion{compiled};
    result.components_ = std::move(work.components);
    result.pins_ = std::move(work.pins);
    result.copper_ = std::move(work.copper);
    result.islands_ = std::move(work.islands);
    result.coverage_ = std::move(work.coverage);
    result.diagnostics_ = std::move(work.diagnostics);
    result.order_diagnostics();
    return result;
}
} // namespace volt
