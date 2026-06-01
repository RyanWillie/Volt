#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace volt {

/** Physical drawing size for a schematic sheet. */
class SheetSize {
  public:
    /** Construct a sheet size from positive finite dimensions. */
    SheetSize(double width = 297.0, double height = 210.0);

    /** Return the sheet width. */
    [[nodiscard]] double width() const noexcept { return width_; }

    /** Return the sheet height. */
    [[nodiscard]] double height() const noexcept { return height_; }

    /** Return whether two sheet sizes have the same dimensions. */
    [[nodiscard]] friend bool operator==(SheetSize lhs, SheetSize rhs) noexcept = default;

  private:
    double width_;
    double height_;
};

/** Page orientation for a schematic drawing sheet. */
enum class SheetOrientation {
    Portrait,
    Landscape,
};

/** Margins between the physical sheet frame and the drawing area. */
class SheetMargins {
  public:
    /** Construct sheet margins from finite non-negative distances. */
    SheetMargins(double left = 10.0, double top = 10.0, double right = 10.0, double bottom = 10.0);

    /** Return the left margin. */
    [[nodiscard]] double left() const noexcept { return left_; }

    /** Return the top margin. */
    [[nodiscard]] double top() const noexcept { return top_; }

    /** Return the right margin. */
    [[nodiscard]] double right() const noexcept { return right_; }

    /** Return the bottom margin. */
    [[nodiscard]] double bottom() const noexcept { return bottom_; }

    /** Return whether two margin sets are identical. */
    [[nodiscard]] friend bool operator==(SheetMargins lhs, SheetMargins rhs) noexcept = default;

  private:
    double left_;
    double top_;
    double right_;
    double bottom_;
};

/** Outer frame configuration for a schematic drawing sheet. */
class SheetFrame {
  public:
    /** Construct a sheet frame with optional visibility and margins. */
    explicit SheetFrame(bool visible = true, SheetMargins margins = {});

    /** Return whether the outer frame should be visible. */
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    /** Return the inner drawing-area margins. */
    [[nodiscard]] SheetMargins margins() const noexcept { return margins_; }

    /** Return whether two frame configurations are identical. */
    [[nodiscard]] friend bool operator==(SheetFrame lhs, SheetFrame rhs) noexcept = default;

  private:
    bool visible_;
    SheetMargins margins_;
};

/** Optional coordinate zone labels shown along a schematic sheet border. */
class SheetCoordinateZones {
  public:
    /** Construct border coordinate zones from positive row and column counts. */
    SheetCoordinateZones(std::size_t columns, std::size_t rows, bool visible = true);

    /** Return the number of horizontal zones. */
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }

    /** Return the number of vertical zones. */
    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }

    /** Return whether coordinate zone labels should be visible. */
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    /** Return whether two coordinate-zone configurations are identical. */
    [[nodiscard]] friend bool operator==(SheetCoordinateZones lhs,
                                         SheetCoordinateZones rhs) noexcept = default;

  private:
    std::size_t columns_;
    std::size_t rows_;
    bool visible_;
};

/** Optional visible grid metadata for a schematic sheet. */
class SheetGrid {
  public:
    /** Construct grid metadata from a positive finite spacing. */
    explicit SheetGrid(double spacing, bool visible = true);

    /** Return the grid spacing in sheet units. */
    [[nodiscard]] double spacing() const noexcept { return spacing_; }

    /** Return whether the grid should be visible. */
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    /** Return whether two grid configurations are identical. */
    [[nodiscard]] friend bool operator==(SheetGrid lhs, SheetGrid rhs) noexcept = default;

  private:
    double spacing_;
    bool visible_;
};

/** One key/value entry in a schematic sheet title block. */
class TitleBlockField {
  public:
    /** Construct a title-block field. */
    TitleBlockField(std::string key, std::string value);

    /** Return the field key. */
    [[nodiscard]] const std::string &key() const noexcept { return key_; }

    /** Return the field value. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two title-block fields are identical. */
    [[nodiscard]] friend bool operator==(const TitleBlockField &lhs,
                                         const TitleBlockField &rhs) noexcept = default;

  private:
    std::string key_;
    std::string value_;
};

/** One key/value style metadata entry for a schematic region. */
class SheetRegionStyleField {
  public:
    /** Construct a region style field. */
    SheetRegionStyleField(std::string key, std::string value);

