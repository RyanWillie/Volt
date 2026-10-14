#pragma once
#include "copper/board_copper_detail.hpp"
#include <volt/pcb/compiled/board_completion.hpp>

namespace volt::detail {
struct CompletionOpening {
    BoardCopperShapeKind kind;
    std::vector<BoardPoint> points;
    double radius_mm;
    std::vector<EntityRef> entities;
};

struct CompletionWork {
    explicit CompletionWork(const CompiledBoard &source);
    const CompiledBoard &compiled;
    std::vector<BoardCompletionComponent> components;
    std::vector<BoardCompletionPin> pins;
    std::vector<BoardCompletionCopper> copper;
    std::vector<BoardCompletionIsland> islands;
    std::array<BoardCompletionCoverage, 7> coverage;
    DiagnosticReport diagnostics;
    std::vector<CompletionOpening> openings;
    void note(BoardCompletionCheck check, BoardCompletionState state, std::string_view code,
              std::string message, std::vector<EntityRef> entities = {},
              std::optional<DiagnosticMeasurement> measurement = std::nullopt);
};

[[nodiscard]] std::vector<EntityRef> completion_entities(const BoardCompletionCopper &copper);
[[nodiscard]] CopperPrimitiveView primitive(const BoardCompletionCopper &copper);
void inventory_completion(CompletionWork &work);
void bind_completion_contacts(CompletionWork &work);
void inventory_completion_copper(CompletionWork &work);
void connect_completion_copper(CompletionWork &work);
void validate_completion_groups(CompletionWork &work);
} // namespace volt::detail
