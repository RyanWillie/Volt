#pragma once
#include <volt/io/pcb/manufacturing_target.hpp>
#include <volt/io/pcb/pcb_fabrication_writer.hpp>
#include <volt/pcb/compiled/manufacturing_review.hpp>

namespace volt::io {
/** Exact domain-separated identity of actual native output and its immutable inputs. */
struct ManufacturingExportBinding {
    /** Exact named board/provenance identity. */
    CompiledBoardIdentity source;
    /** Immutable artifact bytes. */
    ContentHash content_digest;
    /** Delivered selected closure provenance. */
    ContentHash selected_closure_digest;
    /** Complete pinned profile identity. */
    ContentHash profile_digest;
    /** Complete target including CPL options. */
    ContentHash target_digest;
    /** Full current native exporter metadata. */
    PcbFabricationExporterMetadata exporter;
    /** Requested basename; null differs from explicitly supplied input. */
    std::optional<std::string> basename;
    /** Complete actual file inventory sorted by unique function. */
    std::vector<BoardCompletionFile> files;
    /** Canonical semantic digest, independent of JSON and checkout paths. */
    [[nodiscard]] ContentHash digest() const;
};

/** One finite human assertion. Native verification proves correspondence, not honesty. */
struct ManufacturingReviewAssertion {
    /** Exact snapshot/export identity declared by reviewer. */
    ManufacturingExportBinding binding;
    /** Native-derived leaf/method/compound scope and criterion shape. */
    ManufacturingReviewRequirement requirement;
    /** Declared human outcome. */
    ManufacturingReviewOutcome outcome;
    /** Reviewer identity as declared, not authenticated by this API. */
    std::string reviewer;
    /** Source citation in captured profile source bytes. */
    std::string source;
    /** Exact source attachment key. */
    std::string source_key;
    /** Human-declared complete criterion for this leaf. */
    std::string criterion;
    /** Explicit numeric criterion only for the seven minimum-mm leaves. */
    std::optional<double> minimum_mm;
    /** Human observation or external inspection source. */
    std::string observation;
    /** Rationale for declared result. */
    std::string rationale;
    /** Exact review attachment key. */
    std::string evidence_key;
    /** Exact review attachment byte identity. */
    ContentHash evidence_digest;
};

/** One explicitly supplied in-memory attachment; keys never cause filesystem reads. */
struct ManufacturingEvidenceAttachment {
    /** Opaque unique label. */
    std::string key;
    /** Exact bytes; empty bytes cannot satisfy evidence. */
    std::string bytes;
};

/** Typed composition inputs; every assertion and byte is reverified on each evaluation. */
struct ManufacturingEvidenceInput {
    /** Actual native exporter options. */
    PcbFabricationExportOptions options;
    /** Optional reviewed output; supplied inventory must equal every actual file byte. */
    std::optional<std::vector<PcbFabricationFile>> reviewed_files;
    /** At most one assertion per closed leaf, canonicalized in leaf order. */
    std::vector<ManufacturingReviewAssertion> assertions;
    /** Exact review and source attachments. */
    std::vector<ManufacturingEvidenceAttachment> attachments;
};
/** Closed binding/eligibility disposition for each supplied assertion. */
enum class ManufacturingReviewDisposition {
    Accepted,
    BindingMismatch,
    ScopeMismatch,
    Ineligible,
    MissingBytes,
    ChangedBytes,
    FileInventoryMismatch,
    NativePrerequisite
};

/** Checked receipt retaining all declared human provenance. */
struct ManufacturingReviewReceipt {
    /** Original complete assertion. */
    ManufacturingReviewAssertion assertion;
    /** Native verification disposition; Accepted can declare either Pass or Fail. */
    ManufacturingReviewDisposition disposition;
    /** Exact actionable verification explanation. */
    std::string reason;
};

/** Immutable actual output, complete native coverage and checked human assertions. */
class ManufacturingEvaluation final {
  public:
    /** Actual output, never accepted back as trusted evaluator input. */
    [[nodiscard]] const PcbFabricationExportResult &output() const noexcept { return output_; }

    /** Exact export binding derived on this invocation. */
    [[nodiscard]] const ManufacturingExportBinding &binding() const noexcept { return binding_; }

    /** All 65 rows, native assembly/rating evidence and actual IO completion. */
    [[nodiscard]] const ManufacturingCoverage &coverage() const noexcept { return coverage_; }

    /** Every supplied assertion in closed leaf order, without silent dropping. */
    [[nodiscard]] std::span<const ManufacturingReviewReceipt> reviews() const noexcept {
        return reviews_;
    }

  private:
    friend ManufacturingEvaluation
    evaluate_manufacturing_export(const CompiledBoard &, const ManufacturingTarget &,
                                  const ManufacturingEvidenceInput &);
    ManufacturingEvaluation(PcbFabricationExportResult output, ManufacturingExportBinding binding,
                            ManufacturingCoverage coverage,
                            std::vector<ManufacturingReviewReceipt> reviews);
    PcbFabricationExportResult output_;
    ManufacturingExportBinding binding_;
    ManufacturingCoverage coverage_;
    std::vector<ManufacturingReviewReceipt> reviews_;
};

/** Export once, verify supplied bytes and apply only the closed PCB review policy. */
[[nodiscard]] ManufacturingEvaluation
evaluate_manufacturing_export(const CompiledBoard &compiled, const ManufacturingTarget &target,
                              const ManufacturingEvidenceInput &input = {});
/** Deterministic V3 report with actual file bytes/losses, binding and review provenance. */
[[nodiscard]] std::string write_manufacturing_evaluation(const ManufacturingEvaluation &result);
/** Canonical V1 assertions document; raw attachments/files are supplied separately. */
[[nodiscard]] std::string
write_manufacturing_evidence(std::span<const ManufacturingReviewAssertion> assertions);
/** Strict V1 decode; no result is trusted and all bindings are reverified later. */
[[nodiscard]] std::vector<ManufacturingReviewAssertion>
read_manufacturing_evidence(std::string_view text);
/** Closed structural document failure. */
enum class ManufacturingEvidenceReadErrorCode { Malformed, UnsupportedVersion };

/** Typed malformed/unsupported document failure; no partially decoded input. */
class ManufacturingEvidenceReadError final : public std::runtime_error {
  public:
    /** Retain exact failure category and explanation. */
    ManufacturingEvidenceReadError(ManufacturingEvidenceReadErrorCode code, std::string message)
        : std::runtime_error{message}, code_{code} {}

    /** Return stable category. */
    [[nodiscard]] ManufacturingEvidenceReadErrorCode code() const noexcept { return code_; }

  private:
    ManufacturingEvidenceReadErrorCode code_;
};
} // namespace volt::io
