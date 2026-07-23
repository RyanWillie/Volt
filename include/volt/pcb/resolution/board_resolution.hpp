#pragma once

#include <optional>
#include <span>
#include <vector>

#include <volt/circuit/parts/selected_part.hpp>
#include <volt/core/content_hash.hpp>
#include <volt/pcb/board.hpp>

namespace volt {

/** Additional exact asset families required by one Board resolution. */
enum class BoardAssetCapability {
    Models3D,
};

/** Explicit closed capability input for native Board resolution. */
class BoardResolutionCapabilities {
  public:
    /** Build an explicit capability snapshot from one optional profile and asset requirements. */
    BoardResolutionCapabilities(std::optional<BoardCapabilityProfile> profile,
                                std::vector<BoardAssetCapability> additional = {});

    /** Return the exact capability-profile snapshot supplied by the caller. */
    [[nodiscard]] const std::optional<BoardCapabilityProfile> &profile() const noexcept {
        return profile_;
    }

    /** Return normalized additional asset capabilities. */
    [[nodiscard]] std::span<const BoardAssetCapability> additional() const noexcept {
        return additional_;
    }

    /** Return whether one additional asset capability was requested. */
    [[nodiscard]] bool has(BoardAssetCapability capability) const noexcept;

  private:
    std::optional<BoardCapabilityProfile> profile_;
    std::vector<BoardAssetCapability> additional_;
};

/** One exact selected implementation resolved for a Circuit component. */
class ResolvedBoardPart {
  public:
    /** Build one fully validated component-to-part resolution record. */
    ResolvedBoardPart(ComponentId component, LibraryPartRef reference, PhysicalPart physical_part,
                      std::optional<std::string> model_3d_bytes = std::nullopt);

    /** Return the logical component implemented by this selected part. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the exact reference proven against the selected closure. */
    [[nodiscard]] const LibraryPartRef &reference() const noexcept { return reference_; }

    /** Return the validated physical implementation derived from the exact part. */
    [[nodiscard]] const PhysicalPart &physical_part() const noexcept { return physical_part_; }

    /** Return the validated model bytes when the selected part has a 3D asset. */
    [[nodiscard]] const std::optional<std::string> &model_3d_bytes() const noexcept {
        return model_3d_bytes_;
    }

  private:
    ComponentId component_;
    LibraryPartRef reference_;
    PhysicalPart physical_part_;
    std::optional<std::string> model_3d_bytes_;
};

/**
 * Immutable authoring-time physical resolution for exactly one named Board and selected closure.
 *
 * Construction is all-or-nothing through the public IO resolution boundary; Board, Circuit, and
 * bundle inputs are never mutated and no ambient resolver or cache participates.
 */
class BoardResolution {
  public:
    BoardResolution(const BoardResolution &) = delete;
    BoardResolution &operator=(const BoardResolution &) = delete;
    BoardResolution(BoardResolution &&) = delete;
    BoardResolution &operator=(BoardResolution &&) = delete;

    /** Atomically materialize already-resolved native inputs into the immutable PCB result. */
    [[nodiscard]] static BoardResolution materialize(const Board &board, ContentHash closure_digest,
                                                     BoardResolutionCapabilities capabilities,
                                                     FootprintLibrary footprints,
                                                     std::vector<ResolvedBoardPart> parts);

    /** Return the immutable physically resolved projection consumed by PCB algorithms. */
    [[nodiscard]] const Board &board() const noexcept { return resolved_board_; }

    /** Return the exact named authoring Board from which this resolution was built. */
    [[nodiscard]] const Board &authoring_board() const noexcept { return *authoring_board_; }

    /** Return the stable name of the resolved authoring Board. */
    [[nodiscard]] const BoardName &board_name() const noexcept { return board_name_; }

    /** Return the semantic identity of the exact selected P6 closure. */
    [[nodiscard]] const ContentHash &closure_digest() const noexcept { return closure_digest_; }

    /** Return the explicit capability snapshot validated during resolution. */
    [[nodiscard]] const BoardResolutionCapabilities &capabilities() const noexcept {
        return capabilities_;
    }

    /** Return the complete native footprint definitions decoded from the selected closure. */
    [[nodiscard]] const FootprintLibrary &footprints() const noexcept { return footprints_; }

    /** Return all exact selected implementations in component order. */
    [[nodiscard]] std::span<const ResolvedBoardPart> parts() const noexcept { return parts_; }

    /** Return the exact resolved implementation for a component, or null when none is selected. */
    [[nodiscard]] const ResolvedBoardPart *part(ComponentId component) const noexcept;

  private:
    BoardResolution(const Board &board, ContentHash closure_digest,
                    BoardResolutionCapabilities capabilities, FootprintLibrary footprints,
                    std::vector<ResolvedBoardPart> parts);

    const Board *authoring_board_;
    Circuit resolved_circuit_;
    Board resolved_board_;
    BoardName board_name_;
    ContentHash closure_digest_;
    BoardResolutionCapabilities capabilities_;
    FootprintLibrary footprints_;
    std::vector<ResolvedBoardPart> parts_;
};

} // namespace volt
