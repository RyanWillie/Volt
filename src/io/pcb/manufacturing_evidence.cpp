#include "manufacturing_evidence_detail.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <volt/core/errors.hpp>

namespace volt::io {
namespace {
void token(std::string &out, std::string_view value) {
    out += std::to_string(value.size()) + ":";
    out += value;
}

std::vector<BoardCompletionFile> inventory(std::span<const PcbFabricationFile> files) {
    auto result = std::vector<BoardCompletionFile>{};
    for (const auto &file : files)
        result.push_back({file.filename, file.function, sha256_content_hash(file.text)});
    std::ranges::sort(result, {}, &BoardCompletionFile::function);
    return result;
}

bool complete_inventory(std::span<const PcbFabricationFile> files) {
    auto functions = std::set<std::string>{};
    auto names = std::set<std::string>{};
    for (const auto &file : files)
        if (file.text.empty() || !functions.insert(file.function).second ||
            !names.insert(file.filename).second)
            return false;
    for (const auto required :
         {"copper-top", "copper-bottom", "soldermask-top", "soldermask-bottom", "silkscreen-top",
          "paste-top", "profile", "drill-plated", "drill-non-plated"})
        if (!functions.contains(required))
            return false;
    return true;
}

bool same_files(std::vector<PcbFabricationFile> supplied,
                const std::vector<PcbFabricationFile> &actual) {
    const auto sort = [](auto &files) {
        std::ranges::sort(files, {}, &PcbFabricationFile::function);
    };
    auto expected = actual;
    sort(supplied);
    sort(expected);
    if (supplied.size() != expected.size())
        return false;
    for (std::size_t i = 0; i < supplied.size(); ++i)
        if (supplied[i].function != expected[i].function ||
            supplied[i].filename != expected[i].filename || supplied[i].text != expected[i].text)
            return false;
    return true;
}

std::vector<EntityRef> flattened(const ManufacturingReviewRequirement &requirement) {
    auto result = std::vector<EntityRef>{};
    for (const auto &tuple : requirement.scope)
        result.insert(result.end(), tuple.begin(), tuple.end());
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
} // namespace

ContentHash ManufacturingExportBinding::digest() const {
    auto bytes = std::string{};
    for (const auto &value : {std::string{"volt.manufacturing-export-binding/1"},
                              source.board().value(),
                              source.provenance_digest().value(),
                              content_digest.value(),
                              selected_closure_digest.value(),
                              profile_digest.value(),
                              target_digest.value(),
                              std::string{"1"},
                              std::string{"volt.manufacturing-coverage/3"},
                              std::string{"1"},
                              std::string{"volt.native_fabrication/1"},
                              exporter.name,
                              std::to_string(exporter.schema_version),
                              exporter.gerber.format,
                              exporter.gerber.units,
                              exporter.gerber.coordinate_format,
                              exporter.gerber.zero_suppression,
                              exporter.drill.format,
                              exporter.drill.units,
                              exporter.drill.coordinate_format,
                              exporter.drill.pth_npth})
        token(bytes, value);
    token(bytes, basename ? "present" : "absent");
    if (basename)
        token(bytes, *basename);
    auto ordered = files;
    std::ranges::sort(ordered, {}, &BoardCompletionFile::function);
    token(bytes, std::to_string(ordered.size()));
    for (const auto &file : ordered) {
        token(bytes, file.function);
        token(bytes, file.filename);
        token(bytes, file.digest.value());
    }
    return sha256_content_hash(bytes);
}

ManufacturingEvaluation::ManufacturingEvaluation(PcbFabricationExportResult output,
                                                 ManufacturingExportBinding binding,
                                                 ManufacturingCoverage coverage,
                                                 std::vector<ManufacturingReviewReceipt> reviews)
    : output_{std::move(output)}, binding_{std::move(binding)}, coverage_{std::move(coverage)},
      reviews_{std::move(reviews)} {}

ManufacturingEvaluation evaluate_manufacturing_export(const CompiledBoard &compiled,
                                                      const ManufacturingTarget &target,
                                                      const ManufacturingEvidenceInput &input) {
    detail::validate_manufacturing_assertions(input.assertions);
    auto attachments = std::map<std::string, std::string_view>{};
    for (const auto &attachment : input.attachments)
        if (attachment.key.empty() || !attachments.emplace(attachment.key, attachment.bytes).second)
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Duplicate or empty evidence attachment key"};
    auto referenced = std::set<std::string>{};
    for (const auto &assertion : input.assertions) {
        referenced.insert(assertion.evidence_key);
        referenced.insert(assertion.source_key);
    }
    for (const auto &[key, bytes] : attachments) {
        (void)bytes;
        if (!referenced.contains(key))
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Unreferenced evidence attachment"};
    }
    auto native = evaluate_manufacturing_coverage(compiled, target);
    auto fabrication = evaluate_pcb_fabrication(compiled, input.options);
    auto binding = ManufacturingExportBinding{compiled.identity(),
                                              compiled.content_digest(),
                                              compiled.provenance().selected_closure_digest(),
                                              target.profile().digest(),
                                              target.digest(),
                                              fabrication.output.exporter,
                                              input.options.basename,
                                              inventory(fabrication.output.files)};
    const auto identity =
        native.coverage()[static_cast<std::size_t>(ManufacturingCheck::ArtifactIdentity)].state ==
        ManufacturingState::NativePass;
    const auto inventory_ok = complete_inventory(fabrication.output.files);
    const auto supplied_ok =
        !input.reviewed_files || same_files(*input.reviewed_files, fabrication.output.files);
    auto &export_row =
        native.coverage_[static_cast<std::size_t>(ManufacturingCheck::ExportFidelity)];
    const auto &actual = fabrication.completion.coverage().back();
    export_row.owner = ManufacturingOwner::NativeIO;
    export_row.entities = actual.entities;
    export_row.reason = actual.reason;
    const auto fidelity = identity && inventory_ok && actual.state == BoardCompletionState::Pass;
    export_row.state = fidelity ? ManufacturingState::NativePass : ManufacturingState::NotEvaluated;
    export_row.blocker =
        fidelity ? ManufacturingBlocker::None : ManufacturingBlocker::MissingCoverage;
    export_row.remediation =
        fidelity ? "" : "Resolve actual native export coverage, inventory and exact identity";
    if (!inventory_ok)
        export_row.reason =
            "Actual native export lacks a complete unique nonempty required file inventory";
    if (!identity)
        export_row.reason =
            "Foreign target identity cannot establish actual export target coverage";
    // This predicate is evaluated before applying assertions: a failed review may not change
    // whether another independent assertion was checked, nor may insertion order waive a failure.
    const auto allowed = fidelity && manufacturing_review_allowed(native, fabrication.completion);
    auto assertions = input.assertions;
    std::ranges::sort(assertions, {}, [](const auto &a) { return a.requirement.check; });
    auto receipts = std::vector<ManufacturingReviewReceipt>{};
    for (const auto &assertion : assertions) {
        const auto check = assertion.requirement.check;
        auto &row = native.coverage_[static_cast<std::size_t>(check)];
        const auto requirement = manufacturing_review_requirement(compiled, check);
        auto disposition = ManufacturingReviewDisposition::Accepted;
        auto reason = std::string{"Exact eligible human assertion and supplied bytes correspond; "
                                  "inspection honesty is not verified"};
        const auto reject = [&](ManufacturingReviewDisposition value, std::string text) {
            disposition = value;
            reason = std::move(text);
        };
        if (!requirement || !row.applicable || assertion.requirement.method != requirement->method)
            reject(ManufacturingReviewDisposition::Ineligible,
                   "Leaf, goal or method is not eligible for human evidence");
        else if (assertion.binding.digest() != binding.digest())
            reject(ManufacturingReviewDisposition::BindingMismatch,
                   "Exact artifact, closure, profile, target or complete export binding differs");
        else if (assertion.requirement.scope != requirement->scope)
            reject(ManufacturingReviewDisposition::ScopeMismatch,
                   "Complete ordered native compound scope differs");
        else if (!supplied_ok)
            reject(ManufacturingReviewDisposition::FileInventoryMismatch,
                   "Supplied reviewed files differ from the complete actual native bytes");
        else if (!attachments.contains(assertion.evidence_key) ||
                 !attachments.contains(assertion.source_key) ||
                 attachments.at(assertion.evidence_key).empty() ||
                 attachments.at(assertion.source_key).empty())
            reject(ManufacturingReviewDisposition::MissingBytes,
                   "Exact nonempty review and profile-source attachment bytes are required");
        else if (sha256_content_hash(attachments.at(assertion.evidence_key)) !=
                     assertion.evidence_digest ||
                 sha256_content_hash(attachments.at(assertion.source_key)) !=
                     target.profile().source_content())
            reject(ManufacturingReviewDisposition::ChangedBytes,
                   "Review or pinned profile-source attachment bytes differ");
        else if (!allowed)
            reject(ManufacturingReviewDisposition::NativePrerequisite,
                   "Mandatory native identity, process, physical, target or export prerequisites "
                   "do not pass");
        if (requirement && row.applicable && row.state == ManufacturingState::NotEvaluated) {
            row.entities = flattened(*requirement);
            row.owner = ManufacturingOwner::HumanReview;
            row.reason = reason;
            if (disposition == ManufacturingReviewDisposition::Accepted) {
                row.state = manufacturing_review_state(assertion.outcome);
                row.blocker = row.state == ManufacturingState::ReviewedPass
                                  ? ManufacturingBlocker::None
                                  : ManufacturingBlocker::ConstraintFailed;
                row.remediation =
                    row.state == ManufacturingState::ReviewedPass
                        ? ""
                        : "Resolve the declared failed human review and obtain new exact evidence";
            } else {
                row.blocker = ManufacturingBlocker::InvalidEvidence;
                row.remediation = "Supply eligible complete exact-bound review and source bytes "
                                  "after resolving mandatory native prerequisites";
            }
        }
        receipts.push_back({assertion, disposition, std::move(reason)});
    }
    // Eligible missing assertions still report complete native scope, including absent marks.
    for (auto &row : native.coverage_)
        if (const auto requirement = manufacturing_review_requirement(compiled, row.check);
            requirement && row.applicable)
            row.entities = flattened(*requirement);
    native.evaluator_version_ = 3;
    native.completion_ = std::move(fabrication.completion);
    return ManufacturingEvaluation{std::move(fabrication.output), std::move(binding),
                                   std::move(native), std::move(receipts)};
}
} // namespace volt::io
