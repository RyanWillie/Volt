#include "../detail/diagnostic_json.hpp"
#include "../detail/entity_ref_format.hpp"
#include "pcb_fabrication_detail.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <ranges>
#include <volt/io/pcb/board_completion.hpp>
#include <volt/io/pcb/pcb_fabrication_writer.hpp>

namespace volt::io {
namespace {
using Json = nlohmann::json;

[[nodiscard]] Json entities(std::span<const EntityRef> refs) {
    auto result = Json::array();
    for (const auto ref : refs) {
        const auto id = detail::entity_ref_serialized_id(ref);
        result.push_back({{"kind", id.substr(0, id.find(':'))}, {"index", ref.index()}});
    }
    return result;
}

[[nodiscard]] std::string_view state_name(BoardCompletionState state) {
    switch (state) {
    case BoardCompletionState::Pass:
        return "pass";
    case BoardCompletionState::Failed:
        return "failed";
    case BoardCompletionState::NotEvaluated:
        return "not-evaluated";
    }
    std::unreachable();
}

[[nodiscard]] std::string_view status_name(BoardCompletionStatus status) {
    switch (status) {
    case BoardCompletionStatus::Complete:
        return "complete";
    case BoardCompletionStatus::Incomplete:
        return "incomplete";
    case BoardCompletionStatus::NotEvaluated:
        return "not-evaluated";
    }
    std::unreachable();
}

[[nodiscard]] Json part_json(const LibraryPartRef &part) {
    return {{"library_namespace", part.library_namespace()},
            {"library_version", part.library_version()},
            {"part_key", part.part_key().value()},
            {"library_bundle_digest", part.library_digest().value()},
            {"part_digest", part.part_digest().value()}};
}
} // namespace

PcbFabricationEvaluation evaluate_pcb_fabrication(const CompiledBoard &compiled,
                                                  const PcbFabricationExportOptions &options) {
    auto result = evaluate_board_completion(compiled);
    auto export_result = write_pcb_fabrication_files(compiled, options);
    const auto grid = detail::completion_export_grid(compiled, result.copper());
    auto report = DiagnosticReport{};
    for (const auto &diagnostic : result.diagnostics_.diagnostics())
        if (diagnostic.code().value() != pcb_completion_diagnostic_codes::ExportNotEvaluated)
            report.add(diagnostic);
    auto &row = result.coverage_[static_cast<std::size_t>(BoardCompletionCheck::ExportFidelity)];
    row.entities.push_back(EntityRef::board());
    for (const auto &item : result.copper()) {
        row.entities.insert(row.entities.end(), item.entities.begin(), item.entities.end());
        for (const auto layer : item.layers)
            row.entities.push_back(EntityRef::board_layer(layer));
    }
    for (std::size_t i = 0; i < compiled.board().all<BoardFeatureId>().size(); ++i)
        row.entities.push_back(EntityRef::board_feature(BoardFeatureId{i}));
    row.state = BoardCompletionState::Pass;
    row.reason = "Actual volt.native_fabrication/1 Gerber/Excellon bytes evaluated; this is not "
                 "target readiness";
    for (const auto &diagnostic : grid.diagnostics()) {
        report.add(diagnostic);
        row.state = BoardCompletionState::NotEvaluated;
        row.entities.insert(row.entities.end(), diagnostic.entities().begin(),
                            diagnostic.entities().end());
    }
    for (const auto &warning : export_result.loss_report.warnings()) {
        if (warning.fabrication_impact == PcbFabricationLossImpact::FabCritical ||
            warning.kind == PcbFabricationLossKind::LossyGeometry) {
            row.state = BoardCompletionState::NotEvaluated;
            row.entities.insert(row.entities.end(), warning.entities.begin(),
                                warning.entities.end());
        }
    }
    const auto losses = fabrication_diagnostics(export_result.loss_report);
    for (const auto &diagnostic : losses.diagnostics())
        report.add(diagnostic);
    for (const auto &file : export_result.files)
        result.files_.push_back({file.filename, file.function, sha256_content_hash(file.text)});
    if (std::ranges::any_of(grid.diagnostics(), [](const auto &diagnostic) {
            return diagnostic.code().value() == pcb_completion_diagnostic_codes::ExportNotEvaluated;
        })) {
        row.reason = "Native 4.6 coordinate range is unsupported; export was not evaluated";
        std::ranges::sort(row.entities);
        row.entities.erase(std::unique(row.entities.begin(), row.entities.end()),
                           row.entities.end());
        result.diagnostics_ = std::move(report);
        result.order_diagnostics();
        return {std::move(export_result), std::move(result)};
    }
    if (row.state != BoardCompletionState::Pass)
        row.reason = "Native emitted geometry/loss/quantization coverage is incomplete; inspect "
                     "exact diagnostics";
    if (row.state != BoardCompletionState::Pass)
        row.reason = "Native emitted geometry/loss/quantization/format coverage is incomplete; "
                     "inspect exact diagnostics";
    std::ranges::sort(row.entities);
    row.entities.erase(std::unique(row.entities.begin(), row.entities.end()), row.entities.end());
    result.diagnostics_ = std::move(report);
    result.order_diagnostics();
    return {std::move(export_result), std::move(result)};
}

BoardCompletion evaluate_fabrication_completion(const CompiledBoard &compiled) {
    return evaluate_pcb_fabrication(compiled).completion;
}

std::string write_board_completion(const BoardCompletion &completion) {
    auto components = Json::array();
    for (const auto &item : completion.components())
        components.push_back(
            {{"component", item.component.index()},
             {"dnp", item.dnp},
             {"selection", item.selection ? part_json(*item.selection) : Json{}},
             {"placement", item.placement ? Json(item.placement->index()) : Json{}},
             {"footprint_resolved", item.footprint_resolved}});
    auto pins = Json::array();
    auto required_pins = std::size_t{0};
    auto required_contacts = std::size_t{0};
    for (const auto &pin : completion.pins()) {
        auto pads = Json::array();
        for (const auto pad : pin.mapped_pads)
            pads.push_back(pad.index());
        constexpr auto requirements = std::array{"optional", "required", "must_not_connect"};
        pins.push_back({{"pin", pin.pin.index()},
                        {"component", pin.component.index()},
                        {"requirement", requirements.at(static_cast<std::size_t>(pin.requirement))},
                        {"no_connect", pin.no_connect},
                        {"net", pin.net ? Json(pin.net->index()) : Json{}},
                        {"physical_required", pin.physical_required},
                        {"mapped_pads", pads},
                        {"copper", pin.copper}});
        if (pin.physical_required) {
            ++required_pins;
            required_contacts += pin.mapped_pads.size();
        }
    }
    auto copper = Json::array();
    for (const auto &item : completion.copper()) {
        auto layers = Json::array();
        for (const auto layer : item.layers)
            layers.push_back(layer.index());
        auto points = Json::array();
        for (const auto point : item.points)
            points.push_back({point.x_mm(), point.y_mm()});
        constexpr auto kinds = std::array{"disc", "segment", "polygon"};
        copper.push_back({{"kind", kinds.at(static_cast<std::size_t>(item.kind))},
                          {"net", item.net ? Json(item.net->index()) : Json{}},
                          {"layers", layers},
                          {"entities", entities(item.entities)},
                          {"points", points},
                          {"radius_mm", item.radius_mm},
                          {"placement", item.placement ? Json(item.placement->index()) : Json{}},
                          {"pad", item.pad ? Json(item.pad->index()) : Json{}},
                          {"pin", item.pin ? Json(item.pin->index()) : Json{}},
                          {"isolated", item.isolated},
                          {"supported", item.supported},
                          {"contact_evaluated", item.contact_evaluated}});
    }
    auto islands = Json::array();
    for (const auto &island : completion.islands()) {
        auto groups = Json::array();
        for (const auto net : island.logical_groups)
            groups.push_back(net.index());
        islands.push_back({{"copper", island.copper}, {"logical_groups", groups}});
    }
    auto coverage = Json::array();
    constexpr auto checks =
        std::array{"logical-continuity",  "inventory", "required-contacts", "connectivity",
                   "unintended-contacts", "geometry",  "export-fidelity"};
    for (const auto &row : completion.coverage())
        coverage.push_back(
            {{"check", checks.at(static_cast<std::size_t>(row.check))},
             {"state", state_name(row.state)},
             {"required", true},
             {"entities", entities(row.entities)},
             {"entity_count", row.entities.size()},
             {"reason", row.reason},
             {"owner", row.check == BoardCompletionCheck::ExportFidelity ? "io" : "pcb"}});
    auto diagnostics = Json::array();
    for (const auto &diagnostic : completion.diagnostics().diagnostics())
        diagnostics.push_back(detail::diagnostic_json(diagnostic));
    auto files = Json::array();
    for (const auto &file : completion.files())
        files.push_back({{"filename", file.filename},
                         {"function", file.function},
                         {"digest", file.digest.value()}});
    const auto &p = completion.provenance();
    const auto provenance =
        Json{{"schema_version", static_cast<unsigned int>(p.schema_version())},
             {"compiler_version", static_cast<unsigned int>(p.compiler_version())},
             {"compiler_name", p.compiler_name()},
             {"compiler_build", p.compiler_build()},
             {"logical_dependency_digest", p.logical_dependency_digest().value()},
             {"physical_snapshot_digest", p.physical_snapshot_digest().value()},
             {"selected_closure_digest", p.selected_closure_digest().value()},
             {"capability_digest", p.capability_digest().value()},
             {"provenance_digest", p.provenance_digest().value()}};
    auto logical_groups = Json{};
    if (completion.logical_groups()) {
        logical_groups = Json::array();
        for (const auto &group : *completion.logical_groups()) {
            auto members = Json::array();
            for (const auto net : group)
                members.push_back(net.index());
            logical_groups.push_back(std::move(members));
        }
    }
    const auto result =
        Json{{"format", "volt.board-completion"},
             {"schema_version", 1},
             {"evaluator", "volt.board-completion/1"},
             {"source",
              {{"board", completion.source().board().value()},
               {"provenance_digest", completion.source().provenance_digest().value()},
               {"content_digest", completion.content_digest().value()}}},
             {"provenance", provenance},
             {"logical_groups", logical_groups},
             {"status", status_name(completion.status())},
             {"components", components},
             {"pins", pins},
             {"copper", copper},
             {"islands", islands},
             {"coverage", coverage},
             {"diagnostics", diagnostics},
             {"files", files},
             {"counts",
              {{"components", components.size()},
               {"pins", pins.size()},
               {"required_pins", required_pins},
               {"required_contacts", required_contacts},
               {"copper", copper.size()},
               {"islands", islands.size()}}}};
    return result.dump(2) + "\n";
}
} // namespace volt::io
