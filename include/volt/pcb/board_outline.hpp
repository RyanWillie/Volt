#pragma once

#include <vector>

#include <volt/pcb/board_geometry.hpp>

namespace volt {

/** Closed polygonal board outline in board coordinates. */
class BoardOutline {
  public:
    /** Construct a closed outline from polygon vertices. */
    explicit BoardOutline(std::vector<BoardPoint> vertices);

    /** Construct a rectangular board outline from origin and positive size. */
    [[nodiscard]] static BoardOutline rectangle(BoardPoint origin, BoardSize size);

    /** Return outline vertices in deterministic boundary order. */
    [[nodiscard]] const std::vector<BoardPoint> &vertices() const noexcept { return vertices_; }

    /** Return whether the point is inside or on the board outline. */
    [[nodiscard]] bool contains(BoardPoint point) const noexcept;

  private:
    static constexpr double kGeometryEpsilon = 1.0e-9;

    void drop_duplicate_closing_vertex();

    [[nodiscard]] static double signed_area_twice(const std::vector<BoardPoint> &vertices);

    [[nodiscard]] static bool point_on_segment(BoardPoint point, BoardPoint a,
                                               BoardPoint b) noexcept;

    [[nodiscard]] static double orientation(BoardPoint a, BoardPoint b, BoardPoint c) noexcept;

    [[nodiscard]] static bool segments_intersect(BoardPoint a, BoardPoint b, BoardPoint c,
                                                 BoardPoint d) noexcept;

    [[nodiscard]] static bool has_self_intersection(const std::vector<BoardPoint> &vertices);

    std::vector<BoardPoint> vertices_;
};

/** Closed polygon used by generic board primitives. */
class BoardPolygon {
  public:
    /** Construct a non-degenerate polygon from boundary vertices. */
    explicit BoardPolygon(std::vector<BoardPoint> vertices);

    /** Return polygon vertices in deterministic boundary order. */
    [[nodiscard]] const std::vector<BoardPoint> &vertices() const noexcept { return vertices_; }

  private:
    static constexpr double kGeometryEpsilon = 1.0e-9;

    void drop_duplicate_closing_vertex();

    [[nodiscard]] static double signed_area_twice(const std::vector<BoardPoint> &vertices);

    [[nodiscard]] static bool point_on_segment(BoardPoint point, BoardPoint a,
                                               BoardPoint b) noexcept;

    [[nodiscard]] static double orientation(BoardPoint a, BoardPoint b, BoardPoint c) noexcept;

    [[nodiscard]] static bool segments_intersect(BoardPoint a, BoardPoint b, BoardPoint c,
                                                 BoardPoint d) noexcept;

    [[nodiscard]] static bool has_self_intersection(const std::vector<BoardPoint> &vertices);

    std::vector<BoardPoint> vertices_;
};

} // namespace volt
