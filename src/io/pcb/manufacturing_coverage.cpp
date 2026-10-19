#include "../detail/diagnostic_json.hpp"
#include "../detail/entity_ref_format.hpp"
#include <nlohmann/json.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/assembly/cpl_writer.hpp>
#include <volt/io/bom/bom_writer.hpp>
#include <volt/io/parts/electrical_records_io.hpp>
#include <volt/io/pcb/board_completion.hpp>
#include <volt/io/pcb/manufacturing_target.hpp>

namespace volt::io {
namespace {
using Json = nlohmann::json;

Json entities(std::span<const EntityRef> refs) {
    auto result = Json::array();
    for (const auto ref : refs) {
        const auto id = detail::entity_ref_serialized_id(ref);
        result.push_back({{"kind", id.substr(0, id.find(':'))}, {"index", ref.index()}});
    }
    return result;
}

Json completion_report(const BoardCompletion &completion) {
    auto report = Json::parse(write_board_completion(completion));
    if (report.value("format", "") != "volt.board-completion" ||
        report.value("schema_version", 0) != 1 || !report.contains("source") ||
        !report.contains("provenance"))
        throw KernelLogicError{
            ErrorCode::InvalidState,
            "Manufacturing coverage requires volt.board-completion/1 source and provenance"};
    return report;
}

Json number(std::optional<double> value) { return value ? Json(*value) : Json{}; }

std::string_view state_name(ManufacturingState state) {
    constexpr auto names = std::array{"native-pass", "failed", "not-evaluated", "not-applicable"};
    return names.at(static_cast<std::size_t>(state));
}

Json rating_report(const SelectedPartRatings &ratings) {
    auto subjects = Json::array();
    auto counts = std::array<std::size_t, 4>{};
    constexpr auto states = std::array{"pass", "failed", "not-evaluated", "not-applicable"};
    for (const auto &subject : ratings.subjects) {
        auto selection = Json{};
        if (subject.selection) {
            const auto &ref = *subject.selection;
            selection = {{"library_namespace", ref.library_namespace()},
                         {"library_version", ref.library_version()},
                         {"part_key", ref.part_key().value()},
                         {"library_digest", ref.library_digest().value()},
                         {"part_digest", ref.part_digest().value()}};
        }
        auto observations = Json::array();
        for (const auto &item : subject.observations)
            observations.push_back(
                {{"entities", entities(item.entities)},
                 {"entity_count", item.entities.size()},
                 {"value", item.value},
                 {"minimum", number(item.minimum)},
                 {"maximum", number(item.maximum)},
                 {"unit", item.observable == ElectricalObservable::Voltage ? "V" : "A"},
                 {"rule", item.rule},
                 {"state", states.at(static_cast<std::size_t>(item.state))}});
        ++counts.at(static_cast<std::size_t>(subject.state));
        subjects.push_back(
            {{"component", subject.component.index()},
             {"selection", selection},
             {"semantic_key", subject.key ? Json(subject.key->hash().value()) : Json{}},
             {"records",
              subject.records ? Json::parse(write_electrical_records(*subject.records)) : Json{}},
             {"rule", subject.rule},
             {"state", states.at(static_cast<std::size_t>(subject.state))},
             {"reason", subject.reason},
             {"entities", entities(subject.entities)},
             {"entity_count", subject.entities.size()},
             {"observations", observations},
             {"observation_count", observations.size()}});
    }
    auto diagnostics = Json::array();
    for (const auto &diagnostic : ratings.diagnostics.diagnostics())
        diagnostics.push_back(detail::diagnostic_json(diagnostic));
    return {{"subjects", subjects},
            {"subject_count", subjects.size()},
            {"counts",
             {{"pass", counts[0]},
              {"failed", counts[1]},
              {"not_evaluated", counts[2]},
              {"not_applicable", counts[3]}}},
            {"diagnostics", diagnostics}};
}
} // namespace

std::string write_manufacturing_coverage(const ManufacturingCoverage &result) {
    auto rows = Json::array();
    auto counts = std::array<std::size_t, 4>{};
    for (const auto &row : result.coverage()) {
        constexpr auto owners =
            std::array{"native-target", "native-completion", "deferred-native", "deferred-review",
                       "deferred-io",   "native-assembly",   "native-ratings"};
        constexpr auto blockers = std::array{"none",
                                             "MANUFACTURING_MISSING_INPUT",
                                             "MANUFACTURING_NOT_EVALUATED",
                                             "MANUFACTURING_CONSTRAINT_FAILED",
                                             "MANUFACTURING_UNSUPPORTED_SELECTION",
                                             "MANUFACTURING_IDENTITY_MISMATCH"};
        constexpr auto units = std::array{"mm", "oz/ft2", "count", "deg"};
        auto observations = Json::array();
        for (const auto &item : row.observations)
            observations.push_back({{"entities", entities(item.entities)},
                                    {"entity_count", item.entities.size()},
                                    {"value", item.value},
                                    {"minimum", number(item.minimum)},
                                    {"maximum", number(item.maximum)},
                                    {"selected", number(item.selected)},
                                    {"unit", units.at(static_cast<std::size_t>(item.unit))},
                                    {"state", state_name(item.state)}});
        ++counts.at(static_cast<std::size_t>(row.state));
        rows.push_back({{"check", manufacturing_check_name(row.check)},
                        {"state", state_name(row.state)},
                        {"owner", owners.at(static_cast<std::size_t>(row.owner))},
                        {"required", true},
                        {"applicable", row.applicable},
                        {"code", blockers.at(static_cast<std::size_t>(row.blocker))},
                        {"reason", row.reason},
                        {"remediation", row.remediation},
                        {"entities", entities(row.entities)},
                        {"entity_count", row.entities.size()},
                        {"observations", observations},
                        {"observation_count", observations.size()},
                        {"rule", "volt.manufacturing-coverage/2/" +
                                     std::string{manufacturing_check_name(row.check)}}});
    }
    const auto completion = completion_report(result.completion());
    return Json{{"format", "volt.manufacturing-coverage"},
                {"schema_version", 2},
                {"policy_version", 1},
                {"evaluator", "volt.manufacturing-coverage/2"},
                {"source", completion.at("source")},
                {"provenance", completion.at("provenance")},
                {"target_digest", result.target_digest().value()},
                {"profile_digest", result.profile_digest().value()},
                {"target", Json::parse(write_manufacturing_target(result.target()))},
                {"status", result.status() == ManufacturingStatus::UnsupportedTarget
                               ? "unsupported-target"
                               : "blocked"},
                {"ready", false},
                {"coverage", rows},
                {"completion", completion},
                {"assembly",
                 {{"bom", result.bom() ? Json::parse(write_bom_json(*result.bom())) : Json{}},
                  {"cpl", result.cpl() ? Json::parse(write_cpl_json(*result.cpl())) : Json{}}}},
                {"ratings", result.ratings() ? rating_report(*result.ratings()) : Json{}},
                {"counts",
                 {{"required", rows.size()},
                  {"native_pass", counts[0]},
                  {"failed", counts[1]},
                  {"not_evaluated", counts[2]},
                  {"not_applicable", counts[3]}}}}
               .dump(2) +
           "\n";
}
} // namespace volt::io
