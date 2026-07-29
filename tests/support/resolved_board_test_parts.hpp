#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <volt/pcb/resolution/board_resolution.hpp>

namespace volt::test {

/** Explicit test owner for exact physical parts consumed through ResolvedBoardView. */
class ResolvedBoardTestParts {
  public:
    void set(ComponentId component, PhysicalPart part) {
        const auto existing = std::ranges::find(parts_, component, &ResolvedBoardPart::component);
        auto resolved = ResolvedBoardPart{
            component,
            LibraryPartRef{
                "test.resolved", "1", PartKey{"part-" + std::to_string(component.index())},
                sha256_content_hash("test.resolved.library"),
                sha256_content_hash("test.resolved.part." + std::to_string(component.index()))},
            std::move(part)};
        if (existing == parts_.end()) {
            parts_.push_back(std::move(resolved));
            std::ranges::sort(parts_, [](const auto &lhs, const auto &rhs) {
                return lhs.component().index() < rhs.component().index();
            });
        } else {
            *existing = std::move(resolved);
        }
    }

    [[nodiscard]] ResolvedBoardView view(const Board &board,
                                         const FootprintLibrary &footprints) const noexcept {
        return ResolvedBoardView{board, footprints, parts_};
    }

    [[nodiscard]] ResolvedBoardView view_builtin(const Board &board) const noexcept {
        return ResolvedBoardView{board, builtin_footprints_, parts_};
    }

    [[nodiscard]] std::span<const ResolvedBoardPart> parts() const noexcept { return parts_; }

  private:
    FootprintLibrary builtin_footprints_ = builtin_footprint_library();
    std::vector<ResolvedBoardPart> parts_;
};

} // namespace volt::test
