#include "../detail/entity_ref_format.hpp"
#include "manufacturing_evidence_detail.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <volt/core/errors.hpp>

namespace volt::io {
namespace {
using Json = nlohmann::json;

[[noreturn]] void malformed(std::string message) {
    throw ManufacturingEvidenceReadError{ManufacturingEvidenceReadErrorCode::Malformed,
                                         std::move(message)};
}

void require(bool condition, std::string message) {
    if (!condition)
        malformed(std::move(message));
}

void keys(const Json &v, std::initializer_list<std::string_view> expected) {
    require(v.is_object() && v.size() == expected.size(), "Unexpected evidence object fields");
    for (const auto key : expected)
        require(v.contains(key), "Missing evidence field: " + std::string{key});
}

std::string string(const Json &v) {
    require(v.is_string(), "Expected evidence string");
    return v.get<std::string>();
}

std::size_t integer(const Json &v) {
    require(v.is_number_integer() && v >= 0 && v <= std::numeric_limits<std::size_t>::max(),
            "Expected bounded unsigned evidence integer");
    return v.get<std::size_t>();
}

void version(bool valid) {
    if (!valid)
        throw ManufacturingEvidenceReadError{
            ManufacturingEvidenceReadErrorCode::UnsupportedVersion,
            "Unsupported evidence schema, policy, evaluator or exporter version"};
}

bool meaningful(std::string_view v) {
    return v.find_first_not_of(" \t\n\r\f\v") != std::string_view::npos;
}

Json exporter_json(const PcbFabricationExporterMetadata &e) {
    return {{"name", e.name},
            {"schema_version", e.schema_version},
            {"gerber",
             {{"format", e.gerber.format},
              {"units", e.gerber.units},
              {"coordinate_format", e.gerber.coordinate_format},
              {"zero_suppression", e.gerber.zero_suppression}}},
            {"drill",
             {{"format", e.drill.format},
              {"units", e.drill.units},
              {"coordinate_format", e.drill.coordinate_format},
              {"pth_npth", e.drill.pth_npth}}}};
}

Json entity_json(EntityRef e) {
    const auto id = detail::entity_ref_serialized_id(e);
    return {{"kind", id.substr(0, id.find(':'))}, {"index", e.index()}};
}

EntityRef entity(const Json &v) {
    keys(v, {"kind", "index"});
    const auto kind = string(v.at("kind"));
    const auto id = integer(v.at("index"));
    // Reporting-only decoding; these values are compared with native scope, never traversed.
    if (kind == "board") {
        require(id == 0, "Invalid board reporting identity");
        return EntityRef::board();
    }
    if (kind == "component")
        return EntityRef::component(ComponentId{id});
    if (kind == "pin")
        return EntityRef::pin(PinId{id});
    if (kind == "board_layer")
        return EntityRef::board_layer(BoardLayerId{id});
    if (kind == "board_via")
        return EntityRef::board_via(BoardViaId{id});
    if (kind == "board_text")
        return EntityRef::board_text(BoardTextId{id});
    if (kind == "component_placement")
        return EntityRef::component_placement(ComponentPlacementId{id});
    if (kind == "footprint_pad")
        return EntityRef::footprint_pad(FootprintPadId{id});
    malformed("Unsupported review scope entity kind");
}

ManufacturingCheck check(const Json &v) {
    const auto name = string(v);
    for (std::size_t i = 0; i < static_cast<std::size_t>(ManufacturingCheck::Count); ++i)
        if (manufacturing_check_name(static_cast<ManufacturingCheck>(i)) == name)
            return static_cast<ManufacturingCheck>(i);
    malformed("Unknown manufacturing review leaf");
}

ManufacturingExportBinding binding(const Json &v) {
    keys(v, {"source", "selected_closure_digest", "profile_digest", "target_digest",
             "policy_version", "evaluator", "schema_version", "exporter_revision", "exporter",
             "basename", "files", "digest"});
    require(v.at("schema_version").is_number_integer() &&
                v.at("policy_version").is_number_integer(),
            "Expected integer evidence versions");
    version(v.at("schema_version") == 1 && v.at("policy_version") == 1 &&
            string(v.at("evaluator")) == "volt.manufacturing-coverage/3" &&
            string(v.at("exporter_revision")) == "volt.native_fabrication/1");
    const auto &s = v.at("source");
    keys(s, {"board", "provenance_digest", "content_digest"});
    const auto &e = v.at("exporter");
    keys(e, {"name", "schema_version", "gerber", "drill"});
    require(e.at("schema_version").is_number_integer(),
            "Expected exporter metadata integer version");
    version(e.at("schema_version") == 1 && string(e.at("name")) == "volt.native_fabrication");
    const auto &g = e.at("gerber");
    const auto &d = e.at("drill");
    keys(g, {"format", "units", "coordinate_format", "zero_suppression"});
    keys(d, {"format", "units", "coordinate_format", "pth_npth"});
    auto exporter = PcbFabricationExporterMetadata{};
    exporter.gerber = {string(g.at("format")), string(g.at("units")),
                       string(g.at("coordinate_format")), string(g.at("zero_suppression"))};
    exporter.drill = {string(d.at("format")), string(d.at("units")),
                      string(d.at("coordinate_format")), string(d.at("pth_npth"))};
    const auto defaults = PcbFabricationExporterMetadata{};
    version(exporter_json(exporter) == exporter_json(defaults));
    auto files = std::vector<BoardCompletionFile>{};
    require(v.at("files").is_array(), "Expected evidence file inventory");
    for (const auto &f : v.at("files")) {
        keys(f, {"filename", "function", "digest"});
        files.push_back({string(f.at("filename")), string(f.at("function")),
                         ContentHash{string(f.at("digest"))}});
    }
    std::ranges::sort(files, {}, &BoardCompletionFile::function);
    auto result = ManufacturingExportBinding{
        CompiledBoardIdentity{BoardName{string(s.at("board"))},
                              ContentHash{string(s.at("provenance_digest"))}},
        ContentHash{string(s.at("content_digest"))},
        ContentHash{string(v.at("selected_closure_digest"))},
        ContentHash{string(v.at("profile_digest"))},
        ContentHash{string(v.at("target_digest"))},
        exporter,
        v.at("basename").is_null() ? std::nullopt : std::optional{string(v.at("basename"))},
        files};
    require(result.digest() == ContentHash{string(v.at("digest"))},
            "Evidence binding semantic digest differs");
    return result;
}

ManufacturingReviewAssertion assertion(const Json &v) {
    keys(v, {"binding", "check", "method", "scope", "scope_count", "entity_count", "outcome",
             "reviewer", "source", "source_key", "criterion", "minimum_mm", "observation",
             "rationale", "evidence_key", "evidence_digest"});
    const auto c = check(v.at("check"));
    const auto m = string(v.at("method"));
    require(m == "emitted-fabrication-inspection" || m == "independent-assembly-inspection",
            "Unknown review method");
    auto scope = std::vector<std::vector<EntityRef>>{};
    auto entities = std::set<EntityRef>{};
    require(v.at("scope").is_array(), "Expected compound review scope");
    for (const auto &tuple : v.at("scope")) {
        require(tuple.is_array(), "Expected review scope tuple");
        auto refs = std::vector<EntityRef>{};
        for (const auto &ref : tuple) {
            refs.push_back(entity(ref));
            entities.insert(refs.back());
        }
        scope.push_back(std::move(refs));
    }
    require(integer(v.at("scope_count")) == scope.size() &&
                integer(v.at("entity_count")) == entities.size(),
            "Review scope counts differ from exact tuples");
    const auto o = string(v.at("outcome"));
    require(o == "pass" || o == "fail", "Unknown human review outcome");
    auto minimum = std::optional<double>{};
    if (!v.at("minimum_mm").is_null()) {
        require(v.at("minimum_mm").is_number(), "Expected numeric criterion in mm");
        minimum = v.at("minimum_mm").get<double>();
    }
    return {binding(v.at("binding")),
            {c,
             m == "emitted-fabrication-inspection"
                 ? ManufacturingReviewMethod::EmittedFabricationInspection
                 : ManufacturingReviewMethod::IndependentAssemblyInspection,
             std::move(scope), manufacturing_review_requires_minimum_mm(c)},
            o == "pass" ? ManufacturingReviewOutcome::Pass : ManufacturingReviewOutcome::Fail,
            string(v.at("reviewer")),
            string(v.at("source")),
            string(v.at("source_key")),
            string(v.at("criterion")),
            minimum,
            string(v.at("observation")),
            string(v.at("rationale")),
            string(v.at("evidence_key")),
            ContentHash{string(v.at("evidence_digest"))}};
}
} // namespace

namespace detail {
void validate_manufacturing_assertions(std::span<const ManufacturingReviewAssertion> assertions) {
    auto leaves = std::set<ManufacturingCheck>{};
    for (const auto &a : assertions) {
        const auto invalid = [&] {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Invalid or duplicate manufacturing review assertion"};
        };
        if (a.requirement.check < ManufacturingCheck::ArtifactIdentity ||
            a.requirement.check >= ManufacturingCheck::Count ||
            !leaves.insert(a.requirement.check).second)
            invalid();
        if (a.requirement.method != ManufacturingReviewMethod::EmittedFabricationInspection &&
            a.requirement.method != ManufacturingReviewMethod::IndependentAssemblyInspection)
            invalid();
        (void)manufacturing_review_state(a.outcome);
        for (const auto &s : {a.reviewer, a.source, a.source_key, a.criterion, a.observation,
                              a.rationale, a.evidence_key})
            if (!meaningful(s))
                invalid();
        if (a.requirement.requires_minimum_mm !=
                manufacturing_review_requires_minimum_mm(a.requirement.check) ||
            manufacturing_review_requires_minimum_mm(a.requirement.check) !=
                a.minimum_mm.has_value() ||
            (a.minimum_mm && (!std::isfinite(*a.minimum_mm) || *a.minimum_mm < 0)))
            invalid();
        if (exporter_json(a.binding.exporter) != exporter_json(PcbFabricationExporterMetadata{}))
            invalid();
        auto names = std::set<std::string>{};
        auto functions = std::set<std::string>{};
        for (const auto &f : a.binding.files)
            if (!meaningful(f.filename) || !meaningful(f.function) ||
                !names.insert(f.filename).second || !functions.insert(f.function).second)
                invalid();
    }
}

Json manufacturing_binding_json(const ManufacturingExportBinding &b) {
    auto files = Json::array();
    auto ordered = b.files;
    std::ranges::sort(ordered, {}, &BoardCompletionFile::function);
    for (const auto &f : ordered)
        files.push_back(
            {{"filename", f.filename}, {"function", f.function}, {"digest", f.digest.value()}});
    return {{"source",
             {{"board", b.source.board().value()},
              {"provenance_digest", b.source.provenance_digest().value()},
              {"content_digest", b.content_digest.value()}}},
            {"selected_closure_digest", b.selected_closure_digest.value()},
            {"profile_digest", b.profile_digest.value()},
            {"target_digest", b.target_digest.value()},
            {"policy_version", 1},
            {"evaluator", "volt.manufacturing-coverage/3"},
            {"schema_version", 1},
            {"exporter_revision", "volt.native_fabrication/1"},
            {"exporter", exporter_json(b.exporter)},
            {"basename", b.basename ? Json(*b.basename) : Json{}},
            {"files", files},
            {"digest", b.digest().value()}};
}

Json manufacturing_assertion_json(const ManufacturingReviewAssertion &a) {
    auto scope = Json::array();
    auto entities = std::set<EntityRef>{};
    for (const auto &tuple : a.requirement.scope) {
        auto refs = Json::array();
        for (const auto ref : tuple) {
            refs.push_back(entity_json(ref));
            entities.insert(ref);
        }
        scope.push_back(refs);
    }
    return {
        {"binding", manufacturing_binding_json(a.binding)},
        {"check", manufacturing_check_name(a.requirement.check)},
        {"method", a.requirement.method == ManufacturingReviewMethod::EmittedFabricationInspection
                       ? "emitted-fabrication-inspection"
                       : "independent-assembly-inspection"},
        {"scope", scope},
        {"scope_count", scope.size()},
        {"entity_count", entities.size()},
        {"outcome", a.outcome == ManufacturingReviewOutcome::Pass ? "pass" : "fail"},
        {"reviewer", a.reviewer},
        {"source", a.source},
        {"source_key", a.source_key},
        {"criterion", a.criterion},
        {"minimum_mm", a.minimum_mm ? Json(*a.minimum_mm) : Json{}},
        {"observation", a.observation},
        {"rationale", a.rationale},
        {"evidence_key", a.evidence_key},
        {"evidence_digest", a.evidence_digest.value()}};
}
} // namespace detail

std::string write_manufacturing_evidence(std::span<const ManufacturingReviewAssertion> assertions) {
    detail::validate_manufacturing_assertions(assertions);
    auto ordered = std::vector<ManufacturingReviewAssertion>{assertions.begin(), assertions.end()};
    std::ranges::sort(ordered, {}, [](const auto &a) { return a.requirement.check; });
    auto records = Json::array();
    for (const auto &a : ordered)
        records.push_back(detail::manufacturing_assertion_json(a));
    return Json{{"format", "volt.manufacturing-evidence"},
                {"schema_version", 1},
                {"assertions", records}}
               .dump(2) +
           "\n";
}

std::vector<ManufacturingReviewAssertion> read_manufacturing_evidence(std::string_view text) {
    try {
        auto stack = std::vector<std::set<std::string>>{};
        const auto callback = [&](int, Json::parse_event_t event, Json &parsed) {
            if (event == Json::parse_event_t::object_start)
                stack.emplace_back();
            else if (event == Json::parse_event_t::key)
                require(stack.back().insert(parsed.get<std::string>()).second,
                        "Duplicate evidence JSON key");
            else if (event == Json::parse_event_t::object_end)
                stack.pop_back();
            return true;
        };
        const auto doc = Json::parse(text.begin(), text.end(), callback);
        keys(doc, {"format", "schema_version", "assertions"});
        require(string(doc.at("format")) == "volt.manufacturing-evidence",
                "Unknown evidence format");
        require(doc.at("schema_version").is_number_integer(), "Expected integer evidence version");
        version(doc.at("schema_version") == 1);
        require(doc.at("assertions").is_array(), "Expected evidence assertion array");
        auto result = std::vector<ManufacturingReviewAssertion>{};
        for (const auto &v : doc.at("assertions"))
            result.push_back(assertion(v));
        detail::validate_manufacturing_assertions(result);
        std::ranges::sort(result, {}, [](const auto &a) { return a.requirement.check; });
        return result;
    } catch (const ManufacturingEvidenceReadError &) {
        throw;
    } catch (const KernelError &e) {
        malformed(e.what());
    } catch (const Json::exception &e) {
        malformed(e.what());
    }
}

std::string write_manufacturing_evaluation(const ManufacturingEvaluation &result) {
    auto report = Json::parse(write_manufacturing_coverage(result.coverage()));
    report["export_binding"] = detail::manufacturing_binding_json(result.binding());
    auto files = Json::array();
    for (const auto &f : result.output().files)
        files.push_back({{"filename", f.filename},
                         {"function", f.function},
                         {"text", f.text},
                         {"digest", sha256_content_hash(f.text).value()}});
    auto losses = Json::array();
    for (const auto &w : result.output().loss_report.warnings()) {
        auto refs = Json::array();
        for (const auto ref : w.entities)
            refs.push_back(entity_json(ref));
        constexpr auto kinds = std::array{"missing-geometry", "unsupported-geometry",
                                          "unsupported-layer", "lossy-geometry"};
        constexpr auto severities = std::array{"info", "warning", "error"};
        losses.push_back(
            {{"kind", kinds.at(static_cast<std::size_t>(w.kind))},
             {"construct", w.construct},
             {"message", w.message},
             {"severity", severities.at(static_cast<std::size_t>(w.severity))},
             {"fabrication_impact", w.fabrication_impact == PcbFabricationLossImpact::FabCritical
                                        ? "fab-critical"
                                        : "informational"},
             {"entities", refs}});
    }
    report["native_export"] = {{"files", files}, {"losses", losses}};
    auto reviews = Json::array();
    constexpr auto dispositions = std::array{
        "accepted",      "binding-mismatch", "scope-mismatch",          "ineligible",
        "missing-bytes", "changed-bytes",    "file-inventory-mismatch", "native-prerequisite"};
    for (const auto &r : result.reviews())
        reviews.push_back(
            {{"assertion", detail::manufacturing_assertion_json(r.assertion)},
             {"disposition", dispositions.at(static_cast<std::size_t>(r.disposition))},
             {"reason", r.reason}});
    report["reviews"] = reviews;
    report["review_count"] = reviews.size();
    return report.dump(2) + "\n";
}
} // namespace volt::io
