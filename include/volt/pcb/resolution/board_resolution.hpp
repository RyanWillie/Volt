#pragma once

#include <concepts>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

#include <volt/circuit/parts/selected_part.hpp>
#include <volt/core/content_hash.hpp>
#include <volt/pcb/board.hpp>

namespace volt {

class BoardResolution;
class CompiledBoard;

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
    /** Record one exact component-to-part resolution for boundary validation. */
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

/** Non-owning resolved physical view backed by BoardResolution or CompiledBoard storage. */
class ResolvedBoardView {
  public:
    /** Bind one complete verified BoardResolution owner. */
    [[nodiscard]] static ResolvedBoardView from(const BoardResolution &resolution);
    [[nodiscard]] static ResolvedBoardView from(BoardResolution &&resolution) = delete;

    /** Bind one complete immutable CompiledBoard owner. */
    [[nodiscard]] static ResolvedBoardView from(const CompiledBoard &compiled);
    [[nodiscard]] static ResolvedBoardView from(CompiledBoard &&compiled) = delete;

#if defined(VOLT_TEST_SUPPORT)
    /** Construct an explicitly partial raw view for isolated diagnostic and geometry tests. */
    [[nodiscard]] static ResolvedBoardView test_only(const Board &board,
                                                     const FootprintLibrary &footprints,
                                                     std::span<const ResolvedBoardPart> parts) {
        return ResolvedBoardView{board, footprints, parts, false};
    }

    template <std::ranges::contiguous_range Range>
        requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>,
                              ResolvedBoardPart> &&
                     (!std::ranges::borrowed_range<Range>)
    [[nodiscard]] static auto test_only(const Board &board, const FootprintLibrary &footprints,
                                        Range &&parts) -> ResolvedBoardView = delete;
#endif

    /** Return the exact named Board. */
    [[nodiscard]] const Board &board() const;

    /** Return the exact resolved footprint definitions. */
    [[nodiscard]] const FootprintLibrary &footprints() const;

    /** Return all exact selected implementations in component order. */
    [[nodiscard]] std::span<const ResolvedBoardPart> parts() const;

    /** Return the exact resolved implementation for a component, or null when none is selected. */
    [[nodiscard]] const ResolvedBoardPart *part(ComponentId component) const;

  private:
    ResolvedBoardView(const Board &board, const FootprintLibrary &footprints,
                      std::span<const ResolvedBoardPart> parts, bool complete);

    void require_current() const;

    const Board *board_;
    const FootprintLibrary *footprints_;
    std::span<const ResolvedBoardPart> parts_;
    bool complete_;
};

/**
 * Immutable authoring-time physical resolution for exactly one named Board and selected closure.
 *
 * Construction is all-or-nothing through the public IO resolution boundary; Board, Circuit, and
 * bundle inputs are never mutated and no ambient resolver or cache participates.
 */
class BoardResolution {
  public:
    /** Exact IO owner allowed to publish one fully verified resolution. */
    class Codec;

    BoardResolution(const BoardResolution &) = delete;
    BoardResolution &operator=(const BoardResolution &) = delete;
    BoardResolution(BoardResolution &&) = delete;
    BoardResolution &operator=(BoardResolution &&) = delete;

    /** Return the exact named authoring Board. */
    [[nodiscard]] const Board &board() const;

    /** Return the stable name of the resolved authoring Board. */
    [[nodiscard]] const BoardName &board_name() const noexcept { return board_name_; }

    /** Return the semantic identity of the exact selected P6 closure. */
    [[nodiscard]] const ContentHash &closure_digest() const noexcept { return closure_digest_; }

    /** Return the explicit capability snapshot validated during resolution. */
    [[nodiscard]] const BoardResolutionCapabilities &capabilities() const noexcept {
        return capabilities_;
    }

    /** Return the complete native footprint definitions decoded from the selected closure. */
    [[nodiscard]] const FootprintLibrary &footprints() const;

    /** Return all exact selected implementations in component order. */
    [[nodiscard]] std::span<const ResolvedBoardPart> parts() const;

    /** Return the exact resolved implementation for a component, or null when none is selected. */
    [[nodiscard]] const ResolvedBoardPart *part(ComponentId component) const;

    /** Return the explicit non-owning consumer view backed by this resolution. */
    [[nodiscard]] ResolvedBoardView view() const &;

    [[nodiscard]] ResolvedBoardView view() const && = delete;

  private:
    [[nodiscard]] static BoardResolution
    materialize_verified(const Board &board, ContentHash closure_digest,
                         BoardResolutionCapabilities capabilities, FootprintLibrary footprints,
                         std::vector<ResolvedBoardPart> parts);

    BoardResolution(const Board &board, ContentHash closure_digest,
                    BoardResolutionCapabilities capabilities, FootprintLibrary footprints,
                    std::vector<ResolvedBoardPart> parts);

    const Board *board_;
    BoardName board_name_;
    ContentHash closure_digest_;
    BoardResolutionCapabilities capabilities_;
    FootprintLibrary footprints_;
    std::vector<ResolvedBoardPart> parts_;
};

} // namespace volt