    /** Return the style field key. */
    [[nodiscard]] const std::string &key() const noexcept { return key_; }

    /** Return the style field value. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two style fields are identical. */
    [[nodiscard]] friend bool operator==(const SheetRegionStyleField &lhs,
                                         const SheetRegionStyleField &rhs) noexcept = default;

  private:
    std::string key_;
    std::string value_;
};

/** Sheet-local rectangular bounds for a named schematic region. */
class SheetRegionBounds {
  public:
    /** Construct region bounds from an origin and positive size. */
    SheetRegionBounds(double x, double y, double width, double height);

    /** Return the region's sheet-local x origin. */
    [[nodiscard]] double x() const noexcept { return x_; }

    /** Return the region's sheet-local y origin. */
    [[nodiscard]] double y() const noexcept { return y_; }

    /** Return the region width. */
    [[nodiscard]] double width() const noexcept { return width_; }

    /** Return the region height. */
    [[nodiscard]] double height() const noexcept { return height_; }

    /** Return whether two region bounds are identical. */
    [[nodiscard]] friend bool operator==(SheetRegionBounds lhs,
                                         SheetRegionBounds rhs) noexcept = default;

  private:
    double x_;
    double y_;
    double width_;
    double height_;
};

/** A named functional drawing region on a physical schematic sheet. */
class SheetRegion {
  public:
    /** Construct a named rectangular sheet region. */
    SheetRegion(std::string name, std::string title, SheetRegionBounds bounds,
                std::vector<SheetRegionStyleField> style = {});

    /** Return the stable region name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the displayed region title. */
    [[nodiscard]] const std::string &title() const noexcept { return title_; }

    /** Return the sheet-local region bounds. */
    [[nodiscard]] SheetRegionBounds bounds() const noexcept { return bounds_; }

    /** Return region style metadata in insertion order. */
    [[nodiscard]] const std::vector<SheetRegionStyleField> &style() const noexcept;

    /** Return whether two regions are identical. */
    [[nodiscard]] friend bool operator==(const SheetRegion &lhs,
                                         const SheetRegion &rhs) noexcept = default;

  private:
    std::string name_;
    std::string title_;
    SheetRegionBounds bounds_;
    std::vector<SheetRegionStyleField> style_;
};

/** Sheet-level metadata used by renderers and authoring tools. */
class SheetMetadata {
  public:
    /** Construct sheet metadata from a title, size, and optional title-block fields. */
    explicit SheetMetadata(std::string title, SheetSize size = {},
                           std::vector<TitleBlockField> title_block = {},
                           SheetOrientation orientation = SheetOrientation::Landscape,
                           SheetFrame frame = SheetFrame{},
                           std::optional<SheetCoordinateZones> coordinate_zones = std::nullopt,
                           std::optional<SheetGrid> grid = std::nullopt);

    /** Return the displayed sheet title. */
    [[nodiscard]] const std::string &title() const noexcept { return title_; }

    /** Return the sheet drawing size. */
    [[nodiscard]] SheetSize size() const noexcept { return size_; }

    /** Return the sheet orientation. */
    [[nodiscard]] SheetOrientation orientation() const noexcept { return orientation_; }

    /** Return title-block fields in insertion order. */
    [[nodiscard]] const std::vector<TitleBlockField> &title_block() const noexcept;

    /** Return the sheet frame and margins. */
    [[nodiscard]] SheetFrame frame() const noexcept { return frame_; }

    /** Return optional coordinate zone metadata. */
    [[nodiscard]] const std::optional<SheetCoordinateZones> &coordinate_zones() const noexcept;

    /** Return optional visible grid metadata. */
    [[nodiscard]] const std::optional<SheetGrid> &grid() const noexcept { return grid_; }

    /** Return whether two metadata objects are identical. */
    [[nodiscard]] friend bool operator==(const SheetMetadata &lhs,
                                         const SheetMetadata &rhs) noexcept = default;

  private:
    std::string title_;
    SheetSize size_;
    SheetOrientation orientation_;
    std::vector<TitleBlockField> title_block_;
    SheetFrame frame_;
    std::optional<SheetCoordinateZones> coordinate_zones_;
    std::optional<SheetGrid> grid_;
};

} // namespace volt
