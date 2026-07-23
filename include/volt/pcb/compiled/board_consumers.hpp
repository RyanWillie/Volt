#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <volt/pcb/assembly/cpl.hpp>
#include <volt/pcb/compiled/compiled_board.hpp>
#include <volt/pcb/features/board_features.hpp>
#include <volt/pcb/projection/board_geometry_projection.hpp>

namespace volt {

/** Physical-validation evidence tied to one exact CompiledBoard revision. */
class CompiledBoardValidation {
  public:
    /** Bind one deterministic diagnostic report to its exact immutable source revision. */
    CompiledBoardValidation(CompiledBoardIdentity source, DiagnosticReport diagnostics);

    /** Return the exact immutable physical revision that was validated. */
    [[nodiscard]] const CompiledBoardIdentity &source() const noexcept { return source_; }

    /** Return deterministic design-quality diagnostics for that revision. */
    [[nodiscard]] const DiagnosticReport &diagnostics() const noexcept { return diagnostics_; }

  private:
    CompiledBoardIdentity source_;
    DiagnosticReport diagnostics_;
};

/** Deterministic ratsnest evidence tied to one exact CompiledBoard revision. */
class CompiledBoardRatsnest {
  public:
    /** Bind deterministic ratsnest edges to their exact immutable source revision. */
    CompiledBoardRatsnest(CompiledBoardIdentity source, std::vector<RatsnestEdge> edges);

    /** Return the exact immutable physical revision used by the query. */
    [[nodiscard]] const CompiledBoardIdentity &source() const noexcept { return source_; }

    /** Return deterministic ratsnest edges for that revision. */
    [[nodiscard]] std::span<const RatsnestEdge> edges() const noexcept { return edges_; }

  private:
    CompiledBoardIdentity source_;
    std::vector<RatsnestEdge> edges_;
};

/** CPL projection tied to one exact CompiledBoard revision. */
class CompiledBoardCpl {
  public:
    /** Bind one deterministic CPL projection to its exact immutable source revision. */
    CompiledBoardCpl(CompiledBoardIdentity source, Cpl cpl);

    /** Return the exact immutable physical revision used by the projection. */
    [[nodiscard]] const CompiledBoardIdentity &source() const noexcept { return source_; }

    /** Return deterministic placement rows and assembly diagnostics. */
    [[nodiscard]] const Cpl &cpl() const noexcept { return cpl_; }

  private:
    CompiledBoardIdentity source_;
    Cpl cpl_;
};

/** Checked reference to one GLB consumed by one exact CompiledBoard revision. */
class BoardSceneModelRef {
  public:
    /** Check one component's consumed GLB against an exact CompiledBoard models3d closure. */
    BoardSceneModelRef(const CompiledBoard &compiled, ComponentId component);

    /** Return the exact immutable physical revision that owns the asset. */
    [[nodiscard]] const CompiledBoardIdentity &source() const noexcept { return source_; }

    /** Return the digest of the exact consumed GLB bytes. */
    [[nodiscard]] const ContentHash &digest() const noexcept { return digest_; }

    /** Compare complete checked model references. */
    [[nodiscard]] bool operator==(const BoardSceneModelRef &other) const noexcept;

  private:
    CompiledBoardIdentity source_;
    ContentHash digest_;
};

/** Compact display metadata for one exact consumed GLB. */
class BoardSceneModel {
  public:
    /** Retain one checked exact-revision GLB reference for view consumers. */
    explicit BoardSceneModel(BoardSceneModelRef reference);

    /** Return the checked exact-revision asset reference. */
    [[nodiscard]] const BoardSceneModelRef &reference() const noexcept { return reference_; }

  private:
    BoardSceneModelRef reference_;
};

/** Compact render and selection record for one physical placement. */
class BoardScenePlacement {
  public:
    /** Build one display-only placement selection and transform record. */
    BoardScenePlacement(ComponentPlacementId placement, ComponentId component,
                        std::string reference, BoardPoint position, BoardRotation rotation,
                        BoardSide side, std::array<double, 16> transform,
                        std::optional<BoardSceneModelRef> model);

