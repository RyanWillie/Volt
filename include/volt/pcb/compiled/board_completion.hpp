#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <volt/pcb/compiled/compiled_board.hpp>

namespace volt {
class BoardCompletion;

namespace io {
/** Export and evaluate one immutable revision using actual native fabrication bytes. */
[[nodiscard]] BoardCompletion evaluate_fabrication_completion(const CompiledBoard &compiled);
} // namespace io

/** Finite physical completion, never manufacturing-target readiness. */
enum class BoardCompletionStatus { Complete, Incomplete, NotEvaluated };
/** State of one mandatory physical-completion coverage row. */
enum class BoardCompletionState { Pass, Failed, NotEvaluated };
/** Closed mandatory coverage inventory for evaluator version 1. */
enum class BoardCompletionCheck {
    LogicalContinuity,
    Inventory,
    RequiredContacts,
    Connectivity,
    UnintendedContacts,
    Geometry,
    ExportFidelity
};
/** Normalized supported copper primitive; no logical connectivity ownership. */
enum class BoardCompletionCopperKind { Disc, Segment, Polygon };

/** Exact logical component inventory, including unplaced components. */
struct BoardCompletionComponent {
    ComponentId component;
    bool dnp;
    std::optional<LibraryPartRef> selection;
    std::optional<ComponentPlacementId> placement;
    bool footprint_resolved;
};

/** Exact logical pin intent and concrete required copper contacts. */
struct BoardCompletionPin {
    PinId pin;
    ComponentId component;
    ConnectionRequirement requirement;
    bool no_connect;
    std::optional<NetId> net;
    bool physical_required;
    std::vector<FootprintPadId> mapped_pads;
    std::vector<std::size_t> copper;
};

/** One conductor, including unlabelled copper; zone layers are separate conductors. */
struct BoardCompletionCopper {
    BoardCompletionCopperKind kind;
    std::optional<NetId> net;
    std::vector<BoardLayerId> layers;
    std::vector<EntityRef> entities;
    std::vector<BoardPoint> points;
    double radius_mm;
    std::optional<ComponentPlacementId> placement;
    std::optional<FootprintPadId> pad;
    std::optional<PinId> pin;
    bool isolated;
    bool supported;
    /** Whether exact emitted contact can be analyzed despite a fidelity limitation. */
    bool contact_evaluated = true;
};

/** Proven connected-component membership, never inferred from matching net labels. */
struct BoardCompletionIsland {
    std::vector<std::size_t> copper;
    std::vector<NetId> logical_groups;
};

/** One fixed coverage row with exact reporting scope. */
struct BoardCompletionCoverage {
    BoardCompletionCheck check;
    BoardCompletionState state;
    std::vector<EntityRef> entities;
    std::string reason;
};

/** Identity of actual native export bytes, produced only by IO evaluation. */
struct BoardCompletionFile {
    std::string filename;
    std::string function;
    ContentHash digest;
};

/** Owned read-only completion evidence derived from one immutable artifact. */
class BoardCompletion final {
  public:
    /** Return exact source identity. */
    [[nodiscard]] const CompiledBoardIdentity &source() const noexcept { return source_; }

    /** Return exact source artifact byte identity. */
    [[nodiscard]] const ContentHash &content_digest() const noexcept { return content_digest_; }

    /** Return exact preserved native groups; V1 has no authoritative group inventory. */
    [[nodiscard]] const std::optional<std::vector<std::vector<NetId>>> &
    logical_groups() const noexcept {
        return logical_groups_;
    }

    /** Return full source compilation provenance. */
    [[nodiscard]] const CompiledBoardProvenance &provenance() const noexcept { return provenance_; }

    /** Derive status from all fixed mandatory rows; failures are not hidden by gaps. */
    [[nodiscard]] BoardCompletionStatus status() const noexcept;

    /** Return artifact-wide component inventory in concrete ID order. */
    [[nodiscard]] std::span<const BoardCompletionComponent> components() const noexcept {
        return components_;
    }

    /** Return artifact-wide pin inventory in concrete ID order. */
    [[nodiscard]] std::span<const BoardCompletionPin> pins() const noexcept { return pins_; }

    /** Return all normalized copper conductors in stable source order. */
    [[nodiscard]] std::span<const BoardCompletionCopper> copper() const noexcept { return copper_; }

    /** Return physical islands in lowest-copper-index order. */
    [[nodiscard]] std::span<const BoardCompletionIsland> islands() const noexcept {
        return islands_;
    }

    /** Return the closed coverage inventory, including explicit gaps. */
    [[nodiscard]] std::span<const BoardCompletionCoverage> coverage() const noexcept {
        return coverage_;
    }

    /** Return all exact native diagnostics, without waivers or message filtering. */
    [[nodiscard]] const DiagnosticReport &diagnostics() const noexcept { return diagnostics_; }

    /** Return actual native export byte identities; empty before IO composition. */
    [[nodiscard]] std::span<const BoardCompletionFile> files() const noexcept { return files_; }

  private:
    friend BoardCompletion evaluate_board_completion(const CompiledBoard &);
    friend BoardCompletion io::evaluate_fabrication_completion(const CompiledBoard &);
    explicit BoardCompletion(const CompiledBoard &compiled);
    void order_diagnostics();
    CompiledBoardIdentity source_;
    ContentHash content_digest_;
    CompiledBoardProvenance provenance_;
    std::optional<std::vector<std::vector<NetId>>> logical_groups_;
    std::vector<BoardCompletionComponent> components_;
    std::vector<BoardCompletionPin> pins_;
    std::vector<BoardCompletionCopper> copper_;
    std::vector<BoardCompletionIsland> islands_;
    std::array<BoardCompletionCoverage, 7> coverage_;
    DiagnosticReport diagnostics_;
    std::vector<BoardCompletionFile> files_;
};

/** Evaluate native facts; export-fidelity remains NotEvaluated until IO composition. */
[[nodiscard]] BoardCompletion evaluate_board_completion(const CompiledBoard &compiled);
} // namespace volt
