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
    /** Exact logical component identity. */
    ComponentId component;
    /** Native assembly intent; absent DNP is represented as false. */
    bool dnp;
    /** Exact selected implementation, when present. */
    std::optional<LibraryPartRef> selection;
    /** Physical placement on this board, when present. */
    std::optional<ComponentPlacementId> placement;
    /** Whether the selected footprint is resolved in the frozen artifact. */
    bool footprint_resolved;
};

/** Exact logical pin intent and concrete required copper contacts. */
struct BoardCompletionPin {
    /** Exact logical pin identity. */
    PinId pin;
    /** Logical component owning this pin. */
    ComponentId component;
    /** Native pin connection requirement. */
    ConnectionRequirement requirement;
    /** Effective explicit or intrinsic no-connect intent. */
    bool no_connect;
    /** Existing logical net membership, without inference from copper. */
    std::optional<NetId> net;
    /** Whether this pin requires concrete mapped copper contacts. */
    bool physical_required;
    /** All native mapped pad identities in stable order. */
    std::vector<FootprintPadId> mapped_pads;
    /** Required conductor indices into the completion copper inventory. */
    std::vector<std::size_t> copper;
};

/** One conductor, including unlabelled copper; zone layers are separate conductors. */
struct BoardCompletionCopper {
    /** Normalized emitted conductor primitive. */
    BoardCompletionCopperKind kind;
    /** Existing logical label; unlabelled conductors remain unlabelled. */
    std::optional<NetId> net;
    /** Occupied copper layers, including native plated interlayer span. */
    std::vector<BoardLayerId> layers;
    /** Exact reporting identities of the source geometry. */
    std::vector<EntityRef> entities;
    /** Board-space centre, segment endpoints or polygon vertices. */
    std::vector<BoardPoint> points;
    /** Disc radius or segment half-width in millimetres; zero for polygons. */
    double radius_mm;
    /** Source footprint placement for pad copper, when applicable. */
    std::optional<ComponentPlacementId> placement;
    /** Source footprint pad identity, when applicable. */
    std::optional<FootprintPadId> pad;
    /** Native mapped logical pin, when applicable. */
    std::optional<PinId> pin;
    /** Whether unintended contact to this conductor violates pin isolation. */
    bool isolated;
    /** Whether full supported geometry coverage is established. */
    bool supported;
    /** Whether exact emitted contact can be analyzed despite a fidelity limitation. */
    bool contact_evaluated = true;
};

/** Proven connected-component membership, never inferred from matching net labels. */
struct BoardCompletionIsland {
    /** Conductor indices belonging to this proven physical island. */
    std::vector<std::size_t> copper;
    /** Preserved logical group representatives reached by this island. */
    std::vector<NetId> logical_groups;
};

/** One fixed coverage row with exact reporting scope. */
struct BoardCompletionCoverage {
    /** Closed mandatory requirement key. */
    BoardCompletionCheck check;
    /** Proven pass, proven failure or explicit coverage gap. */
    BoardCompletionState state;
    /** Exact ordered reporting scope of this row. */
    std::vector<EntityRef> entities;
    /** Explanation of the evaluated result or missing coverage. */
    std::string reason;
};

/** Identity of actual native export bytes, produced only by IO evaluation. */
struct BoardCompletionFile {
    /** Native export filename, independent of any checkout path. */
    std::string filename;
    /** Native fabrication file function. */
    std::string function;
    /** Content identity of the actual emitted file bytes. */
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

    /** Return a new value with one IO-produced export evaluation composed into native facts. */
    [[nodiscard]] BoardCompletion
    with_export_evaluation(BoardCompletionCoverage coverage, DiagnosticReport export_diagnostics,
                           std::vector<BoardCompletionFile> files) &&;

  private:
    friend BoardCompletion evaluate_board_completion(const CompiledBoard &);
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