    /** Return the stable placement identity in the exact source revision. */
    [[nodiscard]] ComponentPlacementId placement() const noexcept { return placement_; }

    /** Return the stable logical component identity in the exact source revision. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the human-facing component reference. */
    [[nodiscard]] const std::string &reference() const noexcept { return reference_; }

    /** Return the board-space display origin. */
    [[nodiscard]] BoardPoint position() const noexcept { return position_; }

    /** Return the authored board-space display rotation. */
    [[nodiscard]] BoardRotation rotation() const noexcept { return rotation_; }

    /** Return the physical placement side. */
    [[nodiscard]] BoardSide side() const noexcept { return side_; }

    /** Return the complete deterministic model display transform. */
    [[nodiscard]] const std::array<double, 16> &transform() const noexcept { return transform_; }

    /** Return the checked consumed GLB reference when the placement has one. */
    [[nodiscard]] const std::optional<BoardSceneModelRef> &model() const noexcept { return model_; }

  private:
    ComponentPlacementId placement_;
    ComponentId component_;
    std::string reference_;
    BoardPoint position_;
    BoardRotation rotation_;
    BoardSide side_;
    std::array<double, 16> transform_;
    std::optional<BoardSceneModelRef> model_;
};

/**
 * Compact read/view artifact derived from exactly one immutable CompiledBoard revision.
 *
 * It carries display geometry, placement selections, transforms, and checked GLB references.
 * It does not contain logical connectivity, mutable Board state, footprint definitions, selected
 * part definitions, or compilation rules.
 */
class BoardScene {
  public:
    /** Derive and validate one scene from one exact immutable physical revision. */
    [[nodiscard]] static BoardScene from_compiled(const CompiledBoard &compiled);

    /** Return the exact immutable physical revision viewed by this scene. */
    [[nodiscard]] const CompiledBoardIdentity &source() const noexcept { return source_; }

    /** Return compact board-only display geometry. */
    [[nodiscard]] const BoardGeometryProjection &geometry() const noexcept { return geometry_; }

    /** Return deterministic placement render and selection records. */
    [[nodiscard]] std::span<const BoardScenePlacement> placements() const noexcept {
        return placements_;
    }

    /** Return exactly the GLB references consumed by this compiled revision. */
    [[nodiscard]] std::span<const BoardSceneModel> models() const noexcept { return models_; }

  private:
    BoardScene(CompiledBoardIdentity source, BoardGeometryProjection geometry,
               std::vector<BoardScenePlacement> placements, std::vector<BoardSceneModel> models);

    CompiledBoardIdentity source_;
    BoardGeometryProjection geometry_;
    std::vector<BoardScenePlacement> placements_;
    std::vector<BoardSceneModel> models_;
};

/** Validate one immutable physical revision without consulting authoring or resolver state. */
[[nodiscard]] CompiledBoardValidation validate_board(const CompiledBoard &compiled);

/** Compute deterministic ratsnest edges from one immutable physical revision. */
[[nodiscard]] CompiledBoardRatsnest compute_ratsnest(const CompiledBoard &compiled);

/** Project CPL rows from one immutable physical revision. */
[[nodiscard]] CompiledBoardCpl project_cpl(const CompiledBoard &compiled);

/** Project CPL rows with explicit display rotation offsets. */
[[nodiscard]] CompiledBoardCpl project_cpl(const CompiledBoard &compiled,
                                           const CplProjectionOptions &options);

/** Prepare one compact exact-revision BoardScene. */
[[nodiscard]] BoardScene prepare_board_scene(const CompiledBoard &compiled);

/** Resolve checked GLB bytes, rejecting foreign, stale, missing, or extraneous references. */
[[nodiscard]] std::string_view resolve_board_scene_model(const BoardScene &scene,
                                                         const CompiledBoard &compiled,
                                                         const BoardSceneModelRef &reference);
[[nodiscard]] std::string_view
resolve_board_scene_model(const BoardScene &scene, CompiledBoard &&compiled,
                          const BoardSceneModelRef &reference) = delete;

} // namespace volt
